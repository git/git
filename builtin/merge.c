/*
 * Builtin "git merge"
 *
 * Copyright (c) 2008 Miklos Vajna <vmiklos@frugalware.org>
 *
 * Based on git-merge.sh by Junio C Hamano.
 */

#include "cache.h"
#include "parse-options.h"
#include "builtin.h"
#include "run-command.h"
#include "diff.h"
#include "refs.h"
#include "commit.h"
#include "diffcore.h"
#include "revision.h"
#include "unpack-trees.h"
#include "cache-tree.h"
#include "dir.h"
#include "utf8.h"
#include "log-tree.h"
#include "color.h"
#include "rerere.h"
#include "help.h"
#include "merge-recursive.h"
#include "resolve-undo.h"
#include "remote.h"
#include "fmt-merge-msg.h"
#include "gpg-interface.h"

#define DEFAULT_TWOHEAD (1<<0)
#define DEFAULT_OCTOPUS (1<<1)
#define NO_FAST_FORWARD (1<<2)
#define NO_TRIVIAL      (1<<3)

struct strategy {
	const char *name;
	unsigned attr;
};

static const char * const builtin_merge_usage[] = {
	N_("git merge [options] [<commit>...]"),
	N_("git merge [options] <msg> HEAD <commit>"),
	N_("git merge --abort"),
	NULL
};

static int show_diffstat = 1, shortlog_len = -1, squash;
static int option_commit = 1;
static int option_edit = -1;
static int allow_trivial = 1, have_message, verify_signatures;
static int overwrite_ignore = 1;
static struct strbuf merge_msg = STRBUF_INIT;
static struct strategy **use_strategies;
static size_t use_strategies_nr, use_strategies_alloc;
static const char **xopts;
static size_t xopts_nr, xopts_alloc;
static const char *branch;
static char *branch_mergeoptions;
static int option_renormalize;
static int verbosity;
static int allow_rerere_auto;
static int abort_current_merge;
static int show_progress = -1;
static int default_to_upstream = 1;
static const char *sign_commit;

static struct strategy all_strategy[] = {
	{ "recursive",  DEFAULT_TWOHEAD | NO_TRIVIAL },
	{ "octopus",    DEFAULT_OCTOPUS },
	{ "resolve",    0 },
	{ "ours",       NO_FAST_FORWARD | NO_TRIVIAL },
	{ "subtree",    NO_FAST_FORWARD | NO_TRIVIAL },
};

static const char *pull_twohead, *pull_octopus;

enum ff_type {
	FF_NO,
	FF_ALLOW,
	FF_ONLY
};

static enum ff_type fast_forward = FF_ALLOW;

static int option_parse_message(const struct option *opt,
				const char *arg, int unset)
{
	struct strbuf *buf = opt->value;

	if (unset)
		strbuf_setlen(buf, 0);
	else if (arg) {
		strbuf_addf(buf, "%s%s", buf->len ? "\n\n" : "", arg);
		have_message = 1;
	} else
		return error(_("switch `m' requires a value"));
	return 0;
}

static struct strategy *get_strategy(const char *name)
{
	int i;
	struct strategy *ret;
	static struct cmdnames main_cmds, other_cmds;
	static int loaded;

	if (!name)
		return NULL;

	for (i = 0; i < ARRAY_SIZE(all_strategy); i++)
		if (!strcmp(name, all_strategy[i].name))
			return &all_strategy[i];

	if (!loaded) {
		struct cmdnames not_strategies;
		loaded = 1;

		memset(&not_strategies, 0, sizeof(struct cmdnames));
		load_command_list("git-merge-", &main_cmds, &other_cmds);
		for (i = 0; i < main_cmds.cnt; i++) {
			int j, found = 0;
			struct cmdname *ent = main_cmds.names[i];
			for (j = 0; j < ARRAY_SIZE(all_strategy); j++)
				if (!strncmp(ent->name, all_strategy[j].name, ent->len)
						&& !all_strategy[j].name[ent->len])
					found = 1;
			if (!found)
				add_cmdname(&not_strategies, ent->name, ent->len);
		}
		exclude_cmds(&main_cmds, &not_strategies);
	}
	if (!is_in_cmdlist(&main_cmds, name) && !is_in_cmdlist(&other_cmds, name)) {
		fprintf(stderr, _("Could not find merge strategy '%s'.\n"), name);
		fprintf(stderr, _("Available strategies are:"));
		for (i = 0; i < main_cmds.cnt; i++)
			fprintf(stderr, " %s", main_cmds.names[i]->name);
		fprintf(stderr, ".\n");
		if (other_cmds.cnt) {
			fprintf(stderr, _("Available custom strategies are:"));
			for (i = 0; i < other_cmds.cnt; i++)
				fprintf(stderr, " %s", other_cmds.names[i]->name);
			fprintf(stderr, ".\n");
		}
		exit(1);
	}

	ret = xcalloc(1, sizeof(struct strategy));
	ret->name = xstrdup(name);
	ret->attr = NO_TRIVIAL;
	return ret;
}

static void append_strategy(struct strategy *s)
{
	ALLOC_GROW(use_strategies, use_strategies_nr + 1, use_strategies_alloc);
	use_strategies[use_strategies_nr++] = s;
}

static int option_parse_strategy(const struct option *opt,
				 const char *name, int unset)
{
	if (unset)
		return 0;

	append_strategy(get_strategy(name));
	return 0;
}

static int option_parse_x(const struct option *opt,
			  const char *arg, int unset)
{
	if (unset)
		return 0;

	ALLOC_GROW(xopts, xopts_nr + 1, xopts_alloc);
	xopts[xopts_nr++] = xstrdup(arg);
	return 0;
}

static int option_parse_n(const struct option *opt,
			  const char *arg, int unset)
{
	show_diffstat = unset;
	return 0;
}

