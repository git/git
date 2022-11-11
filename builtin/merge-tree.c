#define USE_THE_INDEX_COMPATIBILITY_MACROS
#include "builtin.h"
#include "tree-walk.h"
#include "xdiff-interface.h"
#include "help.h"
#include "commit.h"
#include "commit-reach.h"
#include "merge-ort.h"
#include "object-store.h"
#include "parse-options.h"
#include "repository.h"
#include "blob.h"
#include "exec-cmd.h"
#include "merge-blobs.h"
#include "quote.h"

static int line_termination = '\n';

struct merge_list {
	struct merge_list *next;
	struct merge_list *link;	/* other stages for this object */

	unsigned int stage : 2;
	unsigned int mode;
	const char *path;
	struct blob *blob;
};

static struct merge_list *merge_result, **merge_result_end = &merge_result;

static void add_merge_entry(struct merge_list *entry)
{
	*merge_result_end = entry;
	merge_result_end = &entry->next;
}

static void trivial_merge_trees(struct tree_desc t[3], const char *base);

static const char *explanation(struct merge_list *entry)
{
	switch (entry->stage) {
	case 0:
		return "merged";
	case 3:
		return "added in remote";
	case 2:
		if (entry->link)
			return "added in both";
		return "added in local";
	}

	/* Existed in base */
	entry = entry->link;
	if (!entry)
		return "removed in both";

	if (entry->link)
		return "changed in both";

	if (entry->stage == 3)
		return "removed in local";
	return "removed in remote";
}

static void *result(struct merge_list *entry, unsigned long *size)
{
	enum object_type type;
	struct blob *base, *our, *their;
	const char *path = entry->path;

	if (!entry->stage)
		return read_object_file(&entry->blob->object.oid, &type, size);
	base = NULL;
	if (entry->stage == 1) {
		base = entry->blob;
		entry = entry->link;
	}
	our = NULL;
	if (entry && entry->stage == 2) {
		our = entry->blob;
		entry = entry->link;
	}
	their = NULL;
	if (entry)
		their = entry->blob;
	return merge_blobs(the_repository->index, path,
			   base, our, their, size);
}

static void *origin(struct merge_list *entry, unsigned long *size)
{
	enum object_type type;
	while (entry) {
		if (entry->stage == 2)
			return read_object_file(&entry->blob->object.oid,
						&type, size);
		entry = entry->link;
	}
	return NULL;
}

static int show_outf(void *priv_, mmbuffer_t *mb, int nbuf)
{
	int i;
	for (i = 0; i < nbuf; i++)
		printf("%.*s", (int) mb[i].size, mb[i].ptr);
	return 0;
}

static void show_diff(struct merge_list *entry)
{
	unsigned long size;
	mmfile_t src, dst;
	xpparam_t xpp;
	xdemitconf_t xecfg;
	xdemitcb_t ecb = { .out_line = show_outf };

	memset(&xpp, 0, sizeof(xpp));
	xpp.flags = 0;
	memset(&xecfg, 0, sizeof(xecfg));
	xecfg.ctxlen = 3;

	src.ptr = origin(entry, &size);
	if (!src.ptr)
		size = 0;
	src.size = size;
	dst.ptr = result(entry, &size);
	if (!dst.ptr)
		size = 0;
	dst.size = size;
	if (xdi_diff(&src, &dst, &xpp, &xecfg, &ecb))
		die("unable to generate diff");
	free(src.ptr);
	free(dst.ptr);
}

static void show_result_list(struct merge_list *entry)
{
	printf("%s\n", explanation(entry));
	do {
		struct merge_list *link = entry->link;
		static const char *desc[4] = { "result", "base", "our", "their" };
		printf("  %-6s %o %s %s\n", desc[entry->stage], entry->mode, oid_to_hex(&entry->blob->object.oid), entry->path);
		entry = link;
	} while (entry);
}

static void show_result(void)
{
	struct merge_list *walk;

	walk = merge_result;
	while (walk) {
		show_result_list(walk);
		show_diff(walk);
		walk = walk->next;
	}
}

/* An empty entry never compares same, not even to another empty entry */
static int same_entry(struct name_entry *a, struct name_entry *b)
{
	return	!is_null_oid(&a->oid) &&
		!is_null_oid(&b->oid) &&
		oideq(&a->oid, &b->oid) &&
		a->mode == b->mode;
}

static int both_empty(struct name_entry *a, struct name_entry *b)
{
	return is_null_oid(&a->oid) && is_null_oid(&b->oid);
}

static struct merge_list *create_entry(unsigned stage, unsigned mode, const struct object_id *oid, const char *path)
{
	struct merge_list *res = xcalloc(1, sizeof(*res));

	res->stage = stage;
	res->path = path;
	res->mode = mode;
	res->blob = lookup_blob(the_repository, oid);
	return res;
}

