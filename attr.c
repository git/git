/*
 * Handle git attributes.  See gitattributes(5) for a description of
 * the file syntax, and attr.h for a description of the API.
 *
 * One basic design decision here is that we are not going to support
 * an insanely large number of attributes.
 */

#define USE_THE_REPOSITORY_VARIABLE

#include "git-compat-util.h"
#include "config.h"
#include "environment.h"
#include "exec-cmd.h"
#include "attr.h"
#include "dir.h"
#include "gettext.h"
#include "path.h"
#include "utf8.h"
#include "quote.h"
#include "read-cache-ll.h"
#include "refs.h"
#include "revision.h"
#include "object-store-ll.h"
#include "setup.h"
#include "thread-utils.h"
#include "tree-walk.h"
#include "object-name.h"

char *git_attr_tree;

const char git_attr__true[] = "(builtin)true";
const char git_attr__false[] = "\0(builtin)false";
static const char git_attr__unknown[] = "(builtin)unknown";
#define ATTR__TRUE git_attr__true
#define ATTR__FALSE git_attr__false
#define ATTR__UNSET NULL
#define ATTR__UNKNOWN git_attr__unknown

struct git_attr {
	unsigned int attr_nr; /* unique attribute number */
	char name[FLEX_ARRAY]; /* attribute name */
};

const char *git_attr_name(const struct git_attr *attr)
{
	return attr->name;
}

struct attr_hashmap {
	struct hashmap map;
	pthread_mutex_t mutex;
};

static inline void hashmap_lock(struct attr_hashmap *map)
{
	pthread_mutex_lock(&map->mutex);
}

static inline void hashmap_unlock(struct attr_hashmap *map)
{
	pthread_mutex_unlock(&map->mutex);
}

/* The container for objects stored in "struct attr_hashmap" */
struct attr_hash_entry {
	struct hashmap_entry ent;
	const char *key; /* the key; memory should be owned by value */
	size_t keylen; /* length of the key */
	void *value; /* the stored value */
};

/* attr_hashmap comparison function */
static int attr_hash_entry_cmp(const void *cmp_data UNUSED,
			       const struct hashmap_entry *eptr,
			       const struct hashmap_entry *entry_or_key,
			       const void *keydata UNUSED)
{
	const struct attr_hash_entry *a, *b;

	a = container_of(eptr, const struct attr_hash_entry, ent);
	b = container_of(entry_or_key, const struct attr_hash_entry, ent);
	return (a->keylen != b->keylen) || strncmp(a->key, b->key, a->keylen);
}

/*
 * The global dictionary of all interned attributes.  This
 * is a singleton object which is shared between threads.
 * Access to this dictionary must be surrounded with a mutex.
 */
static struct attr_hashmap g_attr_hashmap = {
	.map = HASHMAP_INIT(attr_hash_entry_cmp, NULL),
};

/*
 * Retrieve the 'value' stored in a hashmap given the provided 'key'.
 * If there is no matching entry, return NULL.
 */
static void *attr_hashmap_get(struct attr_hashmap *map,
			      const char *key, size_t keylen)
{
	struct attr_hash_entry k;
	struct attr_hash_entry *e;

	hashmap_entry_init(&k.ent, memhash(key, keylen));
	k.key = key;
	k.keylen = keylen;
	e = hashmap_get_entry(&map->map, &k, ent, NULL);

	return e ? e->value : NULL;
}

/* Add 'value' to a hashmap based on the provided 'key'. */
static void attr_hashmap_add(struct attr_hashmap *map,
			     const char *key, size_t keylen,
			     void *value)
{
	struct attr_hash_entry *e;

	e = xmalloc(sizeof(struct attr_hash_entry));
	hashmap_entry_init(&e->ent, memhash(key, keylen));
	e->key = key;
	e->keylen = keylen;
	e->value = value;

	hashmap_add(&map->map, &e->ent);
}

struct all_attrs_item {
	const struct git_attr *attr;
	const char *value;
	/*
	 * If 'macro' is non-NULL, indicates that 'attr' is a macro based on
	 * the current attribute stack and contains a pointer to the match_attr
	 * definition of the macro
	 */
	const struct match_attr *macro;
};

/*
 * Reallocate and reinitialize the array of all attributes (which is used in
 * the attribute collection process) in 'check' based on the global dictionary
 * of attributes.
 */
static void all_attrs_init(struct attr_hashmap *map, struct attr_check *check)
{
	int i;
	unsigned int size;

	hashmap_lock(map);

	size = hashmap_get_size(&map->map);
	if (size < check->all_attrs_nr)
		BUG("interned attributes shouldn't be deleted");

	/*
	 * If the number of attributes in the global dictionary has increased
	 * (or this attr_check instance doesn't have an initialized all_attrs
	 * field), reallocate the provided attr_check instance's all_attrs
	 * field and fill each entry with its corresponding git_attr.
	 */
	if (size != check->all_attrs_nr) {
		struct attr_hash_entry *e;
		struct hashmap_iter iter;

		REALLOC_ARRAY(check->all_attrs, size);
		check->all_attrs_nr = size;

		hashmap_for_each_entry(&map->map, &iter, e,
					ent /* member name */) {
			const struct git_attr *a = e->value;
			check->all_attrs[a->attr_nr].attr = a;
		}
	}

	hashmap_unlock(map);

	/*
	 * Re-initialize every entry in check->all_attrs.
	 * This re-initialization can live outside of the locked region since
	 * the attribute dictionary is no longer being accessed.
	 */
	for (i = 0; i < check->all_attrs_nr; i++) {
		check->all_attrs[i].value = ATTR__UNKNOWN;
		check->all_attrs[i].macro = NULL;
	}
}