static struct option builtin_merge_options[] = {
	{ OPTION_CALLBACK, 'n', NULL, NULL, NULL,
		N_("do not show a diffstat at the end of the merge"),
		PARSE_OPT_NOARG, option_parse_n },
	OPT_BOOL(0, "stat", &show_diffstat,
		N_("show a diffstat at the end of the merge")),
	OPT_BOOL(0, "summary", &show_diffstat, N_("(synonym to --stat)")),
	{ OPTION_INTEGER, 0, "log", &shortlog_len, N_("n"),
	  N_("add (at most <n>) entries from shortlog to merge commit message"),
	  PARSE_OPT_OPTARG, NULL, DEFAULT_MERGE_LOG_LEN },
	OPT_BOOL(0, "squash", &squash,
		N_("create a single commit instead of doing a merge")),
	OPT_BOOL(0, "commit", &option_commit,
		N_("perform a commit if the merge succeeds (default)")),
	OPT_BOOL('e', "edit", &option_edit,
		N_("edit message before committing")),
	OPT_SET_INT(0, "ff", &fast_forward, N_("allow fast-forward (default)"), FF_ALLOW),
	{ OPTION_SET_INT, 0, "ff-only", &fast_forward, NULL,
		N_("abort if fast-forward is not possible"),
		PARSE_OPT_NOARG | PARSE_OPT_NONEG, NULL, FF_ONLY },
	OPT_RERERE_AUTOUPDATE(&allow_rerere_auto),
	OPT_BOOL(0, "verify-signatures", &verify_signatures,
		N_("Verify that the named commit has a valid GPG signature")),
	OPT_CALLBACK('s', "strategy", &use_strategies, N_("strategy"),
		N_("merge strategy to use"), option_parse_strategy),
	OPT_CALLBACK('X', "strategy-option", &xopts, N_("option=value"),
		N_("option for selected merge strategy"), option_parse_x),
	OPT_CALLBACK('m', "message", &merge_msg, N_("message"),
		N_("merge commit message (for a non-fast-forward merge)"),
		option_parse_message),
	OPT__VERBOSITY(&verbosity),
	OPT_BOOL(0, "abort", &abort_current_merge,
		N_("abort the current in-progress merge")),
	OPT_SET_INT(0, "progress", &show_progress, N_("force progress reporting"), 1),
	{ OPTION_STRING, 'S', "gpg-sign", &sign_commit, N_("key-id"),
	  N_("GPG sign commit"), PARSE_OPT_OPTARG, NULL, (intptr_t) "" },
	OPT_BOOL(0, "overwrite-ignore", &overwrite_ignore, N_("update ignored files (default)")),
	OPT_END()
};

/* Cleans up metadata that is uninteresting after a succeeded merge. */
static void drop_save(void)
{
	unlink(git_path("MERGE_HEAD"));
	unlink(git_path("MERGE_MSG"));
	unlink(git_path("MERGE_MODE"));
}

static int save_state(unsigned char *stash)
{
	int len;
	struct child_process cp;
	struct strbuf buffer = STRBUF_INIT;
	const char *argv[] = {"stash", "create", NULL};

	memset(&cp, 0, sizeof(cp));
	cp.argv = argv;
	cp.out = -1;
	cp.git_cmd = 1;

	if (start_command(&cp))
		die(_("could not run stash."));
	len = strbuf_read(&buffer, cp.out, 1024);
	close(cp.out);

	if (finish_command(&cp) || len < 0)
		die(_("stash failed"));
	else if (!len)		/* no changes */
		return -1;
	strbuf_setlen(&buffer, buffer.len-1);
	if (get_sha1(buffer.buf, stash))
		die(_("not a valid object: %s"), buffer.buf);
	return 0;
}

static void read_empty(unsigned const char *sha1, int verbose)
{
	int i = 0;
	const char *args[7];

	args[i++] = "read-tree";
	if (verbose)
		args[i++] = "-v";
	args[i++] = "-m";
	args[i++] = "-u";
	args[i++] = EMPTY_TREE_SHA1_HEX;
	args[i++] = sha1_to_hex(sha1);
	args[i] = NULL;

	if (run_command_v_opt(args, RUN_GIT_CMD))
		die(_("read-tree failed"));
}

static void reset_hard(unsigned const char *sha1, int verbose)
{
	int i = 0;
	const char *args[6];

	args[i++] = "read-tree";
	if (verbose)
		args[i++] = "-v";
	args[i++] = "--reset";
	args[i++] = "-u";
	args[i++] = sha1_to_hex(sha1);
	args[i] = NULL;

	if (run_command_v_opt(args, RUN_GIT_CMD))
		die(_("read-tree failed"));
}

static void restore_state(const unsigned char *head,
			  const unsigned char *stash)
{
	struct strbuf sb = STRBUF_INIT;
	const char *args[] = { "stash", "apply", NULL, NULL };

	if (is_null_sha1(stash))
		return;

	reset_hard(head, 1);

	args[2] = sha1_to_hex(stash);

	/*
	 * It is OK to ignore error here, for example when there was
	 * nothing to restore.
	 */
	run_command_v_opt(args, RUN_GIT_CMD);

	strbuf_release(&sb);
	refresh_cache(REFRESH_QUIET);
}

/* This is called when no merge was necessary. */
static void finish_up_to_date(const char *msg)
{
	if (verbosity >= 0)
		printf("%s%s\n", squash ? _(" (nothing to squash)") : "", msg);
	drop_save();
}

static void squash_message(struct commit *commit, struct commit_list *remoteheads)
{
	struct rev_info rev;
	struct strbuf out = STRBUF_INIT;
	struct commit_list *j;
	const char *filename;
	int fd;
	struct pretty_print_context ctx = {0};

	printf(_("Squash commit -- not updating HEAD\n"));
	filename = git_path("SQUASH_MSG");
	fd = open(filename, O_WRONLY | O_CREAT, 0666);
	if (fd < 0)
		die_errno(_("Could not write to '%s'"), filename);

	init_revisions(&rev, NULL);
	rev.ignore_merges = 1;
	rev.commit_format = CMIT_FMT_MEDIUM;

	commit->object.flags |= UNINTERESTING;
	add_pending_object(&rev, &commit->object, NULL);

	for (j = remoteheads; j; j = j->next)
		add_pending_object(&rev, &j->item->object, NULL);

	setup_revisions(0, NULL, &rev, NULL);
	if (prepare_revision_walk(&rev))
		die(_("revision walk setup failed"));

	ctx.abbrev = rev.abbrev;
	ctx.date_mode = rev.date_mode;
	ctx.fmt = rev.commit_format;

	strbuf_addstr(&out, "Squashed commit of the following:\n");
	while ((commit = get_revision(&rev)) != NULL) {
		strbuf_addch(&out, '\n');
		strbuf_addf(&out, "commit %s\n",
			sha1_to_hex(commit->object.sha1));
		pretty_print_commit(&ctx, commit, &out);
	}
	if (write_in_full(fd, out.buf, out.len) != out.len)
		die_errno(_("Writing SQUASH_MSG"));
	if (close(fd))
		die_errno(_("Finishing SQUASH_MSG"));
	strbuf_release(&out);
}

