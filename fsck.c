#include "cache.h"
#include "object-store.h"
#include "repository.h"
#include "object.h"
#include "blob.h"
#include "tree.h"
#include "tree-walk.h"
#include "commit.h"
#include "tag.h"
#include "fsck.h"
#include "refs.h"
#include "url.h"
#include "utf8.h"
#include "decorate.h"
#include "oidset.h"
#include "packfile.h"
#include "submodule-config.h"
#include "config.h"
#include "credential.h"
#include "help.h"

static struct oidset gitmodules_found = OIDSET_INIT;
static struct oidset gitmodules_done = OIDSET_INIT;

#define FSCK_FATAL -1
#define FSCK_INFO -2

#define FOREACH_MSG_ID(FUNC) \
	/* fatal errors */ \
	FUNC(NUL_IN_HEADER, FATAL) \
	FUNC(UNTERMINATED_HEADER, FATAL) \
	/* errors */ \
	FUNC(BAD_DATE, ERROR) \
	FUNC(BAD_DATE_OVERFLOW, ERROR) \
	FUNC(BAD_EMAIL, ERROR) \
	FUNC(BAD_NAME, ERROR) \
	FUNC(BAD_OBJECT_SHA1, ERROR) \
	FUNC(BAD_PARENT_SHA1, ERROR) \
	FUNC(BAD_TAG_OBJECT, ERROR) \
	FUNC(BAD_TIMEZONE, ERROR) \
	FUNC(BAD_TREE, ERROR) \
	FUNC(BAD_TREE_SHA1, ERROR) \
	FUNC(BAD_TYPE, ERROR) \
	FUNC(DUPLICATE_ENTRIES, ERROR) \
	FUNC(MISSING_AUTHOR, ERROR) \
	FUNC(MISSING_COMMITTER, ERROR) \
	FUNC(MISSING_EMAIL, ERROR) \
	FUNC(MISSING_NAME_BEFORE_EMAIL, ERROR) \
	FUNC(MISSING_OBJECT, ERROR) \
	FUNC(MISSING_SPACE_BEFORE_DATE, ERROR) \
	FUNC(MISSING_SPACE_BEFORE_EMAIL, ERROR) \
	FUNC(MISSING_TAG, ERROR) \
	FUNC(MISSING_TAG_ENTRY, ERROR) \
	FUNC(MISSING_TREE, ERROR) \
	FUNC(MISSING_TREE_OBJECT, ERROR) \
	FUNC(MISSING_TYPE, ERROR) \
	FUNC(MISSING_TYPE_ENTRY, ERROR) \
	FUNC(MULTIPLE_AUTHORS, ERROR) \
	FUNC(TREE_NOT_SORTED, ERROR) \
	FUNC(UNKNOWN_TYPE, ERROR) \
	FUNC(ZERO_PADDED_DATE, ERROR) \
	FUNC(GITMODULES_MISSING, ERROR) \
	FUNC(GITMODULES_BLOB, ERROR) \
	FUNC(GITMODULES_LARGE, ERROR) \
	FUNC(GITMODULES_NAME, ERROR) \
	FUNC(GITMODULES_SYMLINK, ERROR) \
	FUNC(GITMODULES_URL, ERROR) \
	FUNC(GITMODULES_PATH, ERROR) \
	FUNC(GITMODULES_UPDATE, ERROR) \
	/* warnings */ \
	FUNC(BAD_FILEMODE, WARN) \
	FUNC(EMPTY_NAME, WARN) \
	FUNC(FULL_PATHNAME, WARN) \
	FUNC(HAS_DOT, WARN) \
	FUNC(HAS_DOTDOT, WARN) \
	FUNC(HAS_DOTGIT, WARN) \
	FUNC(NULL_SHA1, WARN) \
	FUNC(ZERO_PADDED_FILEMODE, WARN) \
	FUNC(NUL_IN_COMMIT, WARN) \
	/* infos (reported as warnings, but ignored by default) */ \
	FUNC(GITMODULES_PARSE, INFO) \
	FUNC(BAD_TAG_NAME, INFO) \
	FUNC(MISSING_TAGGER_ENTRY, INFO) \
	/* ignored (elevated when requested) */ \
	FUNC(EXTRA_HEADER_ENTRY, IGNORE)

#define MSG_ID(id, msg_type) FSCK_MSG_##id,
enum fsck_msg_id {
	FOREACH_MSG_ID(MSG_ID)
	FSCK_MSG_MAX
};
#undef MSG_ID

#define STR(x) #x
#define MSG_ID(id, msg_type) { STR(id), NULL, NULL, FSCK_##msg_type },
static struct {
	const char *id_string;
	const char *downcased;
	const char *camelcased;
	int msg_type;
} msg_id_info[FSCK_MSG_MAX + 1] = {
	FOREACH_MSG_ID(MSG_ID)
	{ NULL, NULL, NULL, -1 }
};
#undef MSG_ID

static void prepare_msg_ids(void)
{
	int i;

	if (msg_id_info[0].downcased)
		return;

	/* convert id_string to lower case, without underscores. */
	for (i = 0; i < FSCK_MSG_MAX; i++) {
		const char *p = msg_id_info[i].id_string;
		int len = strlen(p);
		char *q = xmalloc(len);

		msg_id_info[i].downcased = q;
		while (*p)
			if (*p == '_')
				p++;
			else
				*(q)++ = tolower(*(p)++);
		*q = '\0';

		p = msg_id_info[i].id_string;
		q = xmalloc(len);
		msg_id_info[i].camelcased = q;
		while (*p) {
			if (*p == '_') {
				p++;
				if (*p)
					*q++ = *p++;
			} else {
				*q++ = tolower(*p++);
			}
		}
		*q = '\0';
	}
}