/*
 * Attribute name cannot begin with "builtin_" which
 * is a reserved namespace for built in attributes values.
 */
static int attr_name_reserved(const char *name)
{
	return starts_with(name, "builtin_");
}

static int attr_name_valid(const char *name, size_t namelen)
{
	/*
	 * Attribute name cannot begin with '-' and must consist of
	 * characters from [-A-Za-z0-9_.].
	 */
	if (namelen <= 0 || *name == '-')
		return 0;
	while (namelen--) {
		char ch = *name++;
		if (! (ch == '-' || ch == '.' || ch == '_' ||
		       ('0' <= ch && ch <= '9') ||
		       ('a' <= ch && ch <= 'z') ||
		       ('A' <= ch && ch <= 'Z')) )
			return 0;
	}
	return 1;
}

static void report_invalid_attr(const char *name, size_t len,
				const char *src, int lineno)
{
	struct strbuf err = STRBUF_INIT;
	strbuf_addf(&err, _("%.*s is not a valid attribute name"),
		    (int) len, name);
	fprintf(stderr, "%s: %s:%d\n", err.buf, src, lineno);
	strbuf_release(&err);
}

/*
 * Given a 'name', lookup and return the corresponding attribute in the global
 * dictionary.  If no entry is found, create a new attribute and store it in
 * the dictionary.
 */
static const struct git_attr *git_attr_internal(const char *name, size_t namelen)
{
	struct git_attr *a;

	if (!attr_name_valid(name, namelen))
		return NULL;

	hashmap_lock(&g_attr_hashmap);

	a = attr_hashmap_get(&g_attr_hashmap, name, namelen);

	if (!a) {
		FLEX_ALLOC_MEM(a, name, name, namelen);
		a->attr_nr = hashmap_get_size(&g_attr_hashmap.map);

		attr_hashmap_add(&g_attr_hashmap, a->name, namelen, a);
		if (a->attr_nr != hashmap_get_size(&g_attr_hashmap.map) - 1)
			die(_("unable to add additional attribute"));
	}

	hashmap_unlock(&g_attr_hashmap);

	return a;
}

const struct git_attr *git_attr(const char *name)
{
	return git_attr_internal(name, strlen(name));
}

/* What does a matched pattern decide? */
struct attr_state {
	const struct git_attr *attr;
	const char *setto;
};

struct pattern {
	const char *pattern;
	int patternlen;
	int nowildcardlen;
	unsigned flags;		/* PATTERN_FLAG_* */
};

/*
 * One rule, as from a .gitattributes file.
 *
 * If is_macro is true, then u.attr is a pointer to the git_attr being
 * defined.
 *
 * If is_macro is false, then u.pat is the filename pattern to which the
 * rule applies.
 *
 * In either case, num_attr is the number of attributes affected by
 * this rule, and state is an array listing them.  The attributes are
 * listed as they appear in the file (macros unexpanded).
 */
struct match_attr {
	union {
		struct pattern pat;
		const struct git_attr *attr;
	} u;
	char is_macro;
	size_t num_attr;
	struct attr_state state[FLEX_ARRAY];
};

static const char blank[] = " \t\r\n";

/* Flags usable in read_attr() and parse_attr_line() family of functions. */
#define READ_ATTR_MACRO_OK (1<<0)
#define READ_ATTR_NOFOLLOW (1<<1)

/*
 * Parse a whitespace-delimited attribute state (i.e., "attr",
 * "-attr", "!attr", or "attr=value") from the string starting at src.
 * If e is not NULL, write the results to *e.  Return a pointer to the
 * remainder of the string (with leading whitespace removed), or NULL
 * if there was an error.
 */
static const char *parse_attr(const char *src, int lineno, const char *cp,
			      struct attr_state *e)
{
	const char *ep, *equals;
	size_t len;

	ep = cp + strcspn(cp, blank);
	equals = strchr(cp, '=');
	if (equals && ep < equals)
		equals = NULL;
	if (equals)
		len = equals - cp;
	else
		len = ep - cp;
	if (!e) {
		if (*cp == '-' || *cp == '!') {
			cp++;
			len--;
		}
		if (!attr_name_valid(cp, len) || attr_name_reserved(cp)) {
			report_invalid_attr(cp, len, src, lineno);
			return NULL;
		}
	} else {
		/*
		 * As this function is always called twice, once with
		 * e == NULL in the first pass and then e != NULL in
		 * the second pass, no need for attr_name_valid()
		 * check here.
		 */
		if (*cp == '-' || *cp == '!') {
			e->setto = (*cp == '-') ? ATTR__FALSE : ATTR__UNSET;
			cp++;
			len--;
		}
		else if (!equals)
			e->setto = ATTR__TRUE;
		else {
			e->setto = xmemdupz(equals + 1, ep - equals - 1);
		}
		e->attr = git_attr_internal(cp, len);
	}
	return ep + strspn(ep, blank);
}