static void finish(struct commit *head_commit,
		   struct commit_list *remoteheads,
		   const unsigned char *new_head, const char *msg)
{
	struct strbuf reflog_message = STRBUF_INIT;
	const unsigned char *head = head_commit->object.sha1;

	if (!msg)
		strbuf_addstr(&reflog_message, getenv("GIT_REFLOG_ACTION"));
	else {
		if (verbosity >= 0)
			printf("%s\n", msg);
		strbuf_addf(&reflog_message, "%s: %s",
			getenv("GIT_REFLOG_ACTION"), msg);
	}
	if (squash) {
		squash_message(head_commit, remoteheads);
	} else {
		if (verbosity >= 0 && !merge_msg.len)
			printf(_("No merge message -- not updating HEAD\n"));
		else {
			const char *argv_gc_auto[] = { "gc", "--auto", NULL };
			update_ref(reflog_message.buf, "HEAD",
				new_head, head, 0,
				UPDATE_REFS_DIE_ON_ERR);
			/*
			 * We ignore errors in 'gc --auto', since the
			 * user should see them.
			 */
			run_command_v_opt(argv_gc_auto, RUN_GIT_CMD);
		}
	}
	if (new_head && show_diffstat) {
		struct diff_options opts;
		diff_setup(&opts);
		opts.stat_width = -1; /* use full terminal width */
		opts.stat_graph_width = -1; /* respect statGraphWidth config */
		opts.output_format |=
			DIFF_FORMAT_SUMMARY | DIFF_FORMAT_DIFFSTAT;
		opts.detect_rename = DIFF_DETECT_RENAME;
		diff_setup_done(&opts);
		diff_tree_sha1(head, new_head, "", &opts);
		diffcore_std(&opts);
		diff_flush(&opts);
	}

	/* Run a post-merge hook */
	run_hook_le(NULL, "post-merge", squash ? "1" : "0", NULL);

	strbuf_release(&reflog_message);
}

/* Get the name for the merge commit's message. */
static void merge_name(const char *remote, struct strbuf *msg)
{
	struct commit *remote_head;
	unsigned char branch_head[20];
	struct strbuf buf = STRBUF_INIT;
	struct strbuf bname = STRBUF_INIT;
	const char *ptr;
	char *found_ref;
	int len, early;

	strbuf_branchname(&bname, remote);
	remote = bname.buf;

	memset(branch_head, 0, sizeof(branch_head));
	remote_head = get_merge_parent(remote);
	if (!remote_head)
		die(_("'%s' does not point to a commit"), remote);

	if (dwim_ref(remote, strlen(remote), branch_head, &found_ref) > 0) {
		if (starts_with(found_ref, "refs/heads/")) {
			strbuf_addf(msg, "%s\t\tbranch '%s' of .\n",
				    sha1_to_hex(branch_head), remote);
			goto cleanup;
		}
		if (starts_with(found_ref, "refs/tags/")) {
			strbuf_addf(msg, "%s\t\ttag '%s' of .\n",
				    sha1_to_hex(branch_head), remote);
			goto cleanup;
		}
		if (starts_with(found_ref, "refs/remotes/")) {
			strbuf_addf(msg, "%s\t\tremote-tracking branch '%s' of .\n",
				    sha1_to_hex(branch_head), remote);
			goto cleanup;
		}
	}

	/* See if remote matches <name>^^^.. or <name>~<number> */
	for (len = 0, ptr = remote + strlen(remote);
	     remote < ptr && ptr[-1] == '^';
	     ptr--)
		len++;
	if (len)
		early = 1;
	else {
		early = 0;
		ptr = strrchr(remote, '~');
		if (ptr) {
			int seen_nonzero = 0;

			len++; /* count ~ */
			while (*++ptr && isdigit(*ptr)) {
				seen_nonzero |= (*ptr != '0');
				len++;
			}
			if (*ptr)
				len = 0; /* not ...~<number> */
			else if (seen_nonzero)
				early = 1;
			else if (len == 1)
				early = 1; /* "name~" is "name~1"! */
		}
	}
	if (len) {
		struct strbuf truname = STRBUF_INIT;
		strbuf_addstr(&truname, "refs/heads/");
		strbuf_addstr(&truname, remote);
		strbuf_setlen(&truname, truname.len - len);
		if (ref_exists(truname.buf)) {
			strbuf_addf(msg,
				    "%s\t\tbranch '%s'%s of .\n",
				    sha1_to_hex(remote_head->object.sha1),
				    truname.buf + 11,
				    (early ? " (early part)" : ""));
			strbuf_release(&truname);
			goto cleanup;
		}
	}

	if (!strcmp(remote, "FETCH_HEAD") &&
			!access(git_path("FETCH_HEAD"), R_OK)) {
		const char *filename;
		FILE *fp;
		struct strbuf line = STRBUF_INIT;
		char *ptr;

		filename = git_path("FETCH_HEAD");
		fp = fopen(filename, "r");
		if (!fp)
			die_errno(_("could not open '%s' for reading"),
				  filename);
		strbuf_getline(&line, fp, '\n');
		fclose(fp);
		ptr = strstr(line.buf, "\tnot-for-merge\t");
		if (ptr)
			strbuf_remove(&line, ptr-line.buf+1, 13);
		strbuf_addbuf(msg, &line);
		strbuf_release(&line);
		goto cleanup;
	}

	if (remote_head->util) {
		struct merge_remote_desc *desc;
		desc = merge_remote_util(remote_head);
		if (desc && desc->obj && desc->obj->type == OBJ_TAG) {
			strbuf_addf(msg, "%s\t\t%s '%s'\n",
				    sha1_to_hex(desc->obj->sha1),
				    typename(desc->obj->type),
				    remote);
			goto cleanup;
		}
	}

	strbuf_addf(msg, "%s\t\tcommit '%s'\n",
		sha1_to_hex(remote_head->object.sha1), remote);
cleanup:
	strbuf_release(&buf);
	strbuf_release(&bname);
}

static void parse_branch_merge_options(char *bmo)
{
	const char **argv;
	int argc;

	if (!bmo)
		return;
	argc = split_cmdline(bmo, &argv);
	if (argc < 0)
		die(_("Bad branch.%s.mergeoptions string: %s"), branch,
		    split_cmdline_strerror(argc));
	argv = xrealloc(argv, sizeof(*argv) * (argc + 2));
	memmove(argv + 1, argv, sizeof(*argv) * (argc + 1));
	argc++;
	argv[0] = "branch.*.mergeoptions";
	parse_options(argc, argv, NULL, builtin_merge_options,
		      builtin_merge_usage, 0);
	free(argv);
}