static int parse_msg_id(const char *text)
{
	int i;

	prepare_msg_ids();

	for (i = 0; i < FSCK_MSG_MAX; i++)
		if (!strcmp(text, msg_id_info[i].downcased))
			return i;

	return -1;
}

void list_config_fsck_msg_ids(struct string_list *list, const char *prefix)
{
	int i;

	prepare_msg_ids();

	for (i = 0; i < FSCK_MSG_MAX; i++)
		list_config_item(list, prefix, msg_id_info[i].camelcased);
}

static int fsck_msg_type(enum fsck_msg_id msg_id,
	struct fsck_options *options)
{
	int msg_type;

	assert(msg_id >= 0 && msg_id < FSCK_MSG_MAX);

	if (options->msg_type)
		msg_type = options->msg_type[msg_id];
	else {
		msg_type = msg_id_info[msg_id].msg_type;
		if (options->strict && msg_type == FSCK_WARN)
			msg_type = FSCK_ERROR;
	}

	return msg_type;
}

static int parse_msg_type(const char *str)
{
	if (!strcmp(str, "error"))
		return FSCK_ERROR;
	else if (!strcmp(str, "warn"))
		return FSCK_WARN;
	else if (!strcmp(str, "ignore"))
		return FSCK_IGNORE;
	else
		die("Unknown fsck message type: '%s'", str);
}

int is_valid_msg_type(const char *msg_id, const char *msg_type)
{
	if (parse_msg_id(msg_id) < 0)
		return 0;
	parse_msg_type(msg_type);
	return 1;
}

void fsck_set_msg_type(struct fsck_options *options,
		const char *msg_id, const char *msg_type)
{
	int id = parse_msg_id(msg_id), type;

	if (id < 0)
		die("Unhandled message id: %s", msg_id);
	type = parse_msg_type(msg_type);

	if (type != FSCK_ERROR && msg_id_info[id].msg_type == FSCK_FATAL)
		die("Cannot demote %s to %s", msg_id, msg_type);

	if (!options->msg_type) {
		int i;
		int *msg_type;
		ALLOC_ARRAY(msg_type, FSCK_MSG_MAX);
		for (i = 0; i < FSCK_MSG_MAX; i++)
			msg_type[i] = fsck_msg_type(i, options);
		options->msg_type = msg_type;
	}

	options->msg_type[id] = type;
}

void fsck_set_msg_types(struct fsck_options *options, const char *values)
{
	char *buf = xstrdup(values), *to_free = buf;
	int done = 0;

	while (!done) {
		int len = strcspn(buf, " ,|"), equal;

		done = !buf[len];
		if (!len) {
			buf++;
			continue;
		}
		buf[len] = '\0';

		for (equal = 0;
		     equal < len && buf[equal] != '=' && buf[equal] != ':';
		     equal++)
			buf[equal] = tolower(buf[equal]);
		buf[equal] = '\0';

		if (!strcmp(buf, "skiplist")) {
			if (equal == len)
				die("skiplist requires a path");
			oidset_parse_file(&options->skiplist, buf + equal + 1);
			buf += len + 1;
			continue;
		}

		if (equal == len)
			die("Missing '=': '%s'", buf);

		fsck_set_msg_type(options, buf, buf + equal + 1);
		buf += len + 1;
	}
	free(to_free);
}

static void append_msg_id(struct strbuf *sb, const char *msg_id)
{
	for (;;) {
		char c = *(msg_id)++;

		if (!c)
			break;
		if (c != '_')
			strbuf_addch(sb, tolower(c));
		else {
			assert(*msg_id);
			strbuf_addch(sb, *(msg_id)++);
		}
	}

	strbuf_addstr(sb, ": ");
}

static int object_on_skiplist(struct fsck_options *opts,
			      const struct object_id *oid)
{
	return opts && oid && oidset_contains(&opts->skiplist, oid);
}

__attribute__((format (printf, 5, 6)))
static int report(struct fsck_options *options,
		  const struct object_id *oid, enum object_type object_type,
		  enum fsck_msg_id id, const char *fmt, ...)
{
	va_list ap;
	struct strbuf sb = STRBUF_INIT;
	int msg_type = fsck_msg_type(id, options), result;

	if (msg_type == FSCK_IGNORE)
		return 0;

	if (object_on_skiplist(options, oid))
		return 0;

	if (msg_type == FSCK_FATAL)
		msg_type = FSCK_ERROR;
	else if (msg_type == FSCK_INFO)
		msg_type = FSCK_WARN;

	append_msg_id(&sb, msg_id_info[id].id_string);

	va_start(ap, fmt);
	strbuf_vaddf(&sb, fmt, ap);
	result = options->error_func(options, oid, object_type,
				     msg_type, sb.buf);
	strbuf_release(&sb);
	va_end(ap);

	return result;
}

void fsck_enable_object_names(struct fsck_options *options)
{
	if (!options->object_names)
		options->object_names = kh_init_oid_map();
}

const char *fsck_get_object_name(struct fsck_options *options,
				 const struct object_id *oid)
{
	khiter_t pos;
	if (!options->object_names)
		return NULL;
	pos = kh_get_oid_map(options->object_names, *oid);
	if (pos >= kh_end(options->object_names))
		return NULL;
	return kh_value(options->object_names, pos);
}