static struct match_attr *parse_attr_line(const char *line, const char *src,
					  int lineno, unsigned flags)
{
	size_t namelen, num_attr, i;
	const char *cp, *name, *states;
	struct match_attr *res = NULL;
	int is_macro;
	struct strbuf pattern = STRBUF_INIT;

	cp = line + strspn(line, blank);
	if (!*cp || *cp == '#')
		return NULL;
	name = cp;

	if (strlen(line) >= ATTR_MAX_LINE_LENGTH) {
		warning(_("ignoring overly long attributes line %d"), lineno);
		return NULL;
	}

	if (*cp == '"' && !unquote_c_style(&pattern, name, &states)) {
		name = pattern.buf;
		namelen = pattern.len;
	} else {
		namelen = strcspn(name, blank);
		states = name + namelen;
	}

	if (strlen(ATTRIBUTE_MACRO_PREFIX) < namelen &&
	    starts_with(name, ATTRIBUTE_MACRO_PREFIX)) {
		if (!(flags & READ_ATTR_MACRO_OK)) {
			fprintf_ln(stderr, _("%s not allowed: %s:%d"),
				   name, src, lineno);
			goto fail_return;
		}
		is_macro = 1;
		name += strlen(ATTRIBUTE_MACRO_PREFIX);
		name += strspn(name, blank);
		namelen = strcspn(name, blank);
		if (!attr_name_valid(name, namelen) || attr_name_reserved(name)) {
			report_invalid_attr(name, namelen, src, lineno);
			goto fail_return;
		}
	}
	else
		is_macro = 0;

	states += strspn(states, blank);

	/* First pass to count the attr_states */
	for (cp = states, num_attr = 0; *cp; num_attr++) {
		cp = parse_attr(src, lineno, cp, NULL);
		if (!cp)
			goto fail_return;
	}

	res = xcalloc(1, st_add3(sizeof(*res),
				 st_mult(sizeof(struct attr_state), num_attr),
				 is_macro ? 0 : namelen + 1));
	if (is_macro) {
		res->u.attr = git_attr_internal(name, namelen);
	} else {
		char *p = (char *)&(res->state[num_attr]);
		memcpy(p, name, namelen);
		res->u.pat.pattern = p;
		parse_path_pattern(&res->u.pat.pattern,
				      &res->u.pat.patternlen,
				      &res->u.pat.flags,
				      &res->u.pat.nowildcardlen);
		if (res->u.pat.flags & PATTERN_FLAG_NEGATIVE) {
			warning(_("Negative patterns are ignored in git attributes\n"
				  "Use '\\!' for literal leading exclamation."));
			goto fail_return;
		}
	}
	res->is_macro = is_macro;
	res->num_attr = num_attr;

	/* Second pass to fill the attr_states */
	for (cp = states, i = 0; *cp; i++) {
		cp = parse_attr(src, lineno, cp, &(res->state[i]));
	}

	strbuf_release(&pattern);
	return res;

fail_return:
	strbuf_release(&pattern);
	free(res);
	return NULL;
}

/*
 * Like info/exclude and .gitignore, the attribute information can
 * come from many places.
 *
 * (1) .gitattributes file of the same directory;
 * (2) .gitattributes file of the parent directory if (1) does not have
 *      any match; this goes recursively upwards, just like .gitignore.
 * (3) $GIT_DIR/info/attributes, which overrides both of the above.
 *
 * In the same file, later entries override the earlier match, so in the
 * global list, we would have entries from info/attributes the earliest
 * (reading the file from top to bottom), .gitattributes of the root
 * directory (again, reading the file from top to bottom) down to the
 * current directory, and then scan the list backwards to find the first match.
 * This is exactly the same as what is_excluded() does in dir.c to deal with
 * .gitignore file and info/excludes file as a fallback.
 */

struct attr_stack {
	struct attr_stack *prev;
	char *origin;
	size_t originlen;
	unsigned num_matches;
	unsigned alloc;
	struct match_attr **attrs;
};

static void attr_stack_free(struct attr_stack *e)
{
	unsigned i;
	free(e->origin);
	for (i = 0; i < e->num_matches; i++) {
		struct match_attr *a = e->attrs[i];
		size_t j;

		for (j = 0; j < a->num_attr; j++) {
			const char *setto = a->state[j].setto;
			if (setto == ATTR__TRUE ||
			    setto == ATTR__FALSE ||
			    setto == ATTR__UNSET ||
			    setto == ATTR__UNKNOWN)
				;
			else
				free((char *) setto);
		}
		free(a);
	}
	free(e->attrs);
	free(e);
}

static void drop_attr_stack(struct attr_stack **stack)
{
	while (*stack) {
		struct attr_stack *elem = *stack;
		*stack = elem->prev;
		attr_stack_free(elem);
	}
}

/* List of all attr_check structs; access should be surrounded by mutex */
static struct check_vector {
	size_t nr;
	size_t alloc;
	struct attr_check **checks;
	pthread_mutex_t mutex;
} check_vector;