static int git_merge_config(const char *k, const char *v, void *cb)
{
	int status;

	if (branch && starts_with(k, "branch.") &&
		starts_with(k + 7, branch) &&
		!strcmp(k + 7 + strlen(branch), ".mergeoptions")) {
		free(branch_mergeoptions);
		branch_mergeoptions = xstrdup(v);
		return 0;
	}

	if (!strcmp(k, "merge.diffstat") || !strcmp(k, "merge.stat"))
		show_diffstat = git_config_bool(k, v);
	else if (!strcmp(k, "pull.twohead"))
		return git_config_string(&pull_twohead, k, v);
	else if (!strcmp(k, "pull.octopus"))
		return git_config_string(&pull_octopus, k, v);
	else if (!strcmp(k, "merge.renormalize"))
		option_renormalize = git_config_bool(k, v);
	else if (!strcmp(k, "merge.ff")) {
		int boolval = git_config_maybe_bool(k, v);
		if (0 <= boolval) {
			fast_forward = boolval ? FF_ALLOW : FF_NO;
		} else if (v && !strcmp(v, "only")) {
			fast_forward = FF_ONLY;
		} /* do not barf on values from future versions of git */
		return 0;
	} else if (!strcmp(k, "merge.defaulttoupstream")) {
		default_to_upstream = git_config_bool(k, v);
		return 0;
	} else if (!strcmp(k, "commit.gpgsign")) {
		sign_commit = git_config_bool(k, v) ? "" : NULL;
		return 0;
	}

	status = fmt_merge_msg_config(k, v, cb);
	if (status)
		return status;
	status = git_gpg_config(k, v, NULL);
	if (status)
		return status;
	return git_diff_ui_config(k, v, cb);
}

static int read_tree_trivial(unsigned char *common, unsigned char *head,
			     unsigned char *one)
{
	int i, nr_trees = 0;
	struct tree *trees[MAX_UNPACK_TREES];
	struct tree_desc t[MAX_UNPACK_TREES];
	struct unpack_trees_options opts;

	memset(&opts, 0, sizeof(opts));
	opts.head_idx = 2;
	opts.src_index = &the_index;
	opts.dst_index = &the_index;
	opts.update = 1;
	opts.verbose_update = 1;
	opts.trivial_merges_only = 1;
	opts.merge = 1;
	trees[nr_trees] = parse_tree_indirect(common);
	if (!trees[nr_trees++])
		return -1;
	trees[nr_trees] = parse_tree_indirect(head);
	if (!trees[nr_trees++])
		return -1;
	trees[nr_trees] = parse_tree_indirect(one);
	if (!trees[nr_trees++])
		return -1;
	opts.fn = threeway_merge;
	cache_tree_free(&active_cache_tree);
	for (i = 0; i < nr_trees; i++) {
		parse_tree(trees[i]);
		init_tree_desc(t+i, trees[i]->buffer, trees[i]->size);
	}
	if (unpack_trees(nr_trees, t, &opts))
		return -1;
	return 0;
}

static void write_tree_trivial(unsigned char *sha1)
{
	if (write_cache_as_tree(sha1, 0, NULL))
		die(_("git write-tree failed to write a tree"));
}

static int try_merge_strategy(const char *strategy, struct commit_list *common,
			      struct commit_list *remoteheads,
			      struct commit *head, const char *head_arg)
{
	struct lock_file *lock = xcalloc(1, sizeof(struct lock_file));

	hold_locked_index(lock, 1);
	refresh_cache(REFRESH_QUIET);
	if (active_cache_changed &&
	    write_locked_index(&the_index, lock, COMMIT_LOCK))
		return error(_("Unable to write index."));
	rollback_lock_file(lock);

	if (!strcmp(strategy, "recursive") || !strcmp(strategy, "subtree")) {
		int clean, x;
		struct commit *result;
		struct lock_file *lock = xcalloc(1, sizeof(struct lock_file));
		struct commit_list *reversed = NULL;
		struct merge_options o;
		struct commit_list *j;

		if (remoteheads->next) {
			error(_("Not handling anything other than two heads merge."));
			return 2;
		}

		init_merge_options(&o);
		if (!strcmp(strategy, "subtree"))
			o.subtree_shift = "";

		o.renormalize = option_renormalize;
		o.show_rename_progress =
			show_progress == -1 ? isatty(2) : show_progress;

		for (x = 0; x < xopts_nr; x++)
			if (parse_merge_opt(&o, xopts[x]))
				die(_("Unknown option for merge-recursive: -X%s"), xopts[x]);

		o.branch1 = head_arg;
		o.branch2 = merge_remote_util(remoteheads->item)->name;

		for (j = common; j; j = j->next)
			commit_list_insert(j->item, &reversed);

		hold_locked_index(lock, 1);
		clean = merge_recursive(&o, head,
				remoteheads->item, reversed, &result);
		if (active_cache_changed &&
		    write_locked_index(&the_index, lock, COMMIT_LOCK))
			die (_("unable to write %s"), get_index_file());
		rollback_lock_file(lock);
		return clean ? 0 : 1;
	} else {
		return try_merge_command(strategy, xopts_nr, xopts,
						common, head_arg, remoteheads);
	}
}

static void count_diff_files(struct diff_queue_struct *q,
			     struct diff_options *opt, void *data)
{
	int *count = data;

	(*count) += q->nr;
}

static int count_unmerged_entries(void)
{
	int i, ret = 0;

	for (i = 0; i < active_nr; i++)
		if (ce_stage(active_cache[i]))
			ret++;

	return ret;
}

static void split_merge_strategies(const char *string, struct strategy **list,
				   int *nr, int *alloc)
{
	char *p, *q, *buf;

	if (!string)
		return;

	buf = xstrdup(string);
	q = buf;
	for (;;) {
		p = strchr(q, ' ');
		if (!p) {
			ALLOC_GROW(*list, *nr + 1, *alloc);
			(*list)[(*nr)++].name = xstrdup(q);
			free(buf);
			return;
		} else {
			*p = '\0';
			ALLOC_GROW(*list, *nr + 1, *alloc);
			(*list)[(*nr)++].name = xstrdup(q);
			q = ++p;
		}
	}
}

static void add_strategies(const char *string, unsigned attr)
{
	struct strategy *list = NULL;
	int list_alloc = 0, list_nr = 0, i;

	memset(&list, 0, sizeof(list));
	split_merge_strategies(string, &list, &list_nr, &list_alloc);
	if (list) {
		for (i = 0; i < list_nr; i++)
			append_strategy(get_strategy(list[i].name));
		return;
	}
	for (i = 0; i < ARRAY_SIZE(all_strategy); i++)
		if (all_strategy[i].attr & attr)
			append_strategy(&all_strategy[i]);

}

