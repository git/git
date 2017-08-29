/*
 * GIT - the stupid content tracker
 *
 * Copyright (c) Junio C Hamano, 2006, 2009
 */
#include "builtin.h"
#include "quote.h"
#include "tree.h"
#include "parse-options.h"
#include "object-store.h"
#include "config.h"

static struct treeent {
	unsigned mode;
	struct object_id oid;
	int len;
	char name[FLEX_ARRAY];
} **entries;
static int alloc, used;

static void append_to_tree(unsigned mode, struct object_id *oid, char *path)
{
	struct treeent *ent;
	size_t len = strlen(path);
	if (strchr(path, '/'))
		die("path %s contains slash", path);

	FLEX_ALLOC_MEM(ent, name, path, len);
	ent->mode = mode;
	ent->len = len;
	oidcpy(&ent->oid, oid);

	ALLOC_GROW(entries, used + 1, alloc);
	entries[used++] = ent;
}

static int ent_compare(const void *a_, const void *b_)
{
	struct treeent *a = *(struct treeent **)a_;
	struct treeent *b = *(struct treeent **)b_;
	return base_name_compare(a->name, a->len, a->mode,
				 b->name, b->len, b->mode);
}

static void write_tree(struct object_id *oid)
{
	struct strbuf buf;
	size_t size;
	int i;

	QSORT(entries, used, ent_compare);
	for (size = i = 0; i < used; i++)
		size += 32 + entries[i]->len;

	strbuf_init(&buf, size);
	for (i = 0; i < used; i++) {
		struct treeent *ent = entries[i];
		strbuf_addf(&buf, "%o %s%c", ent->mode, ent->name, '\0');
		strbuf_add(&buf, ent->oid.hash, the_hash_algo->rawsz);
	}

	write_object_file(buf.buf, buf.len, tree_type, oid);
	strbuf_release(&buf);
}

static const char *mktree_usage[] = {
	N_("git mktree [-z] [--missing] [--batch]"),
	NULL
};

static void mktree_line(char *buf, int nul_term_line, int allow_missing)
{
	char *ptr, *ntr;
	const char *p;
	unsigned mode;
	enum object_type mode_type; /* object type derived from mode */
	enum object_type obj_type; /* object type derived from sha */
	char *path, *to_free = NULL;
	struct object_id oid;

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
	if (!ntr || parse_oid_hex(ntr + 1, &oid, &p) ||
	    *p != '\t')
		die("input format error: %s", buf);

	/* It is perfectly normal if we do not have a commit from a submodule */
	if (S_ISGITLINK(mode))
		allow_missing = 1;


	*ntr++ = 0; /* now at the beginning of SHA1 */

	path = (char *)p + 1;  /* at the beginning of name */
	if (!nul_term_line && path[0] == '"') {
		struct strbuf p_uq = STRBUF_INIT;
		if (unquote_c_style(&p_uq, path, NULL))
			die("invalid quoting");
		path = to_free = strbuf_detach(&p_uq, NULL);
	}

	/*
	 * Object type is redundantly derivable three ways.
	 * These should all agree.
	 */
	mode_type = object_type(mode);
	if (mode_type != type_from_string(ptr)) {
		die("entry '%s' object type (%s) doesn't match mode type (%s)",
			path, ptr, type_name(mode_type));
	}

	/* Check the type of object identified by sha1 */
	obj_type = oid_object_info(the_repository, &oid, NULL);
	if (obj_type < 0) {
		if (allow_missing) {
			; /* no problem - missing objects are presumed to be of the right type */
		} else {
			die("entry '%s' object %s is unavailable", path, oid_to_hex(&oid));
		}
	} else {
		if (obj_type != mode_type) {
			/*
			 * The object exists but is of the wrong type.
			 * This is a problem regardless of allow_missing
			 * because the new tree entry will never be correct.
			 */
			die("entry '%s' object %s is a %s but specified type was (%s)",
				path, oid_to_hex(&oid), type_name(obj_type), type_name(mode_type));
		}
	}

	append_to_tree(mode, &oid, path);
	free(to_free);
}

int cmd_mktree(int ac, const char **av, const char *prefix)
{
	struct strbuf sb = STRBUF_INIT;
	struct object_id oid;
	int nul_term_line = 0;
	int allow_missing = 0;
	int is_batch_mode = 0;
	int got_eof = 0;
	strbuf_getline_fn getline_fn;

	const struct option option[] = {
		OPT_BOOL('z', NULL, &nul_term_line, N_("input is NUL terminated")),
		OPT_SET_INT( 0 , "missing", &allow_missing, N_("allow missing objects"), 1),
		OPT_SET_INT( 0 , "batch", &is_batch_mode, N_("allow creation of more than one tree"), 1),
		OPT_END()
	};

	git_config(git_default_config, NULL);
	ac = parse_options(ac, av, prefix, option, mktree_usage, 0);
	getline_fn = nul_term_line ? strbuf_getline_nul : strbuf_getline_lf;

	while (!got_eof) {
		while (1) {
			if (getline_fn(&sb, stdin) == EOF) {
				got_eof = 1;
				break;
			}
			if (sb.buf[0] == '\0') {
				/* empty lines denote tree boundaries in batch mode */
				if (is_batch_mode)
					break;
				die("input format error: (blank line only valid in batch mode)");
			}
			mktree_line(sb.buf, nul_term_line, allow_missing);
		}
		if (is_batch_mode && got_eof && used < 1) {
			/*
			 * Execution gets here if the last tree entry is terminated with a
			 * new-line.  The final new-line has been made optional to be
			 * consistent with the original non-batch behaviour of mktree.
			 */
			; /* skip creating an empty tree */
		} else {
			write_tree(&oid);
			puts(oid_to_hex(&oid));
			fflush(stdout);
		}
		used=0; /* reset tree entry buffer for re-use in batch mode */
	}
	strbuf_release(&sb);
	exit(0);
}