void fsck_put_object_name(struct fsck_options *options,
			  const struct object_id *oid,
			  const char *fmt, ...)
{
	va_list ap;
	struct strbuf buf = STRBUF_INIT;
	khiter_t pos;
	int hashret;

	if (!options->object_names)
		return;

	pos = kh_put_oid_map(options->object_names, *oid, &hashret);
	if (!hashret)
		return;
	va_start(ap, fmt);
	strbuf_vaddf(&buf, fmt, ap);
	kh_value(options->object_names, pos) = strbuf_detach(&buf, NULL);
	va_end(ap);
}

const char *fsck_describe_object(struct fsck_options *options,
				 const struct object_id *oid)
{
	static struct strbuf bufs[] = {
		STRBUF_INIT, STRBUF_INIT, STRBUF_INIT, STRBUF_INIT
	};
	static int b = 0;
	struct strbuf *buf;
	const char *name = fsck_get_object_name(options, oid);

	buf = bufs + b;
	b = (b + 1) % ARRAY_SIZE(bufs);
	strbuf_reset(buf);
	strbuf_addstr(buf, oid_to_hex(oid));
	if (name)
		strbuf_addf(buf, " (%s)", name);

	return buf->buf;
}

static int fsck_walk_tree(struct tree *tree, void *data, struct fsck_options *options)
{
	struct tree_desc desc;
	struct name_entry entry;
	int res = 0;
	const char *name;

	if (parse_tree(tree))
		return -1;

	name = fsck_get_object_name(options, &tree->object.oid);
	if (init_tree_desc_gently(&desc, tree->buffer, tree->size))
		return -1;
	while (tree_entry_gently(&desc, &entry)) {
		struct object *obj;
		int result;

		if (S_ISGITLINK(entry.mode))
			continue;

		if (S_ISDIR(entry.mode)) {
			obj = (struct object *)lookup_tree(the_repository, &entry.oid);
			if (name && obj)
				fsck_put_object_name(options, &entry.oid, "%s%s/",
						     name, entry.path);
			result = options->walk(obj, OBJ_TREE, data, options);
		}
		else if (S_ISREG(entry.mode) || S_ISLNK(entry.mode)) {
			obj = (struct object *)lookup_blob(the_repository, &entry.oid);
			if (name && obj)
				fsck_put_object_name(options, &entry.oid, "%s%s",
						     name, entry.path);
			result = options->walk(obj, OBJ_BLOB, data, options);
		}
		else {
			result = error("in tree %s: entry %s has bad mode %.6o",
				       fsck_describe_object(options, &tree->object.oid),
				       entry.path, entry.mode);
		}
		if (result < 0)
			return result;
		if (!res)
			res = result;
	}
	return res;
}

static int fsck_walk_commit(struct commit *commit, void *data, struct fsck_options *options)
{
	int counter = 0, generation = 0, name_prefix_len = 0;
	struct commit_list *parents;
	int res;
	int result;
	const char *name;

	if (parse_commit(commit))
		return -1;

	name = fsck_get_object_name(options, &commit->object.oid);
	if (name)
		fsck_put_object_name(options, get_commit_tree_oid(commit),
				     "%s:", name);

	result = options->walk((struct object *)get_commit_tree(commit),
			       OBJ_TREE, data, options);
	if (result < 0)
		return result;
	res = result;

	parents = commit->parents;
	if (name && parents) {
		int len = strlen(name), power;

		if (len && name[len - 1] == '^') {
			generation = 1;
			name_prefix_len = len - 1;
		}
		else { /* parse ~<generation> suffix */
			for (generation = 0, power = 1;
			     len && isdigit(name[len - 1]);
			     power *= 10)
				generation += power * (name[--len] - '0');
			if (power > 1 && len && name[len - 1] == '~')
				name_prefix_len = len - 1;
			else {
				/* Maybe a non-first parent, e.g. HEAD^2 */
				generation = 0;
				name_prefix_len = len;
			}
		}
	}

	while (parents) {
		if (name) {
			struct object_id *oid = &parents->item->object.oid;

			if (counter++)
				fsck_put_object_name(options, oid, "%s^%d",
						     name, counter);
			else if (generation > 0)
				fsck_put_object_name(options, oid, "%.*s~%d",
						     name_prefix_len, name,
						     generation + 1);
			else
				fsck_put_object_name(options, oid, "%s^", name);
		}
		result = options->walk((struct object *)parents->item, OBJ_COMMIT, data, options);
		if (result < 0)
			return result;
		if (!res)
			res = result;
		parents = parents->next;
	}
	return res;
}

static int fsck_walk_tag(struct tag *tag, void *data, struct fsck_options *options)
{
	const char *name = fsck_get_object_name(options, &tag->object.oid);

	if (parse_tag(tag))
		return -1;
	if (name)
		fsck_put_object_name(options, &tag->tagged->oid, "%s", name);
	return options->walk(tag->tagged, OBJ_ANY, data, options);
}

int fsck_walk(struct object *obj, void *data, struct fsck_options *options)
{
	if (!obj)
		return -1;

	if (obj->type == OBJ_NONE)
		parse_object(the_repository, &obj->oid);

	switch (obj->type) {
	case OBJ_BLOB:
		return 0;
	case OBJ_TREE:
		return fsck_walk_tree((struct tree *)obj, data, options);
	case OBJ_COMMIT:
		return fsck_walk_commit((struct commit *)obj, data, options);
	case OBJ_TAG:
		return fsck_walk_tag((struct tag *)obj, data, options);
	default:
		error("Unknown object type for %s",
		      fsck_describe_object(options, &obj->oid));
		return -1;
	}
}

struct name_stack {
	const char **names;
	size_t nr, alloc;
};