static inline void vector_lock(void)
{
	pthread_mutex_lock(&check_vector.mutex);
}

static inline void vector_unlock(void)
{
	pthread_mutex_unlock(&check_vector.mutex);
}

static void check_vector_add(struct attr_check *c)
{
	vector_lock();

	ALLOC_GROW(check_vector.checks,
		   check_vector.nr + 1,
		   check_vector.alloc);
	check_vector.checks[check_vector.nr++] = c;

	vector_unlock();
}

static void check_vector_remove(struct attr_check *check)
{
	int i;

	vector_lock();

	/* Find entry */
	for (i = 0; i < check_vector.nr; i++)
		if (check_vector.checks[i] == check)
			break;

	if (i >= check_vector.nr)
		BUG("no entry found");

	/* shift entries over */
	for (; i < check_vector.nr - 1; i++)
		check_vector.checks[i] = check_vector.checks[i + 1];

	check_vector.nr--;

	vector_unlock();
}

/* Iterate through all attr_check instances and drop their stacks */
static void drop_all_attr_stacks(void)
{
	int i;

	vector_lock();

	for (i = 0; i < check_vector.nr; i++) {
		drop_attr_stack(&check_vector.checks[i]->stack);
	}

	vector_unlock();
}

struct attr_check *attr_check_alloc(void)
{
	struct attr_check *c = xcalloc(1, sizeof(struct attr_check));

	/* save pointer to the check struct */
	check_vector_add(c);

	return c;
}

struct attr_check *attr_check_initl(const char *one, ...)
{
	struct attr_check *check;
	int cnt;
	va_list params;
	const char *param;

	va_start(params, one);
	for (cnt = 1; (param = va_arg(params, const char *)) != NULL; cnt++)
		;
	va_end(params);

	check = attr_check_alloc();
	check->nr = cnt;
	check->alloc = cnt;
	CALLOC_ARRAY(check->items, cnt);

	check->items[0].attr = git_attr(one);
	va_start(params, one);
	for (cnt = 1; cnt < check->nr; cnt++) {
		const struct git_attr *attr;
		param = va_arg(params, const char *);
		if (!param)
			BUG("counted %d != ended at %d",
			    check->nr, cnt);
		attr = git_attr(param);
		if (!attr)
			BUG("%s: not a valid attribute name", param);
		check->items[cnt].attr = attr;
	}
	va_end(params);
	return check;
}

struct attr_check *attr_check_dup(const struct attr_check *check)
{
	struct attr_check *ret;

	if (!check)
		return NULL;

	ret = attr_check_alloc();

	ret->nr = check->nr;
	ret->alloc = check->alloc;
	DUP_ARRAY(ret->items, check->items, ret->nr);

	return ret;
}

struct attr_check_item *attr_check_append(struct attr_check *check,
					  const struct git_attr *attr)
{
	struct attr_check_item *item;

	ALLOC_GROW(check->items, check->nr + 1, check->alloc);
	item = &check->items[check->nr++];
	item->attr = attr;
	return item;
}

void attr_check_reset(struct attr_check *check)
{
	check->nr = 0;
}

void attr_check_clear(struct attr_check *check)
{
	FREE_AND_NULL(check->items);
	check->alloc = 0;
	check->nr = 0;

	FREE_AND_NULL(check->all_attrs);
	check->all_attrs_nr = 0;

	drop_attr_stack(&check->stack);
}

void attr_check_free(struct attr_check *check)
{
	if (check) {
		/* Remove check from the check vector */
		check_vector_remove(check);

		attr_check_clear(check);
		free(check);
	}
}

static const char *builtin_attr[] = {
	"[attr]binary -diff -merge -text",
	NULL,
};

static void handle_attr_line(struct attr_stack *res,
			     const char *line,
			     const char *src,
			     int lineno,
			     unsigned flags)
{
	struct match_attr *a;

	a = parse_attr_line(line, src, lineno, flags);
	if (!a)
		return;
	ALLOC_GROW_BY(res->attrs, res->num_matches, 1, res->alloc);
	res->attrs[res->num_matches - 1] = a;
}

static struct attr_stack *read_attr_from_array(const char **list)
{
	struct attr_stack *res;
	const char *line;
	int lineno = 0;

	CALLOC_ARRAY(res, 1);
	while ((line = *(list++)) != NULL)
		handle_attr_line(res, line, "[builtin]", ++lineno,
				 READ_ATTR_MACRO_OK);
	return res;
}

/*
 * Callers into the attribute system assume there is a single, system-wide
 * global state where attributes are read from and when the state is flipped by
 * calling git_attr_set_direction(), the stack frames that have been
 * constructed need to be discarded so that subsequent calls into the
 * attribute system will lazily read from the right place.  Since changing
 * direction causes a global paradigm shift, it should not ever be called while
 * another thread could potentially be calling into the attribute system.
 */
static enum git_attr_direction direction;

void git_attr_set_direction(enum git_attr_direction new_direction)
{
	if (repo_is_bare(the_repository) && new_direction != GIT_ATTR_INDEX)
		BUG("non-INDEX attr direction in a bare repo");

	if (new_direction != direction)
		drop_all_attr_stacks();

	direction = new_direction;
}

