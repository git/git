#include "git-compat-util.h"
#include "date.h"
#include "dir.h"
#include "hex.h"
#include "object-store-ll.h"
#include "path.h"
#include "repository.h"
#include "object.h"
#include "attr.h"
#include "blob.h"
#include "tree.h"
#include "tree-walk.h"
#include "commit.h"
#include "tag.h"
#include "fsck.h"
#include "refs.h"
#include "url.h"
#include "utf8.h"
#include "oidset.h"
#include "packfile.h"
#include "submodule-config.h"
#include "config.h"
#include "help.h"

static ssize_t max_tree_entry_len = 4096;

#define STR(x) #x
#define MSG_ID(id, msg_type) { STR(id), NULL, NULL, FSCK_##msg_type },
static struct {
	const char *id_string;
	const char *downcased;
	const char *camelcased;
	enum fsck_msg_type msg_type;
} msg_id_info[FSCK_MSG_MAX + 1] = {
	FOREACH_FSCK_MSG_ID(MSG_ID)
	{ NULL, NULL, NULL, -1 }
};
#undef MSG_ID
#undef STR

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

static enum fsck_msg_type fsck_msg_type(enum fsck_msg_id msg_id,
	struct fsck_options *options)
{
	assert(msg_id >= 0 && msg_id < FSCK_MSG_MAX);

	if (!options->msg_type) {
		enum fsck_msg_type msg_type = msg_id_info[msg_id].msg_type;

		if (options->strict && msg_type == FSCK_WARN)
			msg_type = FSCK_ERROR;
		return msg_type;
	}

	return options->msg_type[msg_id];
}

static enum fsck_msg_type parse_msg_type(const char *str)
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

void fsck_set_msg_type_from_ids(struct fsck_options *options,
				enum fsck_msg_id msg_id,
				enum fsck_msg_type msg_type)
{
	if (!options->msg_type) {
		int i;
		enum fsck_msg_type *severity;
		ALLOC_ARRAY(severity, FSCK_MSG_MAX);
		for (i = 0; i < FSCK_MSG_MAX; i++)
			severity[i] = fsck_msg_type(i, options);
		options->msg_type = severity;
	}

	options->msg_type[msg_id] = msg_type;
}