static void name_stack_push(struct name_stack *stack, const char *name)
{
	ALLOC_GROW(stack->names, stack->nr + 1, stack->alloc);
	stack->names[stack->nr++] = name;
}

static const char *name_stack_pop(struct name_stack *stack)
{
	return stack->nr ? stack->names[--stack->nr] : NULL;
}

static void name_stack_clear(struct name_stack *stack)
{
	FREE_AND_NULL(stack->names);
	stack->nr = stack->alloc = 0;
}

/*
 * The entries in a tree are ordered in the _path_ order,
 * which means that a directory entry is ordered by adding
 * a slash to the end of it.
 *
 * So a directory called "a" is ordered _after_ a file
 * called "a.c", because "a/" sorts after "a.c".
 */
#define TREE_UNORDERED (-1)
#define TREE_HAS_DUPS  (-2)

static int is_less_than_slash(unsigned char c)
{
	return '\0' < c && c < '/';
}

static int verify_ordered(unsigned mode1, const char *name1,
			  unsigned mode2, const char *name2,
			  struct name_stack *candidates)
{
	int len1 = strlen(name1);
	int len2 = strlen(name2);
	int len = len1 < len2 ? len1 : len2;
	unsigned char c1, c2;
	int cmp;

	cmp = memcmp(name1, name2, len);
	if (cmp < 0)
		return 0;
	if (cmp > 0)
		return TREE_UNORDERED;

	/*
	 * Ok, the first <len> characters are the same.
	 * Now we need to order the next one, but turn
	 * a '\0' into a '/' for a directory entry.
	 */
	c1 = name1[len];
	c2 = name2[len];
	if (!c1 && !c2)
		/*
		 * git-write-tree used to write out a nonsense tree that has
		 * entries with the same name, one blob and one tree.  Make
		 * sure we do not have duplicate entries.
		 */
		return TREE_HAS_DUPS;
	if (!c1 && S_ISDIR(mode1))
		c1 = '/';
	if (!c2 && S_ISDIR(mode2))
		c2 = '/';

	/*
	 * There can be non-consecutive duplicates due to the implicitly
	 * added slash, e.g.:
	 *
	 *   foo
	 *   foo.bar
	 *   foo.bar.baz
	 *   foo.bar/
	 *   foo/
	 *
	 * Record non-directory candidates (like "foo" and "foo.bar" in
	 * the example) on a stack and check directory candidates (like
	 * foo/" and "foo.bar/") against that stack.
	 */
	if (!c1 && is_less_than_slash(c2)) {
		name_stack_push(candidates, name1);
	} else if (c2 == '/' && is_less_than_slash(c1)) {
		for (;;) {
			const char *p;
			const char *f_name = name_stack_pop(candidates);

			if (!f_name)
				break;
			if (!skip_prefix(name2, f_name, &p))
				continue;
			if (!*p)
				return TREE_HAS_DUPS;
			if (is_less_than_slash(*p)) {
				name_stack_push(candidates, f_name);
				break;
			}
		}
	}

	return c1 < c2 ? 0 : TREE_UNORDERED;
}

static int fsck_tree(const struct object_id *oid,
		     const char *buffer, unsigned long size,
		     struct fsck_options *options)
{
	int retval = 0;
	int has_null_sha1 = 0;
	int has_full_path = 0;
	int has_empty_name = 0;
	int has_dot = 0;
	int has_dotdot = 0;
	int has_dotgit = 0;
	int has_zero_pad = 0;
	int has_bad_modes = 0;
	int has_dup_entries = 0;
	int not_properly_sorted = 0;
	struct tree_desc desc;
	unsigned o_mode;
	const char *o_name;
	struct name_stack df_dup_candidates = { NULL };

	if (init_tree_desc_gently(&desc, buffer, size)) {
		retval += report(options, oid, OBJ_TREE, FSCK_MSG_BAD_TREE, "cannot be parsed as a tree");
		return retval;
	}

	o_mode = 0;
	o_name = NULL;

	while (desc.size) {
		unsigned short mode;
		const char *name, *backslash;
		const struct object_id *oid;

		oid = tree_entry_extract(&desc, &name, &mode);

		has_null_sha1 |= is_null_oid(oid);
		has_full_path |= !!strchr(name, '/');
		has_empty_name |= !*name;
		has_dot |= !strcmp(name, ".");
		has_dotdot |= !strcmp(name, "..");
		has_dotgit |= is_hfs_dotgit(name) || is_ntfs_dotgit(name);
		has_zero_pad |= *(char *)desc.buffer == '0';

		if (is_hfs_dotgitmodules(name) || is_ntfs_dotgitmodules(name)) {
			if (!S_ISLNK(mode))
				oidset_insert(&gitmodules_found, oid);
			else
				retval += report(options,
						 oid, OBJ_TREE,
						 FSCK_MSG_GITMODULES_SYMLINK,
						 ".gitmodules is a symbolic link");
		}

		if ((backslash = strchr(name, '\\'))) {
			while (backslash) {
				backslash++;
				has_dotgit |= is_ntfs_dotgit(backslash);
				if (is_ntfs_dotgitmodules(backslash)) {
					if (!S_ISLNK(mode))
						oidset_insert(&gitmodules_found, oid);
					else
						retval += report(options, oid, OBJ_TREE,
								 FSCK_MSG_GITMODULES_SYMLINK,
								 ".gitmodules is a symbolic link");
				}
				backslash = strchr(backslash, '\\');
			}
		}

		if (update_tree_entry_gently(&desc)) {
			retval += report(options, oid, OBJ_TREE, FSCK_MSG_BAD_TREE, "cannot be parsed as a tree");
			break;
		}

		switch (mode) {
		/*
		 * Standard modes..
		 */
		case S_IFREG | 0755:
		case S_IFREG | 0644:
		case S_IFLNK:
		case S_IFDIR:
		case S_IFGITLINK:
			break;
		/*
		 * This is nonstandard, but we had a few of these
		 * early on when we honored the full set of mode
		 * bits..
		 */
		case S_IFREG | 0664:
			if (!options->strict)
				break;
			/* fallthrough */
		default:
			has_bad_modes = 1;
		}

		if (o_name) {
			switch (verify_ordered(o_mode, o_name, mode, name,
					       &df_dup_candidates)) {
			case TREE_UNORDERED:
				not_properly_sorted = 1;
				break;
			case TREE_HAS_DUPS:
				has_dup_entries = 1;
				break;
			default:
				break;
			}
		}

		o_mode = mode;
		o_name = name;
	}