static char *traverse_path(const struct traverse_info *info, const struct name_entry *n)
{
	struct strbuf buf = STRBUF_INIT;
	strbuf_make_traverse_path(&buf, info, n->path, n->pathlen);
	return strbuf_detach(&buf, NULL);
}

static void resolve(const struct traverse_info *info, struct name_entry *ours, struct name_entry *result)
{
	struct merge_list *orig, *final;
	const char *path;

	/* If it's already ours, don't bother showing it */
	if (!ours)
		return;

	path = traverse_path(info, result);
	orig = create_entry(2, ours->mode, &ours->oid, path);
	final = create_entry(0, result->mode, &result->oid, path);

	final->link = orig;

	add_merge_entry(final);
}

static void unresolved_directory(const struct traverse_info *info,
				 struct name_entry n[3])
{
	struct repository *r = the_repository;
	char *newbase;
	struct name_entry *p;
	struct tree_desc t[3];
	void *buf0, *buf1, *buf2;

	for (p = n; p < n + 3; p++) {
		if (p->mode && S_ISDIR(p->mode))
			break;
	}
	if (n + 3 <= p)
		return; /* there is no tree here */

	newbase = traverse_path(info, p);

#define ENTRY_OID(e) (((e)->mode && S_ISDIR((e)->mode)) ? &(e)->oid : NULL)
	buf0 = fill_tree_descriptor(r, t + 0, ENTRY_OID(n + 0));
	buf1 = fill_tree_descriptor(r, t + 1, ENTRY_OID(n + 1));
	buf2 = fill_tree_descriptor(r, t + 2, ENTRY_OID(n + 2));
#undef ENTRY_OID

	trivial_merge_trees(t, newbase);

	free(buf0);
	free(buf1);
	free(buf2);
	free(newbase);
}


static struct merge_list *link_entry(unsigned stage, const struct traverse_info *info, struct name_entry *n, struct merge_list *entry)
{
	const char *path;
	struct merge_list *link;

	if (!n->mode)
		return entry;
	if (entry)
		path = entry->path;
	else
		path = traverse_path(info, n);
	link = create_entry(stage, n->mode, &n->oid, path);
	link->link = entry;
	return link;
}

static void unresolved(const struct traverse_info *info, struct name_entry n[3])
{
	struct merge_list *entry = NULL;
	int i;
	unsigned dirmask = 0, mask = 0;

	for (i = 0; i < 3; i++) {
		mask |= (1 << i);
		/*
		 * Treat missing entries as directories so that we return
		 * after unresolved_directory has handled this.
		 */
		if (!n[i].mode || S_ISDIR(n[i].mode))
			dirmask |= (1 << i);
	}

	unresolved_directory(info, n);

	if (dirmask == mask)
		return;

	if (n[2].mode && !S_ISDIR(n[2].mode))
		entry = link_entry(3, info, n + 2, entry);
	if (n[1].mode && !S_ISDIR(n[1].mode))
		entry = link_entry(2, info, n + 1, entry);
	if (n[0].mode && !S_ISDIR(n[0].mode))
		entry = link_entry(1, info, n + 0, entry);

	add_merge_entry(entry);
}

/*
 * Merge two trees together (t[1] and t[2]), using a common base (t[0])
 * as the origin.
 *
 * This walks the (sorted) trees in lock-step, checking every possible
 * name. Note that directories automatically sort differently from other
 * files (see "base_name_compare"), so you'll never see file/directory
 * conflicts, because they won't ever compare the same.
 *
 * IOW, if a directory changes to a filename, it will automatically be
 * seen as the directory going away, and the filename being created.
 *
 * Think of this as a three-way diff.
 *
 * The output will be either:
 *  - successful merge
 *	 "0 mode sha1 filename"
 *    NOTE NOTE NOTE! FIXME! We really really need to walk the index
 *    in parallel with this too!
 *
 *  - conflict:
 *	"1 mode sha1 filename"
 *	"2 mode sha1 filename"
 *	"3 mode sha1 filename"
 *    where not all of the 1/2/3 lines may exist, of course.
 *
 * The successful merge rules are the same as for the three-way merge
 * in git-read-tree.
 */
static int threeway_callback(int n, unsigned long mask, unsigned long dirmask, struct name_entry *entry, struct traverse_info *info)
{
	/* Same in both? */
	if (same_entry(entry+1, entry+2) || both_empty(entry+1, entry+2)) {
		/* Modified, added or removed identically */
		resolve(info, NULL, entry+1);
		return mask;
	}

	if (same_entry(entry+0, entry+1)) {
		if (!is_null_oid(&entry[2].oid) && !S_ISDIR(entry[2].mode)) {
			/* We did not touch, they modified -- take theirs */
			resolve(info, entry+1, entry+2);
			return mask;
		}
		/*
		 * If we did not touch a directory but they made it
		 * into a file, we fall through and unresolved()
		 * recurses down.  Likewise for the opposite case.
		 */
	}

	if (same_entry(entry+0, entry+2) || both_empty(entry+0, entry+2)) {
		/* We added, modified or removed, they did not touch -- take ours */
		resolve(info, NULL, entry+1);
		return mask;
	}

	unresolved(info, entry);
	return mask;
}