static void write_merge_msg(struct strbuf *msg)
{
	const char *filename = git_path("MERGE_MSG");
	int fd = open(filename, O_WRONLY | O_CREAT, 0666);
	if (fd < 0)
		die_errno(_("Could not open '%s' for writing"),
			  filename);
	if (write_in_full(fd, msg->buf, msg->len) != msg->len)
		die_errno(_("Could not write to '%s'"), filename);
	close(fd);
}

static void read_merge_msg(struct strbuf *msg)
{
	const char *filename = git_path("MERGE_MSG");
	strbuf_reset(msg);
	if (strbuf_read_file(msg, filename, 0) < 0)
		die_errno(_("Could not read from '%s'"), filename);
}

static void write_merge_state(struct commit_list *);
static void abort_commit(struct commit_list *remoteheads, const char *err_msg)
{
	if (err_msg)
		error("%s", err_msg);
	fprintf(stderr,
		_("Not committing merge; use 'git commit' to complete the merge.\n"));
	write_merge_state(remoteheads);
	exit(1);
}

static const char merge_editor_comment[] =
N_("Please enter a commit message to explain why this merge is necessary,\n"
   "especially if it merges an updated upstream into a topic branch.\n"
   "\n"
   "Lines starting with '%c' will be ignored, and an empty message aborts\n"
   "the commit.\n");

static void prepare_to_commit(struct commit_list *remoteheads)
{
	struct strbuf msg = STRBUF_INIT;
	strbuf_addbuf(&msg, &merge_msg);
	strbuf_addch(&msg, '\n');
	if (0 < option_edit)
		strbuf_commented_addf(&msg, _(merge_editor_comment), comment_line_char);
	write_merge_msg(&msg);
	if (run_commit_hook(0 < option_edit, get_index_file(), "prepare-commit-msg",
			    git_path("MERGE_MSG"), "merge", NULL))
		abort_commit(remoteheads, NULL);
	if (0 < option_edit) {
		if (launch_editor(git_path("MERGE_MSG"), NULL, NULL))
			abort_commit(remoteheads, NULL);
	}
	read_merge_msg(&msg);
	stripspace(&msg, 0 < option_edit);
	if (!msg.len)
		abort_commit(remoteheads, _("Empty commit message."));
	strbuf_release(&merge_msg);
	strbuf_addbuf(&merge_msg, &msg);
	strbuf_release(&msg);
}

static int merge_trivial(struct commit *head, struct commit_list *remoteheads)
{
	unsigned char result_tree[20], result_commit[20];
	struct commit_list *parents, **pptr = &parents;

	write_tree_trivial(result_tree);
	printf(_("Wonderful.\n"));
	pptr = commit_list_append(head, pptr);
	pptr = commit_list_append(remoteheads->item, pptr);
	prepare_to_commit(remoteheads);
	if (commit_tree(merge_msg.buf, merge_msg.len, result_tree, parents,
			result_commit, NULL, sign_commit))
		die(_("failed to write commit object"));
	finish(head, remoteheads, result_commit, "In-index merge");
	drop_save();
	return 0;
}

static int finish_automerge(struct commit *head,
			    int head_subsumed,
			    struct commit_list *common,
			    struct commit_list *remoteheads,
			    unsigned char *result_tree,
			    const char *wt_strategy)
{
	struct commit_list *parents = NULL;
	struct strbuf buf = STRBUF_INIT;
	unsigned char result_commit[20];

	free_commit_list(common);
	parents = remoteheads;
	if (!head_subsumed || fast_forward == FF_NO)
		commit_list_insert(head, &parents);
	strbuf_addch(&merge_msg, '\n');
	prepare_to_commit(remoteheads);
	if (commit_tree(merge_msg.buf, merge_msg.len, result_tree, parents,
			result_commit, NULL, sign_commit))
		die(_("failed to write commit object"));
	strbuf_addf(&buf, "Merge made by the '%s' strategy.", wt_strategy);
	finish(head, remoteheads, result_commit, buf.buf);
	strbuf_release(&buf);
	drop_save();
	return 0;
}

static int suggest_conflicts(int renormalizing)
{
	const char *filename;
	FILE *fp;
	int pos;

	filename = git_path("MERGE_MSG");
	fp = fopen(filename, "a");
	if (!fp)
		die_errno(_("Could not open '%s' for writing"), filename);
	fprintf(fp, "\nConflicts:\n");
	for (pos = 0; pos < active_nr; pos++) {
		const struct cache_entry *ce = active_cache[pos];

		if (ce_stage(ce)) {
			fprintf(fp, "\t%s\n", ce->name);
			while (pos + 1 < active_nr &&
					!strcmp(ce->name,
						active_cache[pos + 1]->name))
				pos++;
		}
	}
	fclose(fp);
	rerere(allow_rerere_auto);
	printf(_("Automatic merge failed; "
			"fix conflicts and then commit the result.\n"));
	return 1;
}

static struct commit *is_old_style_invocation(int argc, const char **argv,
					      const unsigned char *head)
{
	struct commit *second_token = NULL;
	if (argc > 2) {
		unsigned char second_sha1[20];

		if (get_sha1(argv[1], second_sha1))
			return NULL;
		second_token = lookup_commit_reference_gently(second_sha1, 0);
		if (!second_token)
			die(_("'%s' is not a commit"), argv[1]);
		if (hashcmp(second_token->object.sha1, head))
			return NULL;
	}
	return second_token;
}

static int evaluate_result(void)
{
	int cnt = 0;
	struct rev_info rev;

	/* Check how many files differ. */
	init_revisions(&rev, "");
	setup_revisions(0, NULL, &rev, NULL);
	rev.diffopt.output_format |=
		DIFF_FORMAT_CALLBACK;
	rev.diffopt.format_callback = count_diff_files;
	rev.diffopt.format_callback_data = &cnt;
	run_diff_files(&rev, 0);

	/*
	 * Check how many unmerged entries are
	 * there.
	 */
	cnt += count_unmerged_entries();

	return cnt;
}

/*
 * Pretend as if the user told us to merge with the remote-tracking
 * branch we have for the upstream of the current branch
 */