	name_stack_clear(&df_dup_candidates);

	if (has_null_sha1)
		retval += report(options, oid, OBJ_TREE, FSCK_MSG_NULL_SHA1, "contains entries pointing to null sha1");
	if (has_full_path)
		retval += report(options, oid, OBJ_TREE, FSCK_MSG_FULL_PATHNAME, "contains full pathnames");
	if (has_empty_name)
		retval += report(options, oid, OBJ_TREE, FSCK_MSG_EMPTY_NAME, "contains empty pathname");
	if (has_dot)
		retval += report(options, oid, OBJ_TREE, FSCK_MSG_HAS_DOT, "contains '.'");
	if (has_dotdot)
		retval += report(options, oid, OBJ_TREE, FSCK_MSG_HAS_DOTDOT, "contains '..'");
	if (has_dotgit)
		retval += report(options, oid, OBJ_TREE, FSCK_MSG_HAS_DOTGIT, "contains '.git'");
	if (has_zero_pad)
		retval += report(options, oid, OBJ_TREE, FSCK_MSG_ZERO_PADDED_FILEMODE, "contains zero-padded file modes");
	if (has_bad_modes)
		retval += report(options, oid, OBJ_TREE, FSCK_MSG_BAD_FILEMODE, "contains bad file modes");
	if (has_dup_entries)
		retval += report(options, oid, OBJ_TREE, FSCK_MSG_DUPLICATE_ENTRIES, "contains duplicate file entries");
	if (not_properly_sorted)
		retval += report(options, oid, OBJ_TREE, FSCK_MSG_TREE_NOT_SORTED, "not properly sorted");
	return retval;
}

static int verify_headers(const void *data, unsigned long size,
			  const struct object_id *oid, enum object_type type,
			  struct fsck_options *options)
{
	const char *buffer = (const char *)data;
	unsigned long i;

	for (i = 0; i < size; i++) {
		switch (buffer[i]) {
		case '\0':
			return report(options, oid, type,
				FSCK_MSG_NUL_IN_HEADER,
				"unterminated header: NUL at offset %ld", i);
		case '\n':
			if (i + 1 < size && buffer[i + 1] == '\n')
				return 0;
		}
	}

	/*
	 * We did not find double-LF that separates the header
	 * and the body.  Not having a body is not a crime but
	 * we do want to see the terminating LF for the last header
	 * line.
	 */
	if (size && buffer[size - 1] == '\n')
		return 0;

	return report(options, oid, type,
		FSCK_MSG_UNTERMINATED_HEADER, "unterminated header");
}

static int fsck_ident(const char **ident,
		      const struct object_id *oid, enum object_type type,
		      struct fsck_options *options)
{
	const char *p = *ident;
	char *end;

	*ident = strchrnul(*ident, '\n');
	if (**ident == '\n')
		(*ident)++;

	if (*p == '<')
		return report(options, oid, type, FSCK_MSG_MISSING_NAME_BEFORE_EMAIL, "invalid author/committer line - missing space before email");
	p += strcspn(p, "<>\n");
	if (*p == '>')
		return report(options, oid, type, FSCK_MSG_BAD_NAME, "invalid author/committer line - bad name");
	if (*p != '<')
		return report(options, oid, type, FSCK_MSG_MISSING_EMAIL, "invalid author/committer line - missing email");
	if (p[-1] != ' ')
		return report(options, oid, type, FSCK_MSG_MISSING_SPACE_BEFORE_EMAIL, "invalid author/committer line - missing space before email");
	p++;
	p += strcspn(p, "<>\n");
	if (*p != '>')
		return report(options, oid, type, FSCK_MSG_BAD_EMAIL, "invalid author/committer line - bad email");
	p++;
	if (*p != ' ')
		return report(options, oid, type, FSCK_MSG_MISSING_SPACE_BEFORE_DATE, "invalid author/committer line - missing space before date");
	p++;
	if (*p == '0' && p[1] != ' ')
		return report(options, oid, type, FSCK_MSG_ZERO_PADDED_DATE, "invalid author/committer line - zero-padded date");
	if (date_overflows(parse_timestamp(p, &end, 10)))
		return report(options, oid, type, FSCK_MSG_BAD_DATE_OVERFLOW, "invalid author/committer line - date causes integer overflow");
	if ((end == p || *end != ' '))
		return report(options, oid, type, FSCK_MSG_BAD_DATE, "invalid author/committer line - bad date");
	p = end + 1;
	if ((*p != '+' && *p != '-') ||
	    !isdigit(p[1]) ||
	    !isdigit(p[2]) ||
	    !isdigit(p[3]) ||
	    !isdigit(p[4]) ||
	    (p[5] != '\n'))
		return report(options, oid, type, FSCK_MSG_BAD_TIMEZONE, "invalid author/committer line - bad time zone");
	p += 6;
	return 0;
}

