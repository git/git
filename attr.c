/*
 * Handle git attributes.  See gitattributes(5) for a description of
 * the file syntax, and Documentation/technical/api-gitattributes.txt
 * for a description of the API.
 *
 * One basic design decision here is that we are not going to support
 * an insanely large number of attributes.
 */

#define NO_THE_INDEX_COMPATIBILITY_MACROS
#include "cache.h"
#include "exec_cmd.h"
#include "attr.h"
#include "dir.h"

const char git_attr__true[] = "(builtin)true";
const char git_attr__false[] = "\0(builtin)false";
static const char git_attr__unknown[] = "(builtin)unknown";
#define ATTR__TRUE git_attr__true
#define ATTR__FALSE git_attr__false
#define ATTR__UNSET NULL
#define ATTR__UNKNOWN git_attr__unknown

/* This is a randomly chosen prime. */
#define HASHSIZE 257

#ifndef DEBUG_ATTR
#define DEBUG_ATTR 0
#endif

struct git_attr {
	struct git_attr *next;
	unsigned h;
	int attr_nr;
	char name[FLEX_ARRAY];
};
static int attr_nr;

static struct git_attr_check *check_all_attr;
static struct git_attr *(git_attr_hash[HASHSIZE]);

char *git_attr_name(struct git_attr *attr)
{
	return attr->name;
}

static unsigned hash_name(const char *name, int namelen)
{
	unsigned val = 0, c;

	while (namelen--) {
		c = *name++;
		val = ((val << 7) | (val >> 22)) ^ c;
	}
	return val;
}

static int invalid_attr_name(const char *name, int namelen)
{
	/*
	 * Attribute name cannot begin with '-' and must consist of
	 * characters from [-A-Za-z0-9_.].
	 */
	if (namelen <= 0 || *name == '-')
		return -1;
	while (namelen--) {
		char ch = *name++;
		if (! (ch == '-' || ch == '.' || ch == '_' ||
		       ('0' <= ch && ch <= '9') ||
		       ('a' <= ch && ch <= 'z') ||
		       ('A' <= ch && ch <= 'Z')) )
			return -1;
	}
	return 0;
}

static struct git_attr *git_attr_internal(const char *name, int len)
{
	unsigned hval = hash_name(name, len);
	unsigned pos = hval % HASHSIZE;
	struct git_attr *a;

	for (a = git_attr_hash[pos]; a; a = a->next) {
		if (a->h == hval &&
		    !memcmp(a->name, name, len) && !a->name[len])
			return a;
	}

	if (invalid_attr_name(name, len))
		return NULL;

	a = xmalloc(sizeof(*a) + len + 1);
	memcpy(a->name, name, len);
	a->name[len] = 0;
	a->h = hval;
	a->next = git_attr_hash[pos];
	a->attr_nr = attr_nr++;
	git_attr_hash[pos] = a;

	check_all_attr = xrealloc(check_all_attr,
				  sizeof(*check_all_attr) * attr_nr);
	check_all_attr[a->attr_nr].attr = a;
	check_all_attr[a->attr_nr].value = ATTR__UNKNOWN;
	return a;
}

struct git_attr *git_attr(const char *name)
{
	return git_attr_internal(name, strlen(name));
}

/* What does a matched pattern decide? */
struct attr_state {
	struct git_attr *attr;
	const char *setto;
};

/*
 * One rule, as from a .gitattributes file.
 *
 * If is_macro is true, then u.attr is a pointer to the git_attr being
 * defined.
 *
 * If is_macro is false, then u.pattern points at the filename pattern
 * to which the rule applies.  (The memory pointed to is part of the
 * memory block allocated for the match_attr instance.)
 *
 * In either case, num_attr is the number of attributes affected by
 * this rule, and state is an array listing them.  The attributes are
 * listed as they appear in the file (macros unexpanded).
 */
struct match_attr {
	union {
		char *pattern;
		struct git_attr *attr;
	} u;
	char is_macro;
	unsigned num_attr;
	struct attr_state state[FLEX_ARRAY];
};