static struct attr_stack *read_attr_from_file(const char *path, unsigned flags)
{
	struct strbuf buf = STRBUF_INIT;
	int fd;
	FILE *fp;
	struct attr_stack *res;
	int lineno = 0;
	struct stat st;

	if (flags & READ_ATTR_NOFOLLOW)
		fd = open_nofollow(path, O_RDONLY);
	else
		fd = open(path, O_RDONLY);

	if (fd < 0) {
		warn_on_fopen_errors(path);
		return NULL;
	}
	fp = xfdopen(fd, "r");
	if (fstat(fd, &st)) {
		warning_errno(_("cannot fstat gitattributes file '%s'"), path);
		fclose(fp);
		return NULL;
	}
	if (st.st_size >= ATTR_MAX_FILE_SIZE) {
		warning(_("ignoring overly large gitattributes file '%s'"), path);
		fclose(fp);
		return NULL;
	}

	CALLOC_ARRAY(res, 1);
	while (strbuf_getline(&buf, fp) != EOF) {
		if (!lineno && starts_with(buf.buf, utf8_bom))
			strbuf_remove(&buf, 0, strlen(utf8_bom));
		handle_attr_line(res, buf.buf, path, ++lineno, flags);
	}

	fclose(fp);
	strbuf_release(&buf);
	return res;
}

static struct attr_stack *read_attr_from_buf(char *buf, size_t length,
					     const char *path, unsigned flags)
{
	struct attr_stack *res;
	char *sp;
	int lineno = 0;

	if (!buf)
		return NULL;
	if (length >= ATTR_MAX_FILE_SIZE) {
		warning(_("ignoring overly large gitattributes blob '%s'"), path);
		free(buf);
		return NULL;
	}

	CALLOC_ARRAY(res, 1);
	for (sp = buf; *sp;) {
		char *ep;
		int more;

		ep = strchrnul(sp, '\n');
		more = (*ep == '\n');
		*ep = '\0';
		handle_attr_line(res, sp, path, ++lineno, flags);
		sp = ep + more;
	}
	free(buf);

	return res;
}

static struct attr_stack *read_attr_from_blob(struct index_state *istate,
					      const struct object_id *tree_oid,
					      const char *path, unsigned flags)
{
	struct object_id oid;
	unsigned long sz;
	enum object_type type;
	void *buf;
	unsigned short mode;

	if (!tree_oid)
		return NULL;

	if (get_tree_entry(istate->repo, tree_oid, path, &oid, &mode))
		return NULL;

	buf = repo_read_object_file(istate->repo, &oid, &type, &sz);
	if (!buf || type != OBJ_BLOB) {
		free(buf);
		return NULL;
	}

	return read_attr_from_buf(buf, sz, path, flags);
}

static struct attr_stack *read_attr_from_index(struct index_state *istate,
					       const char *path, unsigned flags)
{
	struct attr_stack *stack = NULL;
	char *buf;
	unsigned long size;
	int sparse_dir_pos = -1;

	if (!istate)
		return NULL;

	/*
	 * When handling sparse-checkouts, .gitattributes files
	 * may reside within a sparse directory. We distinguish
	 * whether a path exists directly in the index or not by
	 * evaluating if 'pos' is negative.
	 * If 'pos' is negative, the path is not directly present
	 * in the index and is likely within a sparse directory.
	 * For paths not in the index, The absolute value of 'pos'
	 * minus 1 gives us the position where the path would be
	 * inserted in lexicographic order within the index.
	 * We then subtract another 1 from this value
	 * (sparse_dir_pos = -pos - 2) to find the position of the
	 * last index entry which is lexicographically smaller than
	 * the path. This would be the sparse directory containing
	 * the path. By identifying the sparse directory containing
	 * the path, we can correctly read the attributes specified
	 * in the .gitattributes file from the tree object of the
	 * sparse directory.
	 */
	if (!path_in_cone_mode_sparse_checkout(path, istate)) {
		int pos = index_name_pos_sparse(istate, path, strlen(path));

		if (pos < 0)
			sparse_dir_pos = -pos - 2;
	}

	if (sparse_dir_pos >= 0 &&
	    S_ISSPARSEDIR(istate->cache[sparse_dir_pos]->ce_mode) &&
	    !strncmp(istate->cache[sparse_dir_pos]->name, path, ce_namelen(istate->cache[sparse_dir_pos]))) {
		const char *relative_path = path + ce_namelen(istate->cache[sparse_dir_pos]);
		stack = read_attr_from_blob(istate, &istate->cache[sparse_dir_pos]->oid, relative_path, flags);
	} else {
		buf = read_blob_data_from_index(istate, path, &size);
		if (buf)
			stack = read_attr_from_buf(buf, size, path, flags);
	}
	return stack;
}

