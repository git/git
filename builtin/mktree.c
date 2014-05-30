/*
 * GIT - the stupid content tracker
 *
 * Copyright (c) Junio C Hamano, 2006, 2009
 */
#include "builtin.h"
#include "quote.h"
#include "tree.h"
#include "parse-options.h"

static struct treeent {
	unsigned mode;
	unsigned char sha1[20];
	int len;
	char name[FLEX_ARRAY];
} **entries;
static int alloc, used;

static void append_to_tree(unsigned mode, unsigned char *sha1, char *path)
{
	struct treeent *ent;
	int len = strlen(path);
	if (strchr(path, '/'))
		die("path %s contains slash", path);

	ALLOC_GROW(entries, used + 1, alloc);
	ent = entries[used++] = xmalloc(sizeof(**entries) + len + 1);
	ent->mode = mode;
	ent->len = len;
	hashcpy(ent->sha1, sha1);
	memcpy(ent->name, path, len+1);
}

static int ent_compare(const void *a_, const void *b_)
{
	struct treeent *a = *(struct treeent **)a_;
	struct treeent *b = *(struct treeent **)b_;
	return base_name_compare(a->name, a->len, a->mode,
				 b->name, b->len, b->mode);
}

static void write_tree(unsigned char *sha1)
{
	struct strbuf buf;
	size_t size;
	int i;

	qsort(entries, used, sizeof(*entries), ent_compare);
	for (size = i = 0; i < used; i++)
		size += 32 + entries[i]->len;

	strbuf_init(&buf, size);
	for (i = 0; i < used; i++) {
		struct treeent *ent = entries[i];
		strbuf_addf(&buf, "%o %s%c", ent->mode, ent->name, '\0');
		strbuf_add(&buf, ent->sha1, 20);
	}

	write_sha1_file(buf.buf, buf.len, tree_type, sha1);
	strbuf_release(&buf);
}

static const char *mktree_usage[] = {
	N_("git mktree [-z] [--missing] [--batch]"),
	NULL
};

static void mktree_line(char *buf, size_t len, int line_termination, int allow_missing)
{
	char *ptr, *ntr;
	unsigned mode;
	enum object_type mode_type; /* object type derived from mode */
	enum object_type obj_type; /* object type derived from sha */
	char *path;
	unsigned char sha1[20];

	ptr = buf;
	/*
	 * Read non-recursive ls-tree output format:
	 *     mode SP type SP sha1 TAB name
	 */
	mode = strtoul(ptr, &ntr, 8);
	if (ptr == ntr || !ntr || *ntr != ' ')
		die("input format error: %s", buf);
	ptr = ntr + 1; /* type */
	ntr = strchr(ptr, ' ');
	if (!ntr || buf + len <= ntr + 40 ||
	    ntr[41] != '\t' ||
	    get_sha1_hex(ntr + 1, sha1))
		die("input format error: %s", buf);

	/* It is perfectly normal if we do not have a commit from a submodule */
	if (S_ISGITLINK(mode))
		allow_missing = 1;


	*ntr++ = 0; /* now at the beginning of SHA1 */

	path = ntr + 41;  /* at the beginning of name */
	if (line_termination && path[0] == '"') {
		struct strbuf p_uq = STRBUF_INIT;
		if (unquote_c_style(&p_uq, path, NULL))
			die("invalid quoting");
		path = strbuf_detach(&p_uq, NULL);
	}

	/*
	 * Object type is redundantly derivable three ways.
	 * These should all agree.
	 */
	mode_type = object_type(mode);
	if (mode_type != type_from_string(ptr)) {
		die("entry '%s' object type (%s) doesn't match mode type (%s)",
			path, ptr, typename(mode_type));
	}

	/* Check the type of object identified by sha1 */
	obj_type = sha1_object_info(sha1, NULL);
	if (obj_type < 0) {
		if (allow_missing) {
			; /* no problem - missing objects are presumed to be of the right type */
		} else {
			die("entry '%s' object %s is unavailable", path, sha1_to_hex(sha1));
		}
	} else {
		if (obj_type != mode_type) {
			/*
			 * The object exists but is of the wrong type.
			 * This is a problem regardless of allow_missing
			 * because the new tree entry will never be correct.
			 */
			die("entry '%s' object %s is a %s but specified type was (%s)",
				path, sha1_to_hex(sha1), typename(obj_type), typename(mode_type));
		}
	}

	append_to_tree(mode, sha1, path);
}

int cmd_mktree(int ac, const char **av, const char *prefix)
{
	struct strbuf sb = STRBUF_INIT;
	unsigned char sha1[20];
	int line_termination = '\n';
	int allow_missing = 0;
	int is_batch_mode = 0;
	int got_eof = 0;

	const struct option option[] = {
		OPT_SET_INT('z', NULL, &line_termination, N_("input is NUL terminated"), '\0'),
		OPT_SET_INT( 0 , "missing", &allow_missing, N_("allow missing objects"), 1),
		OPT_SET_INT( 0 , "batch", &is_batch_mode, N_("allow creation of more than one tree"), 1),
		OPT_END()
	};

	ac = parse_options(ac, av, prefix, option, mktree_usage, 0);

	while (!got_eof) {
		while (1) {
			if (strbuf_getline(&sb, stdin, line_termination) == EOF) {
				got_eof = 1;
				break;
			}
			if (sb.buf[0] == '\0') {
				/* empty lines denote tree boundaries in batch mode */
				if (is_batch_mode)
					break;
				die("input format error: (blank line only valid in batch mode)");
			}
			mktree_line(sb.buf, sb.len, line_termination, allow_missing);
		}
		if (is_batch_mode && got_eof && used < 1) {
			/*
			 * Execution gets here if the last tree entry is terminated with a
			 * new-line.  The final new-line has been made optional to be
			 * consistent with the original non-batch behaviour of mktree.
			 */
			; /* skip creating an empty tree */
		} else {
			write_tree(sha1);
			puts(sha1_to_hex(sha1));
			fflush(stdout);
		}
		used=0; /* reset tree entry buffer for re-use in batch mode */
	}
	strbuf_release(&sb);
	exit(0);
}