static const char blank[] = " \t\r\n";

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
	int len;

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
		if (invalid_attr_name(cp, len)) {
			fprintf(stderr,
				"%.*s is not a valid attribute name: %s:%d\n",
				len, cp, src, lineno);
			return NULL;
		}
	} else {
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
					  int lineno, int macro_ok)
{
	int namelen;
	int num_attr, i;
	const char *cp, *name, *states;
	struct match_attr *res = NULL;
	int is_macro;

	cp = line + strspn(line, blank);
	if (!*cp || *cp == '#')
		return NULL;
	name = cp;
	namelen = strcspn(name, blank);
	if (strlen(ATTRIBUTE_MACRO_PREFIX) < namelen &&
	    !prefixcmp(name, ATTRIBUTE_MACRO_PREFIX)) {
		if (!macro_ok) {
			fprintf(stderr, "%s not allowed: %s:%d\n",
				name, src, lineno);
			return NULL;
		}
		is_macro = 1;
		name += strlen(ATTRIBUTE_MACRO_PREFIX);
		name += strspn(name, blank);
		namelen = strcspn(name, blank);
		if (invalid_attr_name(name, namelen)) {
			fprintf(stderr,
				"%.*s is not a valid attribute name: %s:%d\n",
				namelen, name, src, lineno);
			return NULL;
		}
	}
	else
		is_macro = 0;

	states = name + namelen;
	states += strspn(states, blank);

	/* First pass to count the attr_states */
	for (cp = states, num_attr = 0; *cp; num_attr++) {
		cp = parse_attr(src, lineno, cp, NULL);
		if (!cp)
			return NULL;
	}

	res = xcalloc(1,
		      sizeof(*res) +
		      sizeof(struct attr_state) * num_attr +
		      (is_macro ? 0 : namelen + 1));
	if (is_macro)
		res->u.attr = git_attr_internal(name, namelen);
	else {
		res->u.pattern = (char *)&(res->state[num_attr]);
		memcpy(res->u.pattern, name, namelen);
		res->u.pattern[namelen] = 0;
	}
	res->is_macro = is_macro;
	res->num_attr = num_attr;

	/* Second pass to fill the attr_states */
	for (cp = states, i = 0; *cp; i++) {
		cp = parse_attr(src, lineno, cp, &(res->state[i]));
	}

	return res;
}

/*
 * Like info/exclude and .gitignore, the attribute information can
 * come from many places.
 *
 * (1) .gitattribute file of the same directory;
 * (2) .gitattribute file of the parent directory if (1) does not have
 *      any match; this goes recursively upwards, just like .gitignore.
 * (3) $GIT_DIR/info/attributes, which overrides both of the above.
 *
 * In the same file, later entries override the earlier match, so in the
 * global list, we would have entries from info/attributes the earliest
 * (reading the file from top to bottom), .gitattribute of the root
 * directory (again, reading the file from top to bottom) down to the
 * current directory, and then scan the list backwards to find the first match.
 * This is exactly the same as what excluded() does in dir.c to deal with
 * .gitignore
 */

static struct attr_stack {
	struct attr_stack *prev;
	char *origin;
	unsigned num_matches;
	unsigned alloc;
	struct match_attr **attrs;
} *attr_stack;