static struct attr_stack *read_attr(struct index_state *istate,
				    const struct object_id *tree_oid,
				    const char *path, unsigned flags)
{
	struct attr_stack *res = NULL;

	if (direction == GIT_ATTR_INDEX) {
		res = read_attr_from_index(istate, path, flags);
	} else if (tree_oid) {
		res = read_attr_from_blob(istate, tree_oid, path, flags);
	} else if (!repo_is_bare(the_repository)) {
		if (direction == GIT_ATTR_CHECKOUT) {
			res = read_attr_from_index(istate, path, flags);
			if (!res)
				res = read_attr_from_file(path, flags);
		} else if (direction == GIT_ATTR_CHECKIN) {
			res = read_attr_from_file(path, flags);
			if (!res)
				/*
				 * There is no checked out .gitattributes file
				 * there, but we might have it in the index.
				 * We allow operation in a sparsely checked out
				 * work tree, so read from it.
				 */
				res = read_attr_from_index(istate, path, flags);
		}
	}

	if (!res)
		CALLOC_ARRAY(res, 1);
	return res;
}

const char *git_attr_system_file(void)
{
	static const char *system_wide;
	if (!system_wide)
		system_wide = system_path(ETC_GITATTRIBUTES);
	return system_wide;
}

const char *git_attr_global_file(void)
{
	if (!git_attributes_file)
		git_attributes_file = xdg_config_home("attributes");

	return git_attributes_file;
}

int git_attr_system_is_enabled(void)
{
	return !git_env_bool("GIT_ATTR_NOSYSTEM", 0);
}

static GIT_PATH_FUNC(git_path_info_attributes, INFOATTRIBUTES_FILE)

static void push_stack(struct attr_stack **attr_stack_p,
		       struct attr_stack *elem, char *origin, size_t originlen)
{
	if (elem) {
		elem->origin = origin;
		if (origin)
			elem->originlen = originlen;
		elem->prev = *attr_stack_p;
		*attr_stack_p = elem;
	}
}

static void bootstrap_attr_stack(struct index_state *istate,
				 const struct object_id *tree_oid,
				 struct attr_stack **stack)
{
	struct attr_stack *e;
	unsigned flags = READ_ATTR_MACRO_OK;

	if (*stack)
		return;

	/* builtin frame */
	e = read_attr_from_array(builtin_attr);
	push_stack(stack, e, NULL, 0);

	/* system-wide frame */
	if (git_attr_system_is_enabled()) {
		e = read_attr_from_file(git_attr_system_file(), flags);
		push_stack(stack, e, NULL, 0);
	}

	/* home directory */
	if (git_attr_global_file()) {
		e = read_attr_from_file(git_attr_global_file(), flags);
		push_stack(stack, e, NULL, 0);
	}

	/* root directory */
	e = read_attr(istate, tree_oid, GITATTRIBUTES_FILE, flags | READ_ATTR_NOFOLLOW);
	push_stack(stack, e, xstrdup(""), 0);

	/* info frame */
	if (startup_info->have_repository)
		e = read_attr_from_file(git_path_info_attributes(), flags);
	else
		e = NULL;
	if (!e)
		CALLOC_ARRAY(e, 1);
	push_stack(stack, e, NULL, 0);
}

static void prepare_attr_stack(struct index_state *istate,
			       const struct object_id *tree_oid,
			       const char *path, int dirlen,
			       struct attr_stack **stack)
{
	struct attr_stack *info;
	struct strbuf pathbuf = STRBUF_INIT;

	/*
	 * At the bottom of the attribute stack is the built-in
	 * set of attribute definitions, followed by the contents
	 * of $(prefix)/etc/gitattributes and a file specified by
	 * core.attributesfile.  Then, contents from
	 * .gitattributes files from directories closer to the
	 * root to the ones in deeper directories are pushed
	 * to the stack.  Finally, at the very top of the stack
	 * we always keep the contents of $GIT_DIR/info/attributes.
	 *
	 * When checking, we use entries from near the top of the
	 * stack, preferring $GIT_DIR/info/attributes, then
	 * .gitattributes in deeper directories to shallower ones,
	 * and finally use the built-in set as the default.
	 */
	bootstrap_attr_stack(istate, tree_oid, stack);

	/*
	 * Pop the "info" one that is always at the top of the stack.
	 */
	info = *stack;
	*stack = info->prev;

	/*
	 * Pop the ones from directories that are not the prefix of
	 * the path we are checking. Break out of the loop when we see
	 * the root one (whose origin is an empty string "") or the builtin
	 * one (whose origin is NULL) without popping it.
	 */
	while ((*stack)->origin) {
		int namelen = (*stack)->originlen;
		struct attr_stack *elem;

		elem = *stack;
		if (namelen <= dirlen &&
		    !strncmp(elem->origin, path, namelen) &&
		    (!namelen || path[namelen] == '/'))
			break;

		*stack = elem->prev;
		attr_stack_free(elem);
	}

	/*
	 * bootstrap_attr_stack() should have added, and the
	 * above loop should have stopped before popping, the
	 * root element whose attr_stack->origin is set to an
	 * empty string.
	 */
	assert((*stack)->origin);