static void trivial_merge_trees(struct tree_desc t[3], const char *base)
{
	struct traverse_info info;

	setup_traverse_info(&info, base);
	info.fn = threeway_callback;
	traverse_trees(&the_index, 3, t, &info);
}

static void *get_tree_descriptor(struct repository *r,
				 struct tree_desc *desc,
				 const char *rev)
{
	struct object_id oid;
	void *buf;

	if (repo_get_oid(r, rev, &oid))
		die("unknown rev %s", rev);
	buf = fill_tree_descriptor(r, desc, &oid);
	if (!buf)
		die("%s is not a tree", rev);
	return buf;
}

static int trivial_merge(const char *base,
			 const char *branch1,
			 const char *branch2)
{
	struct repository *r = the_repository;
	struct tree_desc t[3];
	void *buf1, *buf2, *buf3;

	buf1 = get_tree_descriptor(r, t+0, base);
	buf2 = get_tree_descriptor(r, t+1, branch1);
	buf3 = get_tree_descriptor(r, t+2, branch2);
	trivial_merge_trees(t, "");
	free(buf1);
	free(buf2);
	free(buf3);

	show_result();
	return 0;
}

enum mode {
	MODE_UNKNOWN,
	MODE_TRIVIAL,
	MODE_REAL,
};

struct merge_tree_options {
	int mode;
	int allow_unrelated_histories;
	int show_messages;
	int name_only;
	int use_stdin;
};

static int real_merge(struct merge_tree_options *o,
		      const char *merge_base,
		      const char *branch1, const char *branch2,
		      const char *prefix)
{
	struct commit *parent1, *parent2;
	struct commit_list *merge_bases = NULL;
	struct merge_options opt;
	struct merge_result result = { 0 };
	int show_messages = o->show_messages;

	parent1 = get_merge_parent(branch1);
	if (!parent1)
		help_unknown_ref(branch1, "merge-tree",
				 _("not something we can merge"));

	parent2 = get_merge_parent(branch2);
	if (!parent2)
		help_unknown_ref(branch2, "merge-tree",
				 _("not something we can merge"));

	init_merge_options(&opt, the_repository);

	opt.show_rename_progress = 0;

	opt.branch1 = branch1;
	opt.branch2 = branch2;

	if (merge_base) {
		struct commit *base_commit;
		struct tree *base_tree, *parent1_tree, *parent2_tree;

		base_commit = lookup_commit_reference_by_name(merge_base);
		if (!base_commit)
			die(_("could not lookup commit %s"), merge_base);

		opt.ancestor = merge_base;
		base_tree = get_commit_tree(base_commit);
		parent1_tree = get_commit_tree(parent1);
		parent2_tree = get_commit_tree(parent2);
		merge_incore_nonrecursive(&opt, base_tree, parent1_tree, parent2_tree, &result);
	} else {
		/*
		 * Get the merge bases, in reverse order; see comment above
		 * merge_incore_recursive in merge-ort.h
		 */
		merge_bases = get_merge_bases(parent1, parent2);
		if (!merge_bases && !o->allow_unrelated_histories)
			die(_("refusing to merge unrelated histories"));
		merge_bases = reverse_commit_list(merge_bases);
		merge_incore_recursive(&opt, merge_bases, parent1, parent2, &result);
	}

	if (result.clean < 0)
		die(_("failure to merge"));

	if (show_messages == -1)
		show_messages = !result.clean;

	if (o->use_stdin)
		printf("%d%c", result.clean, line_termination);
	printf("%s%c", oid_to_hex(&result.tree->object.oid), line_termination);
	if (!result.clean) {
		struct string_list conflicted_files = STRING_LIST_INIT_NODUP;
		const char *last = NULL;
		int i;

		merge_get_conflicted_files(&result, &conflicted_files);
		for (i = 0; i < conflicted_files.nr; i++) {
			const char *name = conflicted_files.items[i].string;
			struct stage_info *c = conflicted_files.items[i].util;
			if (!o->name_only)
				printf("%06o %s %d\t",
				       c->mode, oid_to_hex(&c->oid), c->stage);
			else if (last && !strcmp(last, name))
				continue;
			write_name_quoted_relative(
				name, prefix, stdout, line_termination);
			last = name;
		}
		string_list_clear(&conflicted_files, 1);
	}
	if (show_messages) {
		putchar(line_termination);
		merge_display_update_messages(&opt, line_termination == '\0',
					      &result);
	}
	if (o->use_stdin)
		putchar(line_termination);
	merge_finalize(&opt, &result);
	return !result.clean; /* result.clean < 0 handled above */
}