static int fsck_commit(const struct object_id *oid,
		       const char *buffer, unsigned long size,
		       struct fsck_options *options)
{
	struct object_id tree_oid, parent_oid;
	unsigned author_count;
	int err;
	const char *buffer_begin = buffer;
	const char *p;

	if (verify_headers(buffer, size, oid, OBJ_COMMIT, options))
		return -1;

	if (!skip_prefix(buffer, "tree ", &buffer))
		return report(options, oid, OBJ_COMMIT, FSCK_MSG_MISSING_TREE, "invalid format - expected 'tree' line");
	if (parse_oid_hex(buffer, &tree_oid, &p) || *p != '\n') {
		err = report(options, oid, OBJ_COMMIT, FSCK_MSG_BAD_TREE_SHA1, "invalid 'tree' line format - bad sha1");
		if (err)
			return err;
	}
	buffer = p + 1;
	while (skip_prefix(buffer, "parent ", &buffer)) {
		if (parse_oid_hex(buffer, &parent_oid, &p) || *p != '\n') {
			err = report(options, oid, OBJ_COMMIT, FSCK_MSG_BAD_PARENT_SHA1, "invalid 'parent' line format - bad sha1");
			if (err)
				return err;
		}
		buffer = p + 1;
	}
	author_count = 0;
	while (skip_prefix(buffer, "author ", &buffer)) {
		author_count++;
		err = fsck_ident(&buffer, oid, OBJ_COMMIT, options);
		if (err)
			return err;
	}
	if (author_count < 1)
		err = report(options, oid, OBJ_COMMIT, FSCK_MSG_MISSING_AUTHOR, "invalid format - expected 'author' line");
	else if (author_count > 1)
		err = report(options, oid, OBJ_COMMIT, FSCK_MSG_MULTIPLE_AUTHORS, "invalid format - multiple 'author' lines");
	if (err)
		return err;
	if (!skip_prefix(buffer, "committer ", &buffer))
		return report(options, oid, OBJ_COMMIT, FSCK_MSG_MISSING_COMMITTER, "invalid format - expected 'committer' line");
	err = fsck_ident(&buffer, oid, OBJ_COMMIT, options);
	if (err)
		return err;
	if (memchr(buffer_begin, '\0', size)) {
		err = report(options, oid, OBJ_COMMIT, FSCK_MSG_NUL_IN_COMMIT,
			     "NUL byte in the commit object body");
		if (err)
			return err;
	}
	return 0;
}

static int fsck_tag(const struct object_id *oid, const char *buffer,
		    unsigned long size, struct fsck_options *options)
{
	struct object_id tagged_oid;
	int tagged_type;
	return fsck_tag_standalone(oid, buffer, size, options, &tagged_oid,
				   &tagged_type);
}

int fsck_tag_standalone(const struct object_id *oid, const char *buffer,
			unsigned long size, struct fsck_options *options,
			struct object_id *tagged_oid,
			int *tagged_type)
{
	int ret = 0;
	char *eol;
	struct strbuf sb = STRBUF_INIT;
	const char *p;

	ret = verify_headers(buffer, size, oid, OBJ_TAG, options);
	if (ret)
		goto done;

	if (!skip_prefix(buffer, "object ", &buffer)) {
		ret = report(options, oid, OBJ_TAG, FSCK_MSG_MISSING_OBJECT, "invalid format - expected 'object' line");
		goto done;
	}
	if (parse_oid_hex(buffer, tagged_oid, &p) || *p != '\n') {
		ret = report(options, oid, OBJ_TAG, FSCK_MSG_BAD_OBJECT_SHA1, "invalid 'object' line format - bad sha1");
		if (ret)
			goto done;
	}
	buffer = p + 1;

	if (!skip_prefix(buffer, "type ", &buffer)) {
		ret = report(options, oid, OBJ_TAG, FSCK_MSG_MISSING_TYPE_ENTRY, "invalid format - expected 'type' line");
		goto done;
	}
	eol = strchr(buffer, '\n');
	if (!eol) {
		ret = report(options, oid, OBJ_TAG, FSCK_MSG_MISSING_TYPE, "invalid format - unexpected end after 'type' line");
		goto done;
	}
	*tagged_type = type_from_string_gently(buffer, eol - buffer, 1);
	if (*tagged_type < 0)
		ret = report(options, oid, OBJ_TAG, FSCK_MSG_BAD_TYPE, "invalid 'type' value");
	if (ret)
		goto done;
	buffer = eol + 1;

	if (!skip_prefix(buffer, "tag ", &buffer)) {
		ret = report(options, oid, OBJ_TAG, FSCK_MSG_MISSING_TAG_ENTRY, "invalid format - expected 'tag' line");
		goto done;
	}
	eol = strchr(buffer, '\n');
	if (!eol) {
		ret = report(options, oid, OBJ_TAG, FSCK_MSG_MISSING_TAG, "invalid format - unexpected end after 'type' line");
		goto done;
	}
	strbuf_addf(&sb, "refs/tags/%.*s", (int)(eol - buffer), buffer);
	if (check_refname_format(sb.buf, 0)) {
		ret = report(options, oid, OBJ_TAG,
			     FSCK_MSG_BAD_TAG_NAME,
			     "invalid 'tag' name: %.*s",
			     (int)(eol - buffer), buffer);
		if (ret)
			goto done;
	}
	buffer = eol + 1;

	if (!skip_prefix(buffer, "tagger ", &buffer)) {
		/* early tags do not contain 'tagger' lines; warn only */
		ret = report(options, oid, OBJ_TAG, FSCK_MSG_MISSING_TAGGER_ENTRY, "invalid format - expected 'tagger' line");
		if (ret)
			goto done;
	}
	else
		ret = fsck_ident(&buffer, oid, OBJ_TAG, options);
	if (!*buffer)
		goto done;

	if (!starts_with(buffer, "\n")) {
		/*
		 * The verify_headers() check will allow
		 * e.g. "[...]tagger <tagger>\nsome
		 * garbage\n\nmessage" to pass, thinking "some
		 * garbage" could be a custom header. E.g. "mktag"
		 * doesn't want any unknown headers.
		 */
		ret = report(options, oid, OBJ_TAG, FSCK_MSG_EXTRA_HEADER_ENTRY, "invalid format - extra header(s) after 'tagger'");
		if (ret)
			goto done;
	}

done:
	strbuf_release(&sb);
	return ret;
}