static int setup_with_upstream(const char ***argv)
{
	struct branch *branch = branch_get(NULL);
	int i;
	const char **args;

	if (!branch)
		die(_("No current branch."));
	if (!branch->remote)
		die(_("No remote for the current branch."));
	if (!branch->merge_nr)
		die(_("No default upstream defined for the current branch."));

	args = xcalloc(branch->merge_nr + 1, sizeof(char *));
	for (i = 0; i < branch->merge_nr; i++) {
		if (!branch->merge[i]->dst)
			die(_("No remote-tracking branch for %s from %s"),
			    branch->merge[i]->src, branch->remote_name);
		args[i] = branch->merge[i]->dst;
	}
	args[i] = NULL;
	*argv = args;
	return i;
}

static void write_merge_state(struct commit_list *remoteheads)
{
	const char *filename;
	int fd;
	struct commit_list *j;
	struct strbuf buf = STRBUF_INIT;

	for (j = remoteheads; j; j = j->next) {
		unsigned const char *sha1;
		struct commit *c = j->item;
		if (c->util && merge_remote_util(c)->obj) {
			sha1 = merge_remote_util(c)->obj->sha1;
		} else {
			sha1 = c->object.sha1;
		}
		strbuf_addf(&buf, "%s\n", sha1_to_hex(sha1));
	}
	filename = git_path("MERGE_HEAD");
	fd = open(filename, O_WRONLY | O_CREAT, 0666);
	if (fd < 0)
		die_errno(_("Could not open '%s' for writing"), filename);
	if (write_in_full(fd, buf.buf, buf.len) != buf.len)
		die_errno(_("Could not write to '%s'"), filename);
	close(fd);
	strbuf_addch(&merge_msg, '\n');
	write_merge_msg(&merge_msg);

	filename = git_path("MERGE_MODE");
	fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0666);
	if (fd < 0)
		die_errno(_("Could not open '%s' for writing"), filename);
	strbuf_reset(&buf);
	if (fast_forward == FF_NO)
		strbuf_addf(&buf, "no-ff");
	if (write_in_full(fd, buf.buf, buf.len) != buf.len)
		die_errno(_("Could not write to '%s'"), filename);
	close(fd);
}

static int default_edit_option(void)
{
	static const char name[] = "GIT_MERGE_AUTOEDIT";
	const char *e = getenv(name);
	struct stat st_stdin, st_stdout;

	if (have_message)
		/* an explicit -m msg without --[no-]edit */
		return 0;

	if (e) {
		int v = git_config_maybe_bool(name, e);
		if (v < 0)
			die("Bad value '%s' in environment '%s'", e, name);
		return v;
	}

	/* Use editor if stdin and stdout are the same and is a tty */
	return (!fstat(0, &st_stdin) &&
		!fstat(1, &st_stdout) &&
		isatty(0) && isatty(1) &&
		st_stdin.st_dev == st_stdout.st_dev &&
		st_stdin.st_ino == st_stdout.st_ino &&
		st_stdin.st_mode == st_stdout.st_mode);
}

static struct commit_list *collect_parents(struct commit *head_commit,
					   int *head_subsumed,
					   int argc, const char **argv)
{
	int i;
	struct commit_list *remoteheads = NULL, *parents, *next;
	struct commit_list **remotes = &remoteheads;

	if (head_commit)
		remotes = &commit_list_insert(head_commit, remotes)->next;
	for (i = 0; i < argc; i++) {
		struct commit *commit = get_merge_parent(argv[i]);
		if (!commit)
			help_unknown_ref(argv[i], "merge",
					 "not something we can merge");
		remotes = &commit_list_insert(commit, remotes)->next;
	}
	*remotes = NULL;

	parents = reduce_heads(remoteheads);

	*head_subsumed = 1; /* we will flip this to 0 when we find it */
	for (remoteheads = NULL, remotes = &remoteheads;
	     parents;
	     parents = next) {
		struct commit *commit = parents->item;
		next = parents->next;
		if (commit == head_commit)
			*head_subsumed = 0;
		else
			remotes = &commit_list_insert(commit, remotes)->next;
	}
	return remoteheads;
}