	strbuf_addstr(&pathbuf, (*stack)->origin);
	/* Build up to the directory 'path' is in */
	while (pathbuf.len < dirlen) {
		size_t len = pathbuf.len;
		struct attr_stack *next;
		char *origin;

		/* Skip path-separator */
		if (len < dirlen && is_dir_sep(path[len]))
			len++;
		/* Find the end of the next component */
		while (len < dirlen && !is_dir_sep(path[len]))
			len++;

		if (pathbuf.len > 0)
			strbuf_addch(&pathbuf, '/');
		strbuf_add(&pathbuf, path + pathbuf.len, (len - pathbuf.len));
		strbuf_addf(&pathbuf, "/%s", GITATTRIBUTES_FILE);

		next = read_attr(istate, tree_oid, pathbuf.buf, READ_ATTR_NOFOLLOW);

		/* reset the pathbuf to not include "/.gitattributes" */
		strbuf_setlen(&pathbuf, len);

		origin = xstrdup(pathbuf.buf);
		push_stack(stack, next, origin, len);
	}

	/*
	 * Finally push the "info" one at the top of the stack.
	 */
	push_stack(stack, info, NULL, 0);

	strbuf_release(&pathbuf);
}

static int path_matches(const char *pathname, int pathlen,
			int basename_offset,
			const struct pattern *pat,
			const char *base, int baselen)
{
	const char *pattern = pat->pattern;
	int prefix = pat->nowildcardlen;
	int isdir = (pathlen && pathname[pathlen - 1] == '/');

	if ((pat->flags & PATTERN_FLAG_MUSTBEDIR) && !isdir)
		return 0;

	if (pat->flags & PATTERN_FLAG_NODIR) {
		return match_basename(pathname + basename_offset,
				      pathlen - basename_offset - isdir,
				      pattern, prefix,
				      pat->patternlen, pat->flags);
	}
	return match_pathname(pathname, pathlen - isdir,
			      base, baselen,
			      pattern, prefix, pat->patternlen);
}

static int macroexpand_one(struct all_attrs_item *all_attrs, int nr, int rem);

static int fill_one(struct all_attrs_item *all_attrs,
		    const struct match_attr *a, int rem)
{
	size_t i;

	for (i = a->num_attr; rem > 0 && i > 0; i--) {
		const struct git_attr *attr = a->state[i - 1].attr;
		const char **n = &(all_attrs[attr->attr_nr].value);
		const char *v = a->state[i - 1].setto;

		if (*n == ATTR__UNKNOWN) {
			*n = v;
			rem--;
			rem = macroexpand_one(all_attrs, attr->attr_nr, rem);
		}
	}
	return rem;
}

static int fill(const char *path, int pathlen, int basename_offset,
		const struct attr_stack *stack,
		struct all_attrs_item *all_attrs, int rem)
{
	for (; rem > 0 && stack; stack = stack->prev) {
		unsigned i;
		const char *base = stack->origin ? stack->origin : "";

		for (i = stack->num_matches; 0 < rem && 0 < i; i--) {
			const struct match_attr *a = stack->attrs[i - 1];
			if (a->is_macro)
				continue;
			if (path_matches(path, pathlen, basename_offset,
					 &a->u.pat, base, stack->originlen))
				rem = fill_one(all_attrs, a, rem);
		}
	}

	return rem;
}

static int macroexpand_one(struct all_attrs_item *all_attrs, int nr, int rem)
{
	const struct all_attrs_item *item = &all_attrs[nr];

	if (item->macro && item->value == ATTR__TRUE)
		return fill_one(all_attrs, item->macro, rem);
	else
		return rem;
}

/*
 * Marks the attributes which are macros based on the attribute stack.
 * This prevents having to search through the attribute stack each time
 * a macro needs to be expanded during the fill stage.
 */
static void determine_macros(struct all_attrs_item *all_attrs,
			     const struct attr_stack *stack)
{
	for (; stack; stack = stack->prev) {
		unsigned i;
		for (i = stack->num_matches; i > 0; i--) {
			const struct match_attr *ma = stack->attrs[i - 1];
			if (ma->is_macro) {
				unsigned int n = ma->u.attr->attr_nr;
				if (!all_attrs[n].macro) {
					all_attrs[n].macro = ma;
				}
			}
		}
	}
}

/*
 * Collect attributes for path into the array pointed to by check->all_attrs.
 * If check->check_nr is non-zero, only attributes in check[] are collected.
 * Otherwise all attributes are collected.
 */
static void collect_some_attrs(struct index_state *istate,
			       const struct object_id *tree_oid,
			       const char *path, struct attr_check *check)
{
	int pathlen, rem, dirlen;
	const char *cp, *last_slash = NULL;
	int basename_offset;

	for (cp = path; *cp; cp++) {
		if (*cp == '/' && cp[1])
			last_slash = cp;
	}
	pathlen = cp - path;
	if (last_slash) {
		basename_offset = last_slash + 1 - path;
		dirlen = last_slash - path;
	} else {
		basename_offset = 0;
		dirlen = 0;
	}

	prepare_attr_stack(istate, tree_oid, path, dirlen, &check->stack);
	all_attrs_init(&g_attr_hashmap, check);
	determine_macros(check->all_attrs, check->stack);

	rem = check->all_attrs_nr;
	fill(path, pathlen, basename_offset, check->stack, check->all_attrs, rem);
}

static const char *default_attr_source_tree_object_name;