/*
 * Like builtin/submodule--helper.c's starts_with_dot_slash, but without
 * relying on the platform-dependent is_dir_sep helper.
 *
 * This is for use in checking whether a submodule URL is interpreted as
 * relative to the current directory on any platform, since \ is a
 * directory separator on Windows but not on other platforms.
 */
static int starts_with_dot_slash(const char *str)
{
	return str[0] == '.' && (str[1] == '/' || str[1] == '\\');
}

/*
 * Like starts_with_dot_slash, this is a variant of submodule--helper's
 * helper of the same name with the twist that it accepts backslash as a
 * directory separator even on non-Windows platforms.
 */
static int starts_with_dot_dot_slash(const char *str)
{
	return str[0] == '.' && starts_with_dot_slash(str + 1);
}

static int submodule_url_is_relative(const char *url)
{
	return starts_with_dot_slash(url) || starts_with_dot_dot_slash(url);
}

/*
 * Count directory components that a relative submodule URL should chop
 * from the remote_url it is to be resolved against.
 *
 * In other words, this counts "../" components at the start of a
 * submodule URL.
 *
 * Returns the number of directory components to chop and writes a
 * pointer to the next character of url after all leading "./" and
 * "../" components to out.
 */
static int count_leading_dotdots(const char *url, const char **out)
{
	int result = 0;
	while (1) {
		if (starts_with_dot_dot_slash(url)) {
			result++;
			url += strlen("../");
			continue;
		}
		if (starts_with_dot_slash(url)) {
			url += strlen("./");
			continue;
		}
		*out = url;
		return result;
	}
}
/*
 * Check whether a transport is implemented by git-remote-curl.
 *
 * If it is, returns 1 and writes the URL that would be passed to
 * git-remote-curl to the "out" parameter.
 *
 * Otherwise, returns 0 and leaves "out" untouched.
 *
 * Examples:
 *   http::https://example.com/repo.git -> 1, https://example.com/repo.git
 *   https://example.com/repo.git -> 1, https://example.com/repo.git
 *   git://example.com/repo.git -> 0
 *
 * This is for use in checking for previously exploitable bugs that
 * required a submodule URL to be passed to git-remote-curl.
 */
static int url_to_curl_url(const char *url, const char **out)
{
	/*
	 * We don't need to check for case-aliases, "http.exe", and so
	 * on because in the default configuration, is_transport_allowed
	 * prevents URLs with those schemes from being cloned
	 * automatically.
	 */
	if (skip_prefix(url, "http::", out) ||
	    skip_prefix(url, "https::", out) ||
	    skip_prefix(url, "ftp::", out) ||
	    skip_prefix(url, "ftps::", out))
		return 1;
	if (starts_with(url, "http://") ||
	    starts_with(url, "https://") ||
	    starts_with(url, "ftp://") ||
	    starts_with(url, "ftps://")) {
		*out = url;
		return 1;
	}
	return 0;
}

static int check_submodule_url(const char *url)
{
	const char *curl_url;

	if (looks_like_command_line_option(url))
		return -1;

	if (submodule_url_is_relative(url) || starts_with(url, "git://")) {
		char *decoded;
		const char *next;
		int has_nl;

		/*
		 * This could be appended to an http URL and url-decoded;
		 * check for malicious characters.
		 */
		decoded = url_decode(url);
		has_nl = !!strchr(decoded, '\n');

		free(decoded);
		if (has_nl)
			return -1;

		/*
		 * URLs which escape their root via "../" can overwrite
		 * the host field and previous components, resolving to
		 * URLs like https::example.com/submodule.git and
		 * https:///example.com/submodule.git that were
		 * susceptible to CVE-2020-11008.
		 */
		if (count_leading_dotdots(url, &next) > 0 &&
		    (*next == ':' || *next == '/'))
			return -1;
	}

	else if (url_to_curl_url(url, &curl_url)) {
		struct credential c = CREDENTIAL_INIT;
		int ret = 0;
		if (credential_from_url_gently(&c, curl_url, 1) ||
		    !*c.host)
			ret = -1;
		credential_clear(&c);
		return ret;
	}

	return 0;
}

struct fsck_gitmodules_data {
	const struct object_id *oid;
	struct fsck_options *options;
	int ret;
};