int cmd_merge(int argc, const char **argv, const char *prefix)
{
	unsigned char result_tree[20];
	unsigned char stash[20];
	unsigned char head_sha1[20];
	struct commit *head_commit;
	struct strbuf buf = STRBUF_INIT;
	const char *head_arg;
	int flag, i, ret = 0, head_subsumed;
	int best_cnt = -1, merge_was_ok = 0, automerge_was_ok = 0;
	struct commit_list *common = NULL;
	const char *best_strategy = NULL, *wt_strategy = NULL;
	struct commit_list *remoteheads, *p;
	void *branch_to_free;

	if (argc == 2 && !strcmp(argv[1], "-h"))
		usage_with_options(builtin_merge_usage, builtin_merge_options);

	/*
	 * Check if we are _not_ on a detached HEAD, i.e. if there is a
	 * current branch.
	 */
	branch = branch_to_free = resolve_refdup("HEAD", head_sha1, 0, &flag);
	if (branch && starts_with(branch, "refs/heads/"))
		branch += 11;
	if (!branch || is_null_sha1(head_sha1))
		head_commit = NULL;
	else
		head_commit = lookup_commit_or_die(head_sha1, "HEAD");

	git_config(git_merge_config, NULL);

	if (branch_mergeoptions)
		parse_branch_merge_options(branch_mergeoptions);
	argc = parse_options(argc, argv, prefix, builtin_merge_options,
			builtin_merge_usage, 0);
	if (shortlog_len < 0)
		shortlog_len = (merge_log_config > 0) ? merge_log_config : 0;

	if (verbosity < 0 && show_progress == -1)
		show_progress = 0;

	if (abort_current_merge) {
		int nargc = 2;
		const char *nargv[] = {"reset", "--merge", NULL};

		if (!file_exists(git_path("MERGE_HEAD")))
			die(_("There is no merge to abort (MERGE_HEAD missing)."));

		/* Invoke 'git reset --merge' */
		ret = cmd_reset(nargc, nargv, prefix);
		goto done;
	}

	if (read_cache_unmerged())
		die_resolve_conflict("merge");

	if (file_exists(git_path("MERGE_HEAD"))) {
		/*
		 * There is no unmerged entry, don't advise 'git
		 * add/rm <file>', just 'git commit'.
		 */
		if (advice_resolve_conflict)
			die(_("You have not concluded your merge (MERGE_HEAD exists).\n"
				  "Please, commit your changes before you can merge."));
		else
			die(_("You have not concluded your merge (MERGE_HEAD exists)."));
	}
	if (file_exists(git_path("CHERRY_PICK_HEAD"))) {
		if (advice_resolve_conflict)
			die(_("You have not concluded your cherry-pick (CHERRY_PICK_HEAD exists).\n"
			    "Please, commit your changes before you can merge."));
		else
			die(_("You have not concluded your cherry-pick (CHERRY_PICK_HEAD exists)."));
	}
	resolve_undo_clear();

	if (verbosity < 0)
		show_diffstat = 0;

	if (squash) {
		if (fast_forward == FF_NO)
			die(_("You cannot combine --squash with --no-ff."));
		option_commit = 0;
	}

	if (!abort_current_merge) {
		if (!argc) {
			if (default_to_upstream)
				argc = setup_with_upstream(&argv);
			else
				die(_("No commit specified and merge.defaultToUpstream not set."));
		} else if (argc == 1 && !strcmp(argv[0], "-"))
			argv[0] = "@{-1}";
	}
	if (!argc)
		usage_with_options(builtin_merge_usage,
			builtin_merge_options);

	/*
	 * This could be traditional "merge <msg> HEAD <commit>..."  and
	 * the way we can tell it is to see if the second token is HEAD,
	 * but some people might have misused the interface and used a
	 * commit-ish that is the same as HEAD there instead.
	 * Traditional format never would have "-m" so it is an
	 * additional safety measure to check for it.
	 */

	if (!have_message && head_commit &&
	    is_old_style_invocation(argc, argv, head_commit->object.sha1)) {
		strbuf_addstr(&merge_msg, argv[0]);
		head_arg = argv[1];
		argv += 2;
		argc -= 2;
		remoteheads = collect_parents(head_commit, &head_subsumed, argc, argv);
	} else if (!head_commit) {
		struct commit *remote_head;
		/*
		 * If the merged head is a valid one there is no reason
		 * to forbid "git merge" into a branch yet to be born.
		 * We do the same for "git pull".
		 */
		if (argc != 1)
			die(_("Can merge only exactly one commit into "
				"empty head"));
		if (squash)
			die(_("Squash commit into empty head not supported yet"));
		if (fast_forward == FF_NO)
			die(_("Non-fast-forward commit does not make sense into "
			    "an empty head"));
		remoteheads = collect_parents(head_commit, &head_subsumed, argc, argv);
		remote_head = remoteheads->item;
		if (!remote_head)
			die(_("%s - not something we can merge"), argv[0]);
		read_empty(remote_head->object.sha1, 0);
		update_ref("initial pull", "HEAD", remote_head->object.sha1,
			   NULL, 0, UPDATE_REFS_DIE_ON_ERR);
		goto done;
	} else {
		struct strbuf merge_names = STRBUF_INIT;

		/* We are invoked directly as the first-class UI. */
		head_arg = "HEAD";

		/*
		 * All the rest are the commits being merged; prepare
		 * the standard merge summary message to be appended
		 * to the given message.
		 */
		remoteheads = collect_parents(head_commit, &head_subsumed, argc, argv);
		for (p = remoteheads; p; p = p->next)
			merge_name(merge_remote_util(p->item)->name, &merge_names);

		if (!have_message || shortlog_len) {
			struct fmt_merge_msg_opts opts;
			memset(&opts, 0, sizeof(opts));
			opts.add_title = !have_message;
			opts.shortlog_len = shortlog_len;
			opts.credit_people = (0 < option_edit);

			fmt_merge_msg(&merge_names, &merge_msg, &opts);
			if (merge_msg.len)
				strbuf_setlen(&merge_msg, merge_msg.len - 1);
		}
	}

	if (!head_commit || !argc)
		usage_with_options(builtin_merge_usage,
			builtin_merge_options);

	if (verify_signatures) {
		for (p = remoteheads; p; p = p->next) {
			struct commit *commit = p->item;
			char hex[41];
			struct signature_check signature_check;
			memset(&signature_check, 0, sizeof(signature_check));

			check_commit_signature(commit, &signature_check);

			strcpy(hex, find_unique_abbrev(commit->object.sha1, DEFAULT_ABBREV));
			switch (signature_check.result) {
			case 'G':
				break;
			case 'U':
				die(_("Commit %s has an untrusted GPG signature, "
				      "allegedly by %s."), hex, signature_check.signer);
			case 'B':
				die(_("Commit %s has a bad GPG signature "
				      "allegedly by %s."), hex, signature_check.signer);
			default: /* 'N' */
				die(_("Commit %s does not have a GPG signature."), hex);
			}
			if (verbosity >= 0 && signature_check.result == 'G')
				printf(_("Commit %s has a good GPG signature by %s\n"),
				       hex, signature_check.signer);

			signature_check_clear(&signature_check);
		}
	}

	strbuf_addstr(&buf, "merge");
	for (p = remoteheads; p; p = p->next)
		strbuf_addf(&buf, " %s", merge_remote_util(p->item)->name);
	setenv("GIT_REFLOG_ACTION", buf.buf, 0);
	strbuf_reset(&buf);

	for (p = remoteheads; p; p = p->next) {
		struct commit *commit = p->item;
		strbuf_addf(&buf, "GITHEAD_%s",
			    sha1_to_hex(commit->object.sha1));
		setenv(buf.buf, merge_remote_util(commit)->name, 1);
		strbuf_reset(&buf);
		if (fast_forward != FF_ONLY &&
		    merge_remote_util(commit) &&
		    merge_remote_util(commit)->obj &&
		    merge_remote_util(commit)->obj->type == OBJ_TAG)
			fast_forward = FF_NO;
	}

	if (option_edit < 0)
		option_edit = default_edit_option();

	if (!use_strategies) {
		if (!remoteheads)
			; /* already up-to-date */
		else if (!remoteheads->next)
			add_strategies(pull_twohead, DEFAULT_TWOHEAD);
		else
			add_strategies(pull_octopus, DEFAULT_OCTOPUS);
	}

	for (i = 0; i < use_strategies_nr; i++) {
		if (use_strategies[i]->attr & NO_FAST_FORWARD)
			fast_forward = FF_NO;
		if (use_strategies[i]->attr & NO_TRIVIAL)
			allow_trivial = 0;
	}

	if (!remoteheads)
		; /* already up-to-date */
	else if (!remoteheads->next)
		common = get_merge_bases(head_commit, remoteheads->item, 1);
	else {
		struct commit_list *list = remoteheads;
		commit_list_insert(head_commit, &list);
		common = get_octopus_merge_bases(list);
		free(list);
	}

	update_ref("updating ORIG_HEAD", "ORIG_HEAD", head_commit->object.sha1,
		   NULL, 0, UPDATE_REFS_DIE_ON_ERR);

	if (remoteheads && !common)
		; /* No common ancestors found. We need a real merge. */
	else if (!remoteheads ||
		 (!remoteheads->next && !common->next &&
		  common->item == remoteheads->item)) {
		/*
		 * If head can reach all the merge then we are up to date.
		 * but first the most common case of merging one remote.
		 */
		finish_up_to_date("Already up-to-date.");
		goto done;
	} else if (fast_forward != FF_NO && !remoteheads->next &&
			!common->next &&
			!hashcmp(common->item->object.sha1, head_commit->object.sha1)) {
		/* Again the most common case of merging one remote. */
		struct strbuf msg = STRBUF_INIT;
		struct commit *commit;
		char hex[41];

		strcpy(hex, find_unique_abbrev(head_commit->object.sha1, DEFAULT_ABBREV));

		if (verbosity >= 0)
			printf(_("Updating %s..%s\n"),
				hex,
				find_unique_abbrev(remoteheads->item->object.sha1,
				DEFAULT_ABBREV));
		strbuf_addstr(&msg, "Fast-forward");
		if (have_message)
			strbuf_addstr(&msg,
				" (no commit created; -m option ignored)");
		commit = remoteheads->item;
		if (!commit) {
			ret = 1;
			goto done;
		}

		if (checkout_fast_forward(head_commit->object.sha1,
					  commit->object.sha1,
					  overwrite_ignore)) {
			ret = 1;
			goto done;
		}

		finish(head_commit, remoteheads, commit->object.sha1, msg.buf);
		drop_save();
		goto done;
	} else if (!remoteheads->next && common->next)
		;
		/*
		 * We are not doing octopus and not fast-forward.  Need
		 * a real merge.
		 */
	else if (!remoteheads->next && !common->next && option_commit) {
		/*
		 * We are not doing octopus, not fast-forward, and have
		 * only one common.
		 */
		refresh_cache(REFRESH_QUIET);
		if (allow_trivial && fast_forward != FF_ONLY) {
			/* See if it is really trivial. */
			git_committer_info(IDENT_STRICT);
			printf(_("Trying really trivial in-index merge...\n"));
			if (!read_tree_trivial(common->item->object.sha1,
					       head_commit->object.sha1,
					       remoteheads->item->object.sha1)) {
				ret = merge_trivial(head_commit, remoteheads);
				goto done;
			}
			printf(_("Nope.\n"));
		}
	} else {
		/*
		 * An octopus.  If we can reach all the remote we are up
		 * to date.
		 */
		int up_to_date = 1;
		struct commit_list *j;

		for (j = remoteheads; j; j = j->next) {
			struct commit_list *common_one;

			/*
			 * Here we *have* to calculate the individual
			 * merge_bases again, otherwise "git merge HEAD^
			 * HEAD^^" would be missed.
			 */
			common_one = get_merge_bases(head_commit, j->item, 1);
			if (hashcmp(common_one->item->object.sha1,
				j->item->object.sha1)) {
				up_to_date = 0;
				break;
			}
		}
		if (up_to_date) {
			finish_up_to_date("Already up-to-date. Yeeah!");
			goto done;
		}
	}

	if (fast_forward == FF_ONLY)
		die(_("Not possible to fast-forward, aborting."));

	/* We are going to make a new commit. */
	git_committer_info(IDENT_STRICT);

	/*
	 * At this point, we need a real merge.  No matter what strategy
	 * we use, it would operate on the index, possibly affecting the
	 * working tree, and when resolved cleanly, have the desired
	 * tree in the index -- this means that the index must be in
	 * sync with the head commit.  The strategies are responsible
	 * to ensure this.
	 */
	if (use_strategies_nr == 1 ||
	    /*
	     * Stash away the local changes so that we can try more than one.
	     */
	    save_state(stash))
		hashcpy(stash, null_sha1);

	for (i = 0; i < use_strategies_nr; i++) {
		int ret;
		if (i) {
			printf(_("Rewinding the tree to pristine...\n"));
			restore_state(head_commit->object.sha1, stash);
		}
		if (use_strategies_nr != 1)
			printf(_("Trying merge strategy %s...\n"),
				use_strategies[i]->name);
		/*
		 * Remember which strategy left the state in the working
		 * tree.
		 */
		wt_strategy = use_strategies[i]->name;

		ret = try_merge_strategy(use_strategies[i]->name,
					 common, remoteheads,
					 head_commit, head_arg);
		if (!option_commit && !ret) {
			merge_was_ok = 1;
			/*
			 * This is necessary here just to avoid writing
			 * the tree, but later we will *not* exit with
			 * status code 1 because merge_was_ok is set.
			 */
			ret = 1;
		}

		if (ret) {
			/*
			 * The backend exits with 1 when conflicts are
			 * left to be resolved, with 2 when it does not
			 * handle the given merge at all.
			 */
			if (ret == 1) {
				int cnt = evaluate_result();

				if (best_cnt <= 0 || cnt <= best_cnt) {
					best_strategy = use_strategies[i]->name;
					best_cnt = cnt;
				}
			}
			if (merge_was_ok)
				break;
			else
				continue;
		}

		/* Automerge succeeded. */
		write_tree_trivial(result_tree);
		automerge_was_ok = 1;
		break;
	}

	/*
	 * If we have a resulting tree, that means the strategy module
	 * auto resolved the merge cleanly.
	 */
	if (automerge_was_ok) {
		ret = finish_automerge(head_commit, head_subsumed,
				       common, remoteheads,
				       result_tree, wt_strategy);
		goto done;
	}

	/*
	 * Pick the result from the best strategy and have the user fix
	 * it up.
	 */
	if (!best_strategy) {
		restore_state(head_commit->object.sha1, stash);
		if (use_strategies_nr > 1)
			fprintf(stderr,
				_("No merge strategy handled the merge.\n"));
		else
			fprintf(stderr, _("Merge with strategy %s failed.\n"),
				use_strategies[0]->name);
		ret = 2;
		goto done;
	} else if (best_strategy == wt_strategy)
		; /* We already have its result in the working tree. */
	else {
		printf(_("Rewinding the tree to pristine...\n"));
		restore_state(head_commit->object.sha1, stash);
		printf(_("Using the %s to prepare resolving by hand.\n"),
			best_strategy);
		try_merge_strategy(best_strategy, common, remoteheads,
				   head_commit, head_arg);
	}

	if (squash)
		finish(head_commit, remoteheads, NULL, NULL);
	else
		write_merge_state(remoteheads);

	if (merge_was_ok)
		fprintf(stderr, _("Automatic merge went well; "
			"stopped before committing as requested\n"));
	else
		ret = suggest_conflicts(option_renormalize);

done:
	free(branch_to_free);
	return ret;
}