void set_git_attr_source(const char *tree_object_name)
{
	default_attr_source_tree_object_name = xstrdup(tree_object_name);
}

static int compute_default_attr_source(struct object_id *attr_source)
{
	int ignore_bad_attr_tree = 0;

	if (!default_attr_source_tree_object_name)
		default_attr_source_tree_object_name = getenv(GIT_ATTR_SOURCE_ENVIRONMENT);

	if (!default_attr_source_tree_object_name && git_attr_tree) {
		default_attr_source_tree_object_name = git_attr_tree;
		ignore_bad_attr_tree = 1;
	}

	if (!default_attr_source_tree_object_name)
		return 0;

	if (!startup_info->have_repository) {
		if (!ignore_bad_attr_tree)
			die(_("cannot use --attr-source or GIT_ATTR_SOURCE without repo"));
		return 0;
	}

	if (repo_get_oid_treeish(the_repository,
				 default_attr_source_tree_object_name,
				 attr_source)) {
		if (!ignore_bad_attr_tree)
			die(_("bad --attr-source or GIT_ATTR_SOURCE"));
		return 0;
	}

	return 1;
}

static struct object_id *default_attr_source(void)
{
	static struct object_id attr_source;
	static int has_attr_source = -1;

	if (has_attr_source < 0)
		has_attr_source = compute_default_attr_source(&attr_source);
	if (!has_attr_source)
		return NULL;
	return &attr_source;
}

static const char *interned_mode_string(unsigned int mode)
{
	static struct {
		unsigned int val;
		char str[7];
	} mode_string[] = {
		{ .val = 0040000 },
		{ .val = 0100644 },
		{ .val = 0100755 },
		{ .val = 0120000 },
		{ .val = 0160000 },
	};
	int i;

	for (i = 0; i < ARRAY_SIZE(mode_string); i++) {
		if (mode_string[i].val != mode)
			continue;
		if (!*mode_string[i].str)
			snprintf(mode_string[i].str, sizeof(mode_string[i].str),
				 "%06o", mode);
		return mode_string[i].str;
	}
	BUG("Unsupported mode 0%o", mode);
}

static const char *builtin_object_mode_attr(struct index_state *istate, const char *path)
{
	unsigned int mode;

	if (direction == GIT_ATTR_CHECKIN) {
		struct object_id oid;
		struct stat st;
		if (lstat(path, &st))
			die_errno(_("unable to stat '%s'"), path);
		mode = canon_mode(st.st_mode);
		if (S_ISDIR(mode)) {
			/*
			 *`path` is either a directory or it is a submodule,
			 * in which case it is already indexed as submodule
			 * or it does not exist in the index yet and we need to
			 * check if we can resolve to a ref.
			*/
			int pos = index_name_pos(istate, path, strlen(path));
			if (pos >= 0) {
				 if (S_ISGITLINK(istate->cache[pos]->ce_mode))
					 mode = istate->cache[pos]->ce_mode;
			} else if (repo_resolve_gitlink_ref(the_repository, path,
							    "HEAD", &oid) == 0) {
				mode = S_IFGITLINK;
			}
		}
	} else {
		/*
		 * For GIT_ATTR_CHECKOUT and GIT_ATTR_INDEX we only check
		 * for mode in the index.
		 */
		int pos = index_name_pos(istate, path, strlen(path));
		if (pos >= 0)
			mode = istate->cache[pos]->ce_mode;
		else
			return ATTR__UNSET;
	}

	return interned_mode_string(mode);
}


static const char *compute_builtin_attr(struct index_state *istate,
					  const char *path,
					  const struct git_attr *attr) {
	static const struct git_attr *object_mode_attr;

	if (!object_mode_attr)
		object_mode_attr = git_attr("builtin_objectmode");

	if (attr == object_mode_attr)
		return builtin_object_mode_attr(istate, path);
	return ATTR__UNSET;
}

void git_check_attr(struct index_state *istate,
		    const char *path,
		    struct attr_check *check)
{
	int i;
	const struct object_id *tree_oid = default_attr_source();

	collect_some_attrs(istate, tree_oid, path, check);

	for (i = 0; i < check->nr; i++) {
		unsigned int n = check->items[i].attr->attr_nr;
		const char *value = check->all_attrs[n].value;
		if (value == ATTR__UNKNOWN)
			value = compute_builtin_attr(istate, path, check->all_attrs[n].attr);
		check->items[i].value = value;
	}
}

void git_all_attrs(struct index_state *istate,
		   const char *path, struct attr_check *check)
{
	int i;
	const struct object_id *tree_oid = default_attr_source();

	attr_check_reset(check);
	collect_some_attrs(istate, tree_oid, path, check);

	for (i = 0; i < check->all_attrs_nr; i++) {
		const char *name = check->all_attrs[i].attr->name;
		const char *value = check->all_attrs[i].value;
		struct attr_check_item *item;
		if (value == ATTR__UNSET || value == ATTR__UNKNOWN)
			continue;
		item = attr_check_append(check, git_attr(name));
		item->value = value;
	}
}

void attr_start(void)
{
	pthread_mutex_init(&g_attr_hashmap.mutex, NULL);
	pthread_mutex_init(&check_vector.mutex, NULL);
}