int cmd_merge_tree(int argc, const char **argv, const char *prefix)
{
	struct merge_tree_options o = { .show_messages = -1 };
	int expected_remaining_argc;
	int original_argc;
	const char *merge_base = NULL;

	const char * const merge_tree_usage[] = {
		N_("git merge-tree [--write-tree] [<options>] <branch1> <branch2>"),
		N_("git merge-tree [--trivial-merge] <base-tree> <branch1> <branch2>"),
		NULL
	};
	struct option mt_options[] = {
		OPT_CMDMODE(0, "write-tree", &o.mode,
			    N_("do a real merge instead of a trivial merge"),
			    MODE_REAL),
		OPT_CMDMODE(0, "trivial-merge", &o.mode,
			    N_("do a trivial merge only"), MODE_TRIVIAL),
		OPT_BOOL(0, "messages", &o.show_messages,
			 N_("also show informational/conflict messages")),
		OPT_SET_INT('z', NULL, &line_termination,
			    N_("separate paths with the NUL character"), '\0'),
		OPT_BOOL_F(0, "name-only",
			   &o.name_only,
			   N_("list filenames without modes/oids/stages"),
			   PARSE_OPT_NONEG),
		OPT_BOOL_F(0, "allow-unrelated-histories",
			   &o.allow_unrelated_histories,
			   N_("allow merging unrelated histories"),
			   PARSE_OPT_NONEG),
		OPT_BOOL_F(0, "stdin",
			   &o.use_stdin,
			   N_("perform multiple merges, one per line of input"),
			   PARSE_OPT_NONEG),
		OPT_STRING(0, "merge-base",
			   &merge_base,
			   N_("commit"),
			   N_("specify a merge-base for the merge")),
		OPT_END()
	};

	/* Parse arguments */
	original_argc = argc - 1; /* ignoring argv[0] */
	argc = parse_options(argc, argv, prefix, mt_options,
			     merge_tree_usage, PARSE_OPT_STOP_AT_NON_OPTION);

	/* Handle --stdin */
	if (o.use_stdin) {
		struct strbuf buf = STRBUF_INIT;

		if (o.mode == MODE_TRIVIAL)
			die(_("--trivial-merge is incompatible with all other options"));
		if (merge_base)
			die(_("--merge-base is incompatible with --stdin"));
		line_termination = '\0';
		while (strbuf_getline_lf(&buf, stdin) != EOF) {
			struct strbuf **split;
			int result;
			const char *input_merge_base = NULL;

			split = strbuf_split(&buf, ' ');
			if (!split[0] || !split[1])
				die(_("malformed input line: '%s'."), buf.buf);
			strbuf_rtrim(split[0]);
			strbuf_rtrim(split[1]);

			/* parse the merge-base */
			if (!strcmp(split[1]->buf, "--")) {
				input_merge_base = split[0]->buf;
			}

			if (input_merge_base && split[2] && split[3] && !split[4]) {
				strbuf_rtrim(split[2]);
				strbuf_rtrim(split[3]);
				result = real_merge(&o, input_merge_base, split[2]->buf, split[3]->buf, prefix);
			} else if (!input_merge_base && !split[2]) {
				result = real_merge(&o, NULL, split[0]->buf, split[1]->buf, prefix);
			} else {
				die(_("malformed input line: '%s'."), buf.buf);
			}

			if (result < 0)
				die(_("merging cannot continue; got unclean result of %d"), result);
			strbuf_list_free(split);
		}
		strbuf_release(&buf);
		return 0;
	}

	/* Figure out which mode to use */
	switch (o.mode) {
	default:
		BUG("unexpected command mode %d", o.mode);
	case MODE_UNKNOWN:
		switch (argc) {
		default:
			usage_with_options(merge_tree_usage, mt_options);
		case 2:
			o.mode = MODE_REAL;
			break;
		case 3:
			o.mode = MODE_TRIVIAL;
			break;
		}
		expected_remaining_argc = argc;
		break;
	case MODE_REAL:
		expected_remaining_argc = 2;
		break;
	case MODE_TRIVIAL:
		expected_remaining_argc = 3;
		/* Removal of `--trivial-merge` is expected */
		original_argc--;
		break;
	}
	if (o.mode == MODE_TRIVIAL && argc < original_argc)
		die(_("--trivial-merge is incompatible with all other options"));

	if (argc != expected_remaining_argc)
		usage_with_options(merge_tree_usage, mt_options);

	/* Do the relevant type of merge */
	if (o.mode == MODE_REAL)
		return real_merge(&o, merge_base, argv[0], argv[1], prefix);
	else
		return trivial_merge(argv[0], argv[1], argv[2]);
}