static int fsck_gitmodules_fn(const char *var, const char *value, void *vdata)
{
	struct fsck_gitmodules_data *data = vdata;
	const char *subsection, *key;
	size_t subsection_len;
	char *name;

	if (parse_config_key(var, "submodule", &subsection, &subsection_len, &key) < 0 ||
	    !subsection)
		return 0;

	name = xmemdupz(subsection, subsection_len);
	if (check_submodule_name(name) < 0)
		data->ret |= report(data->options,
				    data->oid, OBJ_BLOB,
				    FSCK_MSG_GITMODULES_NAME,
				    "disallowed submodule name: %s",
				    name);
	if (!strcmp(key, "url") && value &&
	    check_submodule_url(value) < 0)
		data->ret |= report(data->options,
				    data->oid, OBJ_BLOB,
				    FSCK_MSG_GITMODULES_URL,
				    "disallowed submodule url: %s",
				    value);
	if (!strcmp(key, "path") && value &&
	    looks_like_command_line_option(value))
		data->ret |= report(data->options,
				    data->oid, OBJ_BLOB,
				    FSCK_MSG_GITMODULES_PATH,
				    "disallowed submodule path: %s",
				    value);
	if (!strcmp(key, "update") && value &&
	    parse_submodule_update_type(value) == SM_UPDATE_COMMAND)
		data->ret |= report(data->options, data->oid, OBJ_BLOB,
				    FSCK_MSG_GITMODULES_UPDATE,
				    "disallowed submodule update setting: %s",
				    value);
	free(name);

	return 0;
}

static int fsck_blob(const struct object_id *oid, const char *buf,
		     unsigned long size, struct fsck_options *options)
{
	struct fsck_gitmodules_data data;
	struct config_options config_opts = { 0 };

	if (!oidset_contains(&gitmodules_found, oid))
		return 0;
	oidset_insert(&gitmodules_done, oid);

	if (object_on_skiplist(options, oid))
		return 0;

	if (!buf) {
		/*
		 * A missing buffer here is a sign that the caller found the
		 * blob too gigantic to load into memory. Let's just consider
		 * that an error.
		 */
		return report(options, oid, OBJ_BLOB,
			      FSCK_MSG_GITMODULES_LARGE,
			      ".gitmodules too large to parse");
	}

	data.oid = oid;
	data.options = options;
	data.ret = 0;
	config_opts.error_action = CONFIG_ERROR_SILENT;
	if (git_config_from_mem(fsck_gitmodules_fn, CONFIG_ORIGIN_BLOB,
				".gitmodules", buf, size, &data, &config_opts))
		data.ret |= report(options, oid, OBJ_BLOB,
				   FSCK_MSG_GITMODULES_PARSE,
				   "could not parse gitmodules blob");

	return data.ret;
}

int fsck_object(struct object *obj, void *data, unsigned long size,
	struct fsck_options *options)
{
	if (!obj)
		return report(options, NULL, OBJ_NONE, FSCK_MSG_BAD_OBJECT_SHA1, "no valid object to fsck");

	if (obj->type == OBJ_BLOB)
		return fsck_blob(&obj->oid, data, size, options);
	if (obj->type == OBJ_TREE)
		return fsck_tree(&obj->oid, data, size, options);
	if (obj->type == OBJ_COMMIT)
		return fsck_commit(&obj->oid, data, size, options);
	if (obj->type == OBJ_TAG)
		return fsck_tag(&obj->oid, data, size, options);

	return report(options, &obj->oid, obj->type,
		      FSCK_MSG_UNKNOWN_TYPE,
		      "unknown type '%d' (internal fsck error)",
		      obj->type);
}

int fsck_error_function(struct fsck_options *o,
			const struct object_id *oid,
			enum object_type object_type,
			int msg_type, const char *message)
{
	if (msg_type == FSCK_WARN) {
		warning("object %s: %s", fsck_describe_object(o, oid), message);
		return 0;
	}
	error("object %s: %s", fsck_describe_object(o, oid), message);
	return 1;
}

void register_found_gitmodules(const struct object_id *oid)
{
	oidset_insert(&gitmodules_found, oid);
}

int fsck_finish(struct fsck_options *options)
{
	int ret = 0;
	struct oidset_iter iter;
	const struct object_id *oid;

	oidset_iter_init(&gitmodules_found, &iter);
	while ((oid = oidset_iter_next(&iter))) {
		enum object_type type;
		unsigned long size;
		char *buf;

		if (oidset_contains(&gitmodules_done, oid))
			continue;

		buf = read_object_file(oid, &type, &size);
		if (!buf) {
			if (is_promisor_object(oid))
				continue;
			ret |= report(options,
				      oid, OBJ_BLOB,
				      FSCK_MSG_GITMODULES_MISSING,
				      "unable to read .gitmodules blob");
			continue;
		}

		if (type == OBJ_BLOB)
			ret |= fsck_blob(oid, buf, size, options);
		else
			ret |= report(options,
				      oid, type,
				      FSCK_MSG_GITMODULES_BLOB,
				      "non-blob found at .gitmodules");
		free(buf);
	}


	oidset_clear(&gitmodules_found);
	oidset_clear(&gitmodules_done);
	return ret;
}

int fsck_config_internal(const char *var, const char *value, void *cb,
			 struct fsck_options *options)
{
	if (strcmp(var, "fsck.skiplist") == 0) {
		const char *path;
		struct strbuf sb = STRBUF_INIT;

		if (git_config_pathname(&path, var, value))
			return 1;
		strbuf_addf(&sb, "skiplist=%s", path);
		free((char *)path);
		fsck_set_msg_types(options, sb.buf);
		strbuf_release(&sb);
		return 0;
	}

	if (skip_prefix(var, "fsck.", &var)) {
		fsck_set_msg_type(options, var, value);
		return 0;
	}

	return git_default_config(var, value, cb);
}