static void free_attr_elem(struct attr_stack *e)
{
	int i;
	free(e->origin);
	for (i = 0; i < e->num_matches; i++) {
		struct match_attr *a = e->attrs[i];
		int j;
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
	free(e);
}

static const char *builtin_attr[] = {
	"[attr]binary -diff -text",
	NULL,
};

static void handle_attr_line(struct attr_stack *res,
			     const char *line,
			     const char *src,
			     int lineno,
			     int macro_ok)
{
	struct match_attr *a;

	a = parse_attr_line(line, src, lineno, macro_ok);
	if (!a)
		return;
	if (res->alloc <= res->num_matches) {
		res->alloc = alloc_nr(res->num_matches);
		res->attrs = xrealloc(res->attrs,
				      sizeof(struct match_attr *) *
				      res->alloc);
	}
	res->attrs[res->num_matches++] = a;
}

static struct attr_stack *read_attr_from_array(const char **list)
{
	struct attr_stack *res;
	const char *line;
	int lineno = 0;

	res = xcalloc(1, sizeof(*res));
	while ((line = *(list++)) != NULL)
		handle_attr_line(res, line, "[builtin]", ++lineno, 1);
	return res;
}

static enum git_attr_direction direction;
static struct index_state *use_index;

static struct attr_stack *read_attr_from_file(const char *path, int macro_ok)
{
	FILE *fp = fopen(path, "r");
	struct attr_stack *res;
	char buf[2048];
	int lineno = 0;

	if (!fp)
		return NULL;
	res = xcalloc(1, sizeof(*res));
	while (fgets(buf, sizeof(buf), fp))
		handle_attr_line(res, buf, path, ++lineno, macro_ok);
	fclose(fp);
	return res;
}

static void *read_index_data(const char *path)
{
	int pos, len;
	unsigned long sz;
	enum object_type type;
	void *data;
	struct index_state *istate = use_index ? use_index : &the_index;

	len = strlen(path);
	pos = index_name_pos(istate, path, len);
	if (pos < 0) {
		/*
		 * We might be in the middle of a merge, in which
		 * case we would read stage #2 (ours).
		 */
		int i;
		for (i = -pos - 1;
		     (pos < 0 && i < istate->cache_nr &&
		      !strcmp(istate->cache[i]->name, path));
		     i++)
			if (ce_stage(istate->cache[i]) == 2)
				pos = i;
	}
	if (pos < 0)
		return NULL;
	data = read_sha1_file(istate->cache[pos]->sha1, &type, &sz);
	if (!data || type != OBJ_BLOB) {
		free(data);
		return NULL;
	}
	return data;
}

static struct attr_stack *read_attr_from_index(const char *path, int macro_ok)
{
	struct attr_stack *res;
	char *buf, *sp;
	int lineno = 0;

	buf = read_index_data(path);
	if (!buf)
		return NULL;

	res = xcalloc(1, sizeof(*res));
	for (sp = buf; *sp; ) {
		char *ep;
		int more;
		for (ep = sp; *ep && *ep != '\n'; ep++)
			;
		more = (*ep == '\n');
		*ep = '\0';
		handle_attr_line(res, sp, path, ++lineno, macro_ok);
		sp = ep + more;
	}
	free(buf);
	return res;
}

static struct attr_stack *read_attr(const char *path, int macro_ok)
{
	struct attr_stack *res;

	if (direction == GIT_ATTR_CHECKOUT) {
		res = read_attr_from_index(path, macro_ok);
		if (!res)
			res = read_attr_from_file(path, macro_ok);
	}
	else if (direction == GIT_ATTR_CHECKIN) {
		res = read_attr_from_file(path, macro_ok);
		if (!res)
			/*
			 * There is no checked out .gitattributes file there, but
			 * we might have it in the index.  We allow operation in a
			 * sparsely checked out work tree, so read from it.
			 */
			res = read_attr_from_index(path, macro_ok);
	}
	else
		res = read_attr_from_index(path, macro_ok);
	if (!res)
		res = xcalloc(1, sizeof(*res));
	return res;
}

#if DEBUG_ATTR
static void debug_info(const char *what, struct attr_stack *elem)
{
	fprintf(stderr, "%s: %s\n", what, elem->origin ? elem->origin : "()");
}
static void debug_set(const char *what, const char *match, struct git_attr *attr, const void *v)
{
	const char *value = v;

	if (ATTR_TRUE(value))
		value = "set";
	else if (ATTR_FALSE(value))
		value = "unset";
	else if (ATTR_UNSET(value))
		value = "unspecified";

	fprintf(stderr, "%s: %s => %s (%s)\n",
		what, attr->name, (char *) value, match);
}
#define debug_push(a) debug_info("push", (a))
#define debug_pop(a) debug_info("pop", (a))
#else
#define debug_push(a) do { ; } while (0)
#define debug_pop(a) do { ; } while (0)
#define debug_set(a,b,c,d) do { ; } while (0)
#endif

static void drop_attr_stack(void)
{
	while (attr_stack) {
		struct attr_stack *elem = attr_stack;
		attr_stack = elem->prev;
		free_attr_elem(elem);
	}
}

static const char *git_etc_gitattributes(void)
{
	static const char *system_wide;
	if (!system_wide)
		system_wide = system_path(ETC_GITATTRIBUTES);
	return system_wide;
}

static int git_attr_system(void)
{
	return !git_env_bool("GIT_ATTR_NOSYSTEM", 0);
}

static void bootstrap_attr_stack(void)
{
	if (!attr_stack) {
		struct attr_stack *elem;

		elem = read_attr_from_array(builtin_attr);
		elem->origin = NULL;
		elem->prev = attr_stack;
		attr_stack = elem;

		if (git_attr_system()) {
			elem = read_attr_from_file(git_etc_gitattributes(), 1);
			if (elem) {
				elem->origin = NULL;
				elem->prev = attr_stack;
				attr_stack = elem;
			}
		}

		if (git_attributes_file) {
			elem = read_attr_from_file(git_attributes_file, 1);
			if (elem) {
				elem->origin = NULL;
				elem->prev = attr_stack;
				attr_stack = elem;
			}
		}

		if (!is_bare_repository() || direction == GIT_ATTR_INDEX) {
			elem = read_attr(GITATTRIBUTES_FILE, 1);
			elem->origin = xstrdup("");
			elem->prev = attr_stack;
			attr_stack = elem;
			debug_push(elem);
		}

		elem = read_attr_from_file(git_path(INFOATTRIBUTES_FILE), 1);
		if (!elem)
			elem = xcalloc(1, sizeof(*elem));
		elem->origin = NULL;
		elem->prev = attr_stack;
		attr_stack = elem;
	}
}

static void prepare_attr_stack(const char *path)
{
	struct attr_stack *elem, *info;
	int dirlen, len;
	const char *cp;

	cp = strrchr(path, '/');
	if (!cp)
		dirlen = 0;
	else
		dirlen = cp - path;

	/*
	 * At the bottom of the attribute stack is the built-in
	 * set of attribute definitions, followed by the contents
	 * of $(prefix)/etc/gitattributes and a file specified by
	 * core.attributesfile.  Then, contents from
	 * .gitattribute files from directories closer to the
	 * root to the ones in deeper directories are pushed
	 * to the stack.  Finally, at the very top of the stack
	 * we always keep the contents of $GIT_DIR/info/attributes.
	 *
	 * When checking, we use entries from near the top of the
	 * stack, preferring $GIT_DIR/info/attributes, then
	 * .gitattributes in deeper directories to shallower ones,
	 * and finally use the built-in set as the default.
	 */
	bootstrap_attr_stack();

	/*
	 * Pop the "info" one that is always at the top of the stack.
	 */
	info = attr_stack;
	attr_stack = info->prev;

	/*
	 * Pop the ones from directories that are not the prefix of
	 * the path we are checking.
	 */
	while (attr_stack && attr_stack->origin) {
		int namelen = strlen(attr_stack->origin);

		elem = attr_stack;
		if (namelen <= dirlen &&
		    !strncmp(elem->origin, path, namelen))
			break;

		debug_pop(elem);
		attr_stack = elem->prev;
		free_attr_elem(elem);
	}

	/*
	 * Read from parent directories and push them down
	 */
	if (!is_bare_repository() || direction == GIT_ATTR_INDEX) {
		struct strbuf pathbuf = STRBUF_INIT;

		while (1) {
			len = strlen(attr_stack->origin);
			if (dirlen <= len)
				break;
			cp = memchr(path + len + 1, '/', dirlen - len - 1);
			if (!cp)
				cp = path + dirlen;
			strbuf_add(&pathbuf, path, cp - path);
			strbuf_addch(&pathbuf, '/');
			strbuf_addstr(&pathbuf, GITATTRIBUTES_FILE);
			elem = read_attr(pathbuf.buf, 0);
			strbuf_setlen(&pathbuf, cp - path);
			elem->origin = strbuf_detach(&pathbuf, NULL);
			elem->prev = attr_stack;
			attr_stack = elem;
			debug_push(elem);
		}

		strbuf_release(&pathbuf);
	}

	/*
	 * Finally push the "info" one at the top of the stack.
	 */
	info->prev = attr_stack;
	attr_stack = info;
}

static int path_matches(const char *pathname, int pathlen,
			const char *pattern,
			const char *base, int baselen)
{
	if (!strchr(pattern, '/')) {
		/* match basename */
		const char *basename = strrchr(pathname, '/');
		basename = basename ? basename + 1 : pathname;
		return (fnmatch_icase(pattern, basename, 0) == 0);
	}
	/*
	 * match with FNM_PATHNAME; the pattern has base implicitly
	 * in front of it.
	 */
	if (*pattern == '/')
		pattern++;
	if (pathlen < baselen ||
	    (baselen && pathname[baselen] != '/') ||
	    strncmp(pathname, base, baselen))
		return 0;
	if (baselen != 0)
		baselen++;
	return fnmatch_icase(pattern, pathname + baselen, FNM_PATHNAME) == 0;
}

static int macroexpand_one(int attr_nr, int rem);

static int fill_one(const char *what, struct match_attr *a, int rem)
{
	struct git_attr_check *check = check_all_attr;
	int i;

	for (i = a->num_attr - 1; 0 < rem && 0 <= i; i--) {
		struct git_attr *attr = a->state[i].attr;
		const char **n = &(check[attr->attr_nr].value);
		const char *v = a->state[i].setto;

		if (*n == ATTR__UNKNOWN) {
			debug_set(what,
				  a->is_macro ? a->u.attr->name : a->u.pattern,
				  attr, v);
			*n = v;
			rem--;
			rem = macroexpand_one(attr->attr_nr, rem);
		}
	}
	return rem;
}

static int fill(const char *path, int pathlen, struct attr_stack *stk, int rem)
{
	int i;
	const char *base = stk->origin ? stk->origin : "";

	for (i = stk->num_matches - 1; 0 < rem && 0 <= i; i--) {
		struct match_attr *a = stk->attrs[i];
		if (a->is_macro)
			continue;
		if (path_matches(path, pathlen,
				 a->u.pattern, base, strlen(base)))
			rem = fill_one("fill", a, rem);
	}
	return rem;
}

static int macroexpand_one(int attr_nr, int rem)
{
	struct attr_stack *stk;
	struct match_attr *a = NULL;
	int i;

	if (check_all_attr[attr_nr].value != ATTR__TRUE)
		return rem;

	for (stk = attr_stack; !a && stk; stk = stk->prev)
		for (i = stk->num_matches - 1; !a && 0 <= i; i--) {
			struct match_attr *ma = stk->attrs[i];
			if (!ma->is_macro)
				continue;
			if (ma->u.attr->attr_nr == attr_nr)
				a = ma;
		}

	if (a)
		rem = fill_one("expand", a, rem);

	return rem;
}

/*
 * Collect all attributes for path into the array pointed to by
 * check_all_attr.
 */
static void collect_all_attrs(const char *path)
{
	struct attr_stack *stk;
	int i, pathlen, rem;

	prepare_attr_stack(path);
	for (i = 0; i < attr_nr; i++)
		check_all_attr[i].value = ATTR__UNKNOWN;

	pathlen = strlen(path);
	rem = attr_nr;
	for (stk = attr_stack; 0 < rem && stk; stk = stk->prev)
		rem = fill(path, pathlen, stk, rem);
}

int git_check_attr(const char *path, int num, struct git_attr_check *check)
{
	int i;

	collect_all_attrs(path);

	for (i = 0; i < num; i++) {
		const char *value = check_all_attr[check[i].attr->attr_nr].value;
		if (value == ATTR__UNKNOWN)
			value = ATTR__UNSET;
		check[i].value = value;
	}

	return 0;
}

int git_all_attrs(const char *path, int *num, struct git_attr_check **check)
{
	int i, count, j;

	collect_all_attrs(path);

	/* Count the number of attributes that are set. */
	count = 0;
	for (i = 0; i < attr_nr; i++) {
		const char *value = check_all_attr[i].value;
		if (value != ATTR__UNSET && value != ATTR__UNKNOWN)
			++count;
	}
	*num = count;
	*check = xmalloc(sizeof(**check) * count);
	j = 0;
	for (i = 0; i < attr_nr; i++) {
		const char *value = check_all_attr[i].value;
		if (value != ATTR__UNSET && value != ATTR__UNKNOWN) {
			(*check)[j].attr = check_all_attr[i].attr;
			(*check)[j].value = value;
			++j;
		}
	}

	return 0;
}

void git_attr_set_direction(enum git_attr_direction new, struct index_state *istate)
{
	enum git_attr_direction old = direction;

	if (is_bare_repository() && new != GIT_ATTR_INDEX)
		die("BUG: non-INDEX attr direction in a bare repo");

	direction = new;
	if (new != old)
		drop_attr_stack();
	use_index = istate;
}