void fsck_set_msg_type(struct fsck_options *options,
		       const char *msg_id_str, const char *msg_type_str)
{
	int msg_id = parse_msg_id(msg_id_str);
	char *to_free = NULL;
	enum fsck_msg_type msg_type;

	if (msg_id < 0)
		die("Unhandled message id: %s", msg_id_str);

	if (msg_id == FSCK_MSG_LARGE_PATHNAME) {
		const char *colon = strchr(msg_type_str, ':');
		if (colon) {
			msg_type_str = to_free =
				xmemdupz(msg_type_str, colon - msg_type_str);
			colon++;
			if (!git_parse_ssize_t(colon, &max_tree_entry_len))
				die("unable to parse max tree entry len: %s", colon);
		}
	}
	msg_type = parse_msg_type(msg_type_str);

	if (msg_type != FSCK_ERROR && msg_id_info[msg_id].msg_type == FSCK_FATAL)
		die("Cannot demote %s to %s", msg_id_str, msg_type_str);

	fsck_set_msg_type_from_ids(options, msg_id, msg_type);
	free(to_free);
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

static int object_on_skiplist(struct fsck_options *opts,
			      const struct object_id *oid)
{
	return opts && oid && oidset_contains(&opts->skiplist, oid);
}

__attribute__((format (printf, 5, 6)))
static int report(struct fsck_options *options,
		  const struct object_id *oid, enum object_type object_type,
		  enum fsck_msg_id msg_id, const char *fmt, ...)
{
	va_list ap;
	struct strbuf sb = STRBUF_INIT;
	enum fsck_msg_type msg_type = fsck_msg_type(msg_id, options);
	int result;

	if (msg_type == FSCK_IGNORE)
		return 0;

	if (object_on_skiplist(options, oid))
		return 0;

	if (msg_type == FSCK_FATAL)
		msg_type = FSCK_ERROR;
	else if (msg_type == FSCK_INFO)
		msg_type = FSCK_WARN;

	prepare_msg_ids();
	strbuf_addf(&sb, "%s: ", msg_id_info[msg_id].camelcased);

	va_start(ap, fmt);
	strbuf_vaddf(&sb, fmt, ap);
	result = options->error_func(options, oid, object_type,
				     msg_type, msg_id, sb.buf);
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
	if (init_tree_desc_gently(&desc, tree->buffer, tree->size, 0))
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

	if (repo_parse_commit(the_repository, commit))
		return -1;

	name = fsck_get_object_name(options, &commit->object.oid);
	if (name)
		fsck_put_object_name(options, get_commit_tree_oid(commit),
				     "%s:", name);

	result = options->walk((struct object *) repo_get_commit_tree(the_repository, commit),
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

static int fsck_tree(const struct object_id *tree_oid,
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
	int has_large_name = 0;
	struct tree_desc desc;
	unsigned o_mode;
	const char *o_name;
	struct name_stack df_dup_candidates = { NULL };

	if (init_tree_desc_gently(&desc, buffer, size, TREE_DESC_RAW_MODES)) {
		retval += report(options, tree_oid, OBJ_TREE,
				 FSCK_MSG_BAD_TREE,
				 "cannot be parsed as a tree");
		return retval;
	}

	o_mode = 0;
	o_name = NULL;

	while (desc.size) {
		unsigned short mode;
		const char *name, *backslash;
		const struct object_id *entry_oid;

		entry_oid = tree_entry_extract(&desc, &name, &mode);

		has_null_sha1 |= is_null_oid(entry_oid);
		has_full_path |= !!strchr(name, '/');
		has_empty_name |= !*name;
		has_dot |= !strcmp(name, ".");
		has_dotdot |= !strcmp(name, "..");
		has_dotgit |= is_hfs_dotgit(name) || is_ntfs_dotgit(name);
		has_zero_pad |= *(char *)desc.buffer == '0';
		has_large_name |= tree_entry_len(&desc.entry) > max_tree_entry_len;

		if (is_hfs_dotgitmodules(name) || is_ntfs_dotgitmodules(name)) {
			if (!S_ISLNK(mode))
				oidset_insert(&options->gitmodules_found,
					      entry_oid);
			else
				retval += report(options,
						 tree_oid, OBJ_TREE,
						 FSCK_MSG_GITMODULES_SYMLINK,
						 ".gitmodules is a symbolic link");
		}

		if (is_hfs_dotgitattributes(name) || is_ntfs_dotgitattributes(name)) {
			if (!S_ISLNK(mode))
				oidset_insert(&options->gitattributes_found,
					      entry_oid);
			else
				retval += report(options, tree_oid, OBJ_TREE,
						 FSCK_MSG_GITATTRIBUTES_SYMLINK,
						 ".gitattributes is a symlink");
		}

		if (S_ISLNK(mode)) {
			if (is_hfs_dotgitignore(name) ||
			    is_ntfs_dotgitignore(name))
				retval += report(options, tree_oid, OBJ_TREE,
						 FSCK_MSG_GITIGNORE_SYMLINK,
						 ".gitignore is a symlink");
			if (is_hfs_dotmailmap(name) ||
			    is_ntfs_dotmailmap(name))
				retval += report(options, tree_oid, OBJ_TREE,
						 FSCK_MSG_MAILMAP_SYMLINK,
						 ".mailmap is a symlink");
		}

		if ((backslash = strchr(name, '\\'))) {
			while (backslash) {
				backslash++;
				has_dotgit |= is_ntfs_dotgit(backslash);
				if (is_ntfs_dotgitmodules(backslash)) {
					if (!S_ISLNK(mode))
						oidset_insert(&options->gitmodules_found,
							      entry_oid);
					else
						retval += report(options, tree_oid, OBJ_TREE,
								 FSCK_MSG_GITMODULES_SYMLINK,
								 ".gitmodules is a symbolic link");
				}
				backslash = strchr(backslash, '\\');
			}
		}

		if (update_tree_entry_gently(&desc)) {
			retval += report(options, tree_oid, OBJ_TREE,
					 FSCK_MSG_BAD_TREE,
					 "cannot be parsed as a tree");
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
		retval += report(options, tree_oid, OBJ_TREE,
				 FSCK_MSG_NULL_SHA1,
				 "contains entries pointing to null sha1");
	if (has_full_path)
		retval += report(options, tree_oid, OBJ_TREE,
				 FSCK_MSG_FULL_PATHNAME,
				 "contains full pathnames");
	if (has_empty_name)
		retval += report(options, tree_oid, OBJ_TREE,
				 FSCK_MSG_EMPTY_NAME,
				 "contains empty pathname");
	if (has_dot)
		retval += report(options, tree_oid, OBJ_TREE,
				 FSCK_MSG_HAS_DOT,
				 "contains '.'");
	if (has_dotdot)
		retval += report(options, tree_oid, OBJ_TREE,
				 FSCK_MSG_HAS_DOTDOT,
				 "contains '..'");
	if (has_dotgit)
		retval += report(options, tree_oid, OBJ_TREE,
				 FSCK_MSG_HAS_DOTGIT,
				 "contains '.git'");
	if (has_zero_pad)
		retval += report(options, tree_oid, OBJ_TREE,
				 FSCK_MSG_ZERO_PADDED_FILEMODE,
				 "contains zero-padded file modes");
	if (has_bad_modes)
		retval += report(options, tree_oid, OBJ_TREE,
				 FSCK_MSG_BAD_FILEMODE,
				 "contains bad file modes");
	if (has_dup_entries)
		retval += report(options, tree_oid, OBJ_TREE,
				 FSCK_MSG_DUPLICATE_ENTRIES,
				 "contains duplicate file entries");
	if (not_properly_sorted)
		retval += report(options, tree_oid, OBJ_TREE,
				 FSCK_MSG_TREE_NOT_SORTED,
				 "not properly sorted");
	if (has_large_name)
		retval += report(options, tree_oid, OBJ_TREE,
				 FSCK_MSG_LARGE_PATHNAME,
				 "contains excessively large pathname");
	return retval;
}

/*
 * Confirm that the headers of a commit or tag object end in a reasonable way,
 * either with the usual "\n\n" separator, or at least with a trailing newline
 * on the final header line.
 *
 * This property is important for the memory safety of our callers. It allows
 * them to scan the buffer linewise without constantly checking the remaining
 * size as long as:
 *
 *   - they check that there are bytes left in the buffer at the start of any
 *     line (i.e., that the last newline they saw was not the final one we
 *     found here)
 *
 *   - any intra-line scanning they do will stop at a newline, which will worst
 *     case hit the newline we found here as the end-of-header. This makes it
 *     OK for them to use helpers like parse_oid_hex(), or even skip_prefix().
 */
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
	/*
	 * Our timestamp parser is based on the C strto*() functions, which
	 * will happily eat whitespace, including the newline that is supposed
	 * to prevent us walking past the end of the buffer. So do our own
	 * scan, skipping linear whitespace but not newlines, and then
	 * confirming we found a digit. We _could_ be even more strict here,
	 * as we really expect only a single space, but since we have
	 * traditionally allowed extra whitespace, we'll continue to do so.
	 */
	while (*p == ' ' || *p == '\t')
		p++;
	if (!isdigit(*p))
		return report(options, oid, type, FSCK_MSG_BAD_DATE,
			      "invalid author/committer line - bad date");
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
	const char *buffer_end = buffer + size;
	const char *p;

	/*
	 * We _must_ stop parsing immediately if this reports failure, as the
	 * memory safety of the rest of the function depends on it. See the
	 * comment above the definition of verify_headers() for more details.
	 */
	if (verify_headers(buffer, size, oid, OBJ_COMMIT, options))
		return -1;

	if (buffer >= buffer_end || !skip_prefix(buffer, "tree ", &buffer))
		return report(options, oid, OBJ_COMMIT, FSCK_MSG_MISSING_TREE, "invalid format - expected 'tree' line");
	if (parse_oid_hex(buffer, &tree_oid, &p) || *p != '\n') {
		err = report(options, oid, OBJ_COMMIT, FSCK_MSG_BAD_TREE_SHA1, "invalid 'tree' line format - bad sha1");
		if (err)
			return err;
	}
	buffer = p + 1;
	while (buffer < buffer_end && skip_prefix(buffer, "parent ", &buffer)) {
		if (parse_oid_hex(buffer, &parent_oid, &p) || *p != '\n') {
			err = report(options, oid, OBJ_COMMIT, FSCK_MSG_BAD_PARENT_SHA1, "invalid 'parent' line format - bad sha1");
			if (err)
				return err;
		}
		buffer = p + 1;
	}
	author_count = 0;
	while (buffer < buffer_end && skip_prefix(buffer, "author ", &buffer)) {
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
	if (buffer >= buffer_end || !skip_prefix(buffer, "committer ", &buffer))
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
	const char *buffer_end = buffer + size;
	const char *p;

	/*
	 * We _must_ stop parsing immediately if this reports failure, as the
	 * memory safety of the rest of the function depends on it. See the
	 * comment above the definition of verify_headers() for more details.
	 */
	ret = verify_headers(buffer, size, oid, OBJ_TAG, options);
	if (ret)
		goto done;

	if (buffer >= buffer_end || !skip_prefix(buffer, "object ", &buffer)) {
		ret = report(options, oid, OBJ_TAG, FSCK_MSG_MISSING_OBJECT, "invalid format - expected 'object' line");
		goto done;
	}
	if (parse_oid_hex(buffer, tagged_oid, &p) || *p != '\n') {
		ret = report(options, oid, OBJ_TAG, FSCK_MSG_BAD_OBJECT_SHA1, "invalid 'object' line format - bad sha1");
		if (ret)
			goto done;
	}
	buffer = p + 1;

	if (buffer >= buffer_end || !skip_prefix(buffer, "type ", &buffer)) {
		ret = report(options, oid, OBJ_TAG, FSCK_MSG_MISSING_TYPE_ENTRY, "invalid format - expected 'type' line");
		goto done;
	}
	eol = memchr(buffer, '\n', buffer_end - buffer);
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

	if (buffer >= buffer_end || !skip_prefix(buffer, "tag ", &buffer)) {
		ret = report(options, oid, OBJ_TAG, FSCK_MSG_MISSING_TAG_ENTRY, "invalid format - expected 'tag' line");
		goto done;
	}
	eol = memchr(buffer, '\n', buffer_end - buffer);
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

	if (buffer >= buffer_end || !skip_prefix(buffer, "tagger ", &buffer)) {
		/* early tags do not contain 'tagger' lines; warn only */
		ret = report(options, oid, OBJ_TAG, FSCK_MSG_MISSING_TAGGER_ENTRY, "invalid format - expected 'tagger' line");
		if (ret)
			goto done;
	}
	else
		ret = fsck_ident(&buffer, oid, OBJ_TAG, options);

	if (buffer < buffer_end && !starts_with(buffer, "\n")) {
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

struct fsck_gitmodules_data {
	const struct object_id *oid;
	struct fsck_options *options;
	int ret;
};

static int fsck_gitmodules_fn(const char *var, const char *value,
			      const struct config_context *ctx UNUSED,
			      void *vdata)
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
	int ret = 0;

	if (object_on_skiplist(options, oid))
		return 0;

	if (oidset_contains(&options->gitmodules_found, oid)) {
		struct config_options config_opts = { 0 };
		struct fsck_gitmodules_data data;

		oidset_insert(&options->gitmodules_done, oid);

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
					".gitmodules", buf, size, &data,
					CONFIG_SCOPE_UNKNOWN, &config_opts))
			data.ret |= report(options, oid, OBJ_BLOB,
					FSCK_MSG_GITMODULES_PARSE,
					"could not parse gitmodules blob");
		ret |= data.ret;
	}

	if (oidset_contains(&options->gitattributes_found, oid)) {
		const char *ptr;

		oidset_insert(&options->gitattributes_done, oid);

		if (!buf || size > ATTR_MAX_FILE_SIZE) {
			/*
			 * A missing buffer here is a sign that the caller found the
			 * blob too gigantic to load into memory. Let's just consider
			 * that an error.
			 */
			return report(options, oid, OBJ_BLOB,
					FSCK_MSG_GITATTRIBUTES_LARGE,
					".gitattributes too large to parse");
		}

		for (ptr = buf; *ptr; ) {
			const char *eol = strchrnul(ptr, '\n');
			if (eol - ptr >= ATTR_MAX_LINE_LENGTH) {
				ret |= report(options, oid, OBJ_BLOB,
					      FSCK_MSG_GITATTRIBUTES_LINE_LENGTH,
					      ".gitattributes has too long lines to parse");
				break;
			}

			ptr = *eol ? eol + 1 : eol;
		}
	}

	return ret;
}

int fsck_object(struct object *obj, void *data, unsigned long size,
	struct fsck_options *options)
{
	if (!obj)
		return report(options, NULL, OBJ_NONE, FSCK_MSG_BAD_OBJECT_SHA1, "no valid object to fsck");

	return fsck_buffer(&obj->oid, obj->type, data, size, options);
}

int fsck_buffer(const struct object_id *oid, enum object_type type,
		void *data, unsigned long size,
		struct fsck_options *options)
{
	if (type == OBJ_BLOB)
		return fsck_blob(oid, data, size, options);
	if (type == OBJ_TREE)
		return fsck_tree(oid, data, size, options);
	if (type == OBJ_COMMIT)
		return fsck_commit(oid, data, size, options);
	if (type == OBJ_TAG)
		return fsck_tag(oid, data, size, options);

	return report(options, oid, type,
		      FSCK_MSG_UNKNOWN_TYPE,
		      "unknown type '%d' (internal fsck error)",
		      type);
}

int fsck_error_function(struct fsck_options *o,
			const struct object_id *oid,
			enum object_type object_type UNUSED,
			enum fsck_msg_type msg_type,
			enum fsck_msg_id msg_id UNUSED,
			const char *message)
{
	if (msg_type == FSCK_WARN) {
		warning("object %s: %s", fsck_describe_object(o, oid), message);
		return 0;
	}
	error("object %s: %s", fsck_describe_object(o, oid), message);
	return 1;
}

static int fsck_blobs(struct oidset *blobs_found, struct oidset *blobs_done,
		      enum fsck_msg_id msg_missing, enum fsck_msg_id msg_type,
		      struct fsck_options *options, const char *blob_type)
{
	int ret = 0;
	struct oidset_iter iter;
	const struct object_id *oid;

	oidset_iter_init(blobs_found, &iter);
	while ((oid = oidset_iter_next(&iter))) {
		enum object_type type;
		unsigned long size;
		char *buf;

		if (oidset_contains(blobs_done, oid))
			continue;

		buf = repo_read_object_file(the_repository, oid, &type, &size);
		if (!buf) {
			if (is_promisor_object(oid))
				continue;
			ret |= report(options,
				      oid, OBJ_BLOB, msg_missing,
				      "unable to read %s blob", blob_type);
			continue;
		}

		if (type == OBJ_BLOB)
			ret |= fsck_blob(oid, buf, size, options);
		else
			ret |= report(options, oid, type, msg_type,
				      "non-blob found at %s", blob_type);
		free(buf);
	}

	oidset_clear(blobs_found);
	oidset_clear(blobs_done);

	return ret;
}

int fsck_finish(struct fsck_options *options)
{
	int ret = 0;

	ret |= fsck_blobs(&options->gitmodules_found, &options->gitmodules_done,
			  FSCK_MSG_GITMODULES_MISSING, FSCK_MSG_GITMODULES_BLOB,
			  options, ".gitmodules");
	ret |= fsck_blobs(&options->gitattributes_found, &options->gitattributes_done,
			  FSCK_MSG_GITATTRIBUTES_MISSING, FSCK_MSG_GITATTRIBUTES_BLOB,
			  options, ".gitattributes");

	return ret;
}

int git_fsck_config(const char *var, const char *value,
		    const struct config_context *ctx, void *cb)
{
	struct fsck_options *options = cb;
	const char *msg_id;

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

	if (skip_prefix(var, "fsck.", &msg_id)) {
		if (!value)
			return config_error_nonbool(var);
		fsck_set_msg_type(options, msg_id, value);
		return 0;
	}

	return git_default_config(var, value, ctx, cb);
}

/*
 * Custom error callbacks that are used in more than one place.
 */

int fsck_error_cb_print_missing_gitmodules(struct fsck_options *o,
					   const struct object_id *oid,
					   enum object_type object_type,
					   enum fsck_msg_type msg_type,
					   enum fsck_msg_id msg_id,
					   const char *message)
{
	if (msg_id == FSCK_MSG_GITMODULES_MISSING) {
		puts(oid_to_hex(oid));
		return 0;
	}
	return fsck_error_function(o, oid, object_type, msg_type, msg_id, message);
}
