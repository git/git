/*
 * Builtin "git merge"
 *
 * Copyright (c) 2008 Miklos Vajna <vmiklos@frugalware.org>
 *
 * Based on git-merge.sh by Junio C Hamano.
 */

#define USE_THE_INDEX_COMPATIBILITY_MACROS
#include "cache.h"
#include "config.h"
#include "parse-options.h"
#include "builtin.h"
#include "lockfile.h"
#include "run-command.h"
#include "hook.h"
#include "diff.h"
#include "diff-merges.h"
#include "refs.h"
#include "refspec.h"
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
#include "merge-ort-wrappers.h"
#include "resolve-undo.h"
#include "remote.h"
#include "fmt-merge-msg.h"
#include "gpg-interface.h"
#include "sequencer.h"
#include "string-list.h"
#include "packfile.h"
#include "tag.h"
#include "alias.h"
#include "branch.h"
#include "commit-reach.h"
#include "wt-status.h"
#include "commit-graph.h"

#define DEFAULT_TWOHEAD (1<<0)
#define DEFAULT_OCTOPUS (1<<1)
#define NO_FAST_FORWARD (1<<2)
#define NO_TRIVIAL      (1<<3)

struct strategy {
	const char *name;
	unsigned attr;
};

static const char * const builtin_merge_usage[] = {
	N_("git merge [<options>] [<commit>...]"),
	"git merge --abort",
	"git merge --continue",
	NULL
};

static int show_diffstat = 1, shortlog_len = -1, squash;
static int option_commit = -1;
static int option_edit = -1;
static int allow_trivial = 1, have_message, verify_signatures;
static int check_trust_level = 1;
static int overwrite_ignore = 1;
static struct strbuf merge_msg = STRBUF_INIT;
static struct strategy **use_strategies;
static size_t use_strategies_nr, use_strategies_alloc;
static const char **xopts;
static size_t xopts_nr, xopts_alloc;
static const char *branch;
static char *branch_mergeoptions;
static int verbosity;
static int allow_rerere_auto;
static int abort_current_merge;
static int quit_current_merge;
static int continue_current_merge;
static int allow_unrelated_histories;
static int show_progress = -1;
static int default_to_upstream = 1;
static int signoff;
static const char *sign_commit;
static int autostash;
static int no_verify;

static struct strategy all_strategy[] = {
	{ "recursive",  NO_TRIVIAL },
	{ "octopus",    DEFAULT_OCTOPUS },
	{ "ort",        DEFAULT_TWOHEAD | NO_TRIVIAL },
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

static const char *cleanup_arg;
static enum commit_msg_cleanup_mode cleanup_mode;

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

static enum parse_opt_result option_read_message(struct parse_opt_ctx_t *ctx,
						 const struct option *opt,
						 const char *arg_not_used,
						 int unset)
{
	struct strbuf *buf = opt->value;
	const char *arg;

	BUG_ON_OPT_ARG(arg_not_used);
	if (unset)
		BUG("-F cannot be negated");

	if (ctx->opt) {
		arg = ctx->opt;
		ctx->opt = NULL;
	} else if (ctx->argc > 1) {
		ctx->argc--;
		arg = *++ctx->argv;
	} else
		return error(_("option `%s' requires a value"), opt->long_name);

	if (buf->len)
		strbuf_addch(buf, '\n');
	if (ctx->prefix && !is_absolute_path(arg))
		arg = prefix_filename(ctx->prefix, arg);
	if (strbuf_read_file(buf, arg, 0) < 0)
		return error(_("could not read file '%s'"), arg);
	have_message = 1;

	return 0;
}

static struct strategy *get_strategy(const char *name)
{
	int i;
	struct strategy *ret;
	static struct cmdnames main_cmds, other_cmds;
	static int loaded;
	char *default_strategy = getenv("GIT_TEST_MERGE_ALGORITHM");

	if (!name)
		return NULL;

	if (default_strategy &&
	    !strcmp(default_strategy, "ort") &&
	    !strcmp(name, "recursive")) {
		name = "ort";
	}

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

	CALLOC_ARRAY(ret, 1);
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
	BUG_ON_OPT_ARG(arg);
	show_diffstat = unset;
	return 0;
}

static struct option builtin_merge_options[] = {
	OPT_CALLBACK_F('n', NULL, NULL, NULL,
		N_("do not show a diffstat at the end of the merge"),
		PARSE_OPT_NOARG, option_parse_n),
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
	OPT_CLEANUP(&cleanup_arg),
	OPT_SET_INT(0, "ff", &fast_forward, N_("allow fast-forward (default)"), FF_ALLOW),
	OPT_SET_INT_F(0, "ff-only", &fast_forward,
		      N_("abort if fast-forward is not possible"),
		      FF_ONLY, PARSE_OPT_NONEG),
	OPT_RERERE_AUTOUPDATE(&allow_rerere_auto),
	OPT_BOOL(0, "verify-signatures", &verify_signatures,
		N_("verify that the named commit has a valid GPG signature")),
	OPT_CALLBACK('s', "strategy", &use_strategies, N_("strategy"),
		N_("merge strategy to use"), option_parse_strategy),
	OPT_CALLBACK('X', "strategy-option", &xopts, N_("option=value"),
		N_("option for selected merge strategy"), option_parse_x),
	OPT_CALLBACK('m', "message", &merge_msg, N_("message"),
		N_("merge commit message (for a non-fast-forward merge)"),
		option_parse_message),
	{ OPTION_LOWLEVEL_CALLBACK, 'F', "file", &merge_msg, N_("path"),
		N_("read message from file"), PARSE_OPT_NONEG,
		NULL, 0, option_read_message },
	OPT__VERBOSITY(&verbosity),
	OPT_BOOL(0, "abort", &abort_current_merge,
		N_("abort the current in-progress merge")),
	OPT_BOOL(0, "quit", &quit_current_merge,
		N_("--abort but leave index and working tree alone")),
	OPT_BOOL(0, "continue", &continue_current_merge,
		N_("continue the current in-progress merge")),
	OPT_BOOL(0, "allow-unrelated-histories", &allow_unrelated_histories,
		 N_("allow merging unrelated histories")),
	OPT_SET_INT(0, "progress", &show_progress, N_("force progress reporting"), 1),
	{ OPTION_STRING, 'S', "gpg-sign", &sign_commit, N_("key-id"),
	  N_("GPG sign commit"), PARSE_OPT_OPTARG, NULL, (intptr_t) "" },
	OPT_AUTOSTASH(&autostash),
	OPT_BOOL(0, "overwrite-ignore", &overwrite_ignore, N_("update ignored files (default)")),
	OPT_BOOL(0, "signoff", &signoff, N_("add a Signed-off-by trailer")),
	OPT_BOOL(0, "no-verify", &no_verify, N_("bypass pre-merge-commit and commit-msg hooks")),
	OPT_END()
};

static int save_state(struct object_id *stash)
{
	int len;
	struct child_process cp = CHILD_PROCESS_INIT;
	struct strbuf buffer = STRBUF_INIT;
	const char *argv[] = {"stash", "create", NULL};
	int rc = -1;

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
		goto out;
	strbuf_setlen(&buffer, buffer.len-1);
	if (get_oid(buffer.buf, stash))
		die(_("not a valid object: %s"), buffer.buf);
	rc = 0;
out:
	strbuf_release(&buffer);
	return rc;
}

static void read_empty(const struct object_id *oid, int verbose)
{
	int i = 0;
	const char *args[7];

	args[i++] = "read-tree";
	if (verbose)
		args[i++] = "-v";
	args[i++] = "-m";
	args[i++] = "-u";
	args[i++] = empty_tree_oid_hex();
	args[i++] = oid_to_hex(oid);
	args[i] = NULL;

	if (run_command_v_opt(args, RUN_GIT_CMD))
		die(_("read-tree failed"));
}

static void reset_hard(const struct object_id *oid, int verbose)
{
	int i = 0;
	const char *args[6];

	args[i++] = "read-tree";
	if (verbose)
		args[i++] = "-v";
	args[i++] = "--reset";
	args[i++] = "-u";
	args[i++] = oid_to_hex(oid);
	args[i] = NULL;

	if (run_command_v_opt(args, RUN_GIT_CMD))
		die(_("read-tree failed"));
}

static void restore_state(const struct object_id *head,
			  const struct object_id *stash)
{
	struct strbuf sb = STRBUF_INIT;
	const char *args[] = { "stash", "apply", NULL, NULL };

	if (is_null_oid(stash))
		return;

	reset_hard(head, 1);

	args[2] = oid_to_hex(stash);

	/*
	 * It is OK to ignore error here, for example when there was
	 * nothing to restore.
	 */
	run_command_v_opt(args, RUN_GIT_CMD);

	strbuf_release(&sb);
	refresh_cache(REFRESH_QUIET);
}

/* This is called when no merge was necessary. */
static void finish_up_to_date(void)
{
	if (verbosity >= 0) {
		if (squash)
			puts(_("Already up to date. (nothing to squash)"));
		else
			puts(_("Already up to date."));
	}
	remove_merge_branch_state(the_repository);
}

static void squash_message(struct commit *commit, struct commit_list *remoteheads)
{
	struct rev_info rev;
	struct strbuf out = STRBUF_INIT;
	struct commit_list *j;
	struct pretty_print_context ctx = {0};

	printf(_("Squash commit -- not updating HEAD\n"));

	repo_init_revisions(the_repository, &rev, NULL);
	diff_merges_suppress(&rev);
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
			oid_to_hex(&commit->object.oid));
		pretty_print_commit(&ctx, commit, &out);
	}
	write_file_buf(git_path_squash_msg(the_repository), out.buf, out.len);
	strbuf_release(&out);
}

static void finish(struct commit *head_commit,
		   struct commit_list *remoteheads,
		   const struct object_id *new_head, const char *msg)
{
	struct strbuf reflog_message = STRBUF_INIT;
	const struct object_id *head = &head_commit->object.oid;

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
			update_ref(reflog_message.buf, "HEAD", new_head, head,
				   0, UPDATE_REFS_DIE_ON_ERR);
			/*
			 * We ignore errors in 'gc --auto', since the
			 * user should see them.
			 */
			run_auto_maintenance(verbosity < 0);
		}
	}
	if (new_head && show_diffstat) {
		struct diff_options opts;
		repo_diff_setup(the_repository, &opts);
		opts.stat_width = -1; /* use full terminal width */
		opts.stat_graph_width = -1; /* respect statGraphWidth config */
		opts.output_format |=
			DIFF_FORMAT_SUMMARY | DIFF_FORMAT_DIFFSTAT;
		opts.detect_rename = DIFF_DETECT_RENAME;
		diff_setup_done(&opts);
		diff_tree_oid(head, new_head, "", &opts);
		diffcore_std(&opts);
		diff_flush(&opts);
	}

	/* Run a post-merge hook */
	run_hook_le(NULL, "post-merge", squash ? "1" : "0", NULL);

	apply_autostash(git_path_merge_autostash(the_repository));
	strbuf_release(&reflog_message);
}

/* Get the name for the merge commit's message. */
static void merge_name(const char *remote, struct strbuf *msg)
{
	struct commit *remote_head;
	struct object_id branch_head;
	struct strbuf buf = STRBUF_INIT;
	struct strbuf bname = STRBUF_INIT;
	struct merge_remote_desc *desc;
	const char *ptr;
	char *found_ref = NULL;
	int len, early;

	strbuf_branchname(&bname, remote, 0);
	remote = bname.buf;

	oidclr(&branch_head);
	remote_head = get_merge_parent(remote);
	if (!remote_head)
		die(_("'%s' does not point to a commit"), remote);

	if (dwim_ref(remote, strlen(remote), &branch_head, &found_ref, 0) > 0) {
		if (starts_with(found_ref, "refs/heads/")) {
			strbuf_addf(msg, "%s\t\tbranch '%s' of .\n",
				    oid_to_hex(&branch_head), remote);
			goto cleanup;
		}
		if (starts_with(found_ref, "refs/tags/")) {
			strbuf_addf(msg, "%s\t\ttag '%s' of .\n",
				    oid_to_hex(&branch_head), remote);
			goto cleanup;
		}
		if (starts_with(found_ref, "refs/remotes/")) {
			strbuf_addf(msg, "%s\t\tremote-tracking branch '%s' of .\n",
				    oid_to_hex(&branch_head), remote);
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
		strbuf_addf(&truname, "refs/heads/%s", remote);
		strbuf_setlen(&truname, truname.len - len);
		if (ref_exists(truname.buf)) {
			strbuf_addf(msg,
				    "%s\t\tbranch '%s'%s of .\n",
				    oid_to_hex(&remote_head->object.oid),
				    truname.buf + 11,
				    (early ? " (early part)" : ""));
			strbuf_release(&truname);
			goto cleanup;
		}
		strbuf_release(&truname);
	}

	desc = merge_remote_util(remote_head);
	if (desc && desc->obj && desc->obj->type == OBJ_TAG) {
		strbuf_addf(msg, "%s\t\t%s '%s'\n",
			    oid_to_hex(&desc->obj->oid),
			    type_name(desc->obj->type),
			    remote);
		goto cleanup;
	}

	strbuf_addf(msg, "%s\t\tcommit '%s'\n",
		oid_to_hex(&remote_head->object.oid), remote);
cleanup:
	free(found_ref);
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
		    _(split_cmdline_strerror(argc)));
	REALLOC_ARRAY(argv, argc + 2);
	MOVE_ARRAY(argv + 1, argv, argc + 1);
	argc++;
	argv[0] = "branch.*.mergeoptions";
	parse_options(argc, argv, NULL, builtin_merge_options,
		      builtin_merge_usage, 0);
	free(argv);
}

static int git_merge_config(const char *k, const char *v, void *cb)
{
	int status;
	const char *str;

	if (branch &&
	    skip_prefix(k, "branch.", &str) &&
	    skip_prefix(str, branch, &str) &&
	    !strcmp(str, ".mergeoptions")) {
		free(branch_mergeoptions);
		branch_mergeoptions = xstrdup(v);
		return 0;
	}

	if (!strcmp(k, "merge.diffstat") || !strcmp(k, "merge.stat"))
		show_diffstat = git_config_bool(k, v);
	else if (!strcmp(k, "merge.verifysignatures"))
		verify_signatures = git_config_bool(k, v);
	else if (!strcmp(k, "pull.twohead"))
		return git_config_string(&pull_twohead, k, v);
	else if (!strcmp(k, "pull.octopus"))
		return git_config_string(&pull_octopus, k, v);
	else if (!strcmp(k, "commit.cleanup"))
		return git_config_string(&cleanup_arg, k, v);
	else if (!strcmp(k, "merge.ff")) {
		int boolval = git_parse_maybe_bool(v);
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
	} else if (!strcmp(k, "gpg.mintrustlevel")) {
		check_trust_level = 0;
	} else if (!strcmp(k, "merge.autostash")) {
		autostash = git_config_bool(k, v);
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

static int read_tree_trivial(struct object_id *common, struct object_id *head,
			     struct object_id *one)
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
	opts.preserve_ignored = 0; /* FIXME: !overwrite_ignore */
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

static void write_tree_trivial(struct object_id *oid)
{
	if (write_cache_as_tree(oid, 0, NULL))
		die(_("git write-tree failed to write a tree"));
}

static int try_merge_strategy(const char *strategy, struct commit_list *common,
			      struct commit_list *remoteheads,
			      struct commit *head)
{
	const char *head_arg = "HEAD";

	if (refresh_and_write_cache(REFRESH_QUIET, SKIP_IF_UNCHANGED, 0) < 0)
		return error(_("Unable to write index."));

	if (!strcmp(strategy, "recursive") || !strcmp(strategy, "subtree") ||
	    !strcmp(strategy, "ort")) {
		struct lock_file lock = LOCK_INIT;
		int clean, x;
		struct commit *result;
		struct commit_list *reversed = NULL;
		struct merge_options o;
		struct commit_list *j;

		if (remoteheads->next) {
			error(_("Not handling anything other than two heads merge."));
			return 2;
		}

		init_merge_options(&o, the_repository);
		if (!strcmp(strategy, "subtree"))
			o.subtree_shift = "";

		o.show_rename_progress =
			show_progress == -1 ? isatty(2) : show_progress;

		for (x = 0; x < xopts_nr; x++)
			if (parse_merge_opt(&o, xopts[x]))
				die(_("unknown strategy option: -X%s"), xopts[x]);

		o.branch1 = head_arg;
		o.branch2 = merge_remote_util(remoteheads->item)->name;

		for (j = common; j; j = j->next)
			commit_list_insert(j->item, &reversed);

		hold_locked_index(&lock, LOCK_DIE_ON_ERROR);
		if (!strcmp(strategy, "ort"))
			clean = merge_ort_recursive(&o, head, remoteheads->item,
						    reversed, &result);
		else
			clean = merge_recursive(&o, head, remoteheads->item,
						reversed, &result);
		if (clean < 0)
			exit(128);
		if (write_locked_index(&the_index, &lock,
				       COMMIT_LOCK | SKIP_IF_UNCHANGED))
			die(_("unable to write %s"), get_index_file());
		return clean ? 0 : 1;
	} else {
		return try_merge_command(the_repository,
					 strategy, xopts_nr, xopts,
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

static void add_strategies(const char *string, unsigned attr)
{
	int i;

	if (string) {
		struct string_list list = STRING_LIST_INIT_DUP;
		struct string_list_item *item;
		string_list_split(&list, string, ' ', -1);
		for_each_string_list_item(item, &list)
			append_strategy(get_strategy(item->string));
		string_list_clear(&list, 0);
		return;
	}
	for (i = 0; i < ARRAY_SIZE(all_strategy); i++)
		if (all_strategy[i].attr & attr)
			append_strategy(&all_strategy[i]);

}

static void read_merge_msg(struct strbuf *msg)
{
	const char *filename = git_path_merge_msg(the_repository);
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
   "\n");

static const char scissors_editor_comment[] =
N_("An empty message aborts the commit.\n");

static const char no_scissors_editor_comment[] =
N_("Lines starting with '%c' will be ignored, and an empty message aborts\n"
   "the commit.\n");

static void write_merge_heads(struct commit_list *);
static void prepare_to_commit(struct commit_list *remoteheads)
{
	struct strbuf msg = STRBUF_INIT;
	const char *index_file = get_index_file();

	if (!no_verify && run_commit_hook(0 < option_edit, index_file, "pre-merge-commit", NULL))
		abort_commit(remoteheads, NULL);
	/*
	 * Re-read the index as pre-merge-commit hook could have updated it,
	 * and write it out as a tree.  We must do this before we invoke
	 * the editor and after we invoke run_status above.
	 */
	if (hook_exists("pre-merge-commit"))
		discard_cache();
	read_cache_from(index_file);
	strbuf_addbuf(&msg, &merge_msg);
	if (squash)
		BUG("the control must not reach here under --squash");
	if (0 < option_edit) {
		strbuf_addch(&msg, '\n');
		if (cleanup_mode == COMMIT_MSG_CLEANUP_SCISSORS) {
			wt_status_append_cut_line(&msg);
			strbuf_commented_addf(&msg, "\n");
		}
		strbuf_commented_addf(&msg, _(merge_editor_comment));
		if (cleanup_mode == COMMIT_MSG_CLEANUP_SCISSORS)
			strbuf_commented_addf(&msg, _(scissors_editor_comment));
		else
			strbuf_commented_addf(&msg,
				_(no_scissors_editor_comment), comment_line_char);
	}
	if (signoff)
		append_signoff(&msg, ignore_non_trailer(msg.buf, msg.len), 0);
	write_merge_heads(remoteheads);
	write_file_buf(git_path_merge_msg(the_repository), msg.buf, msg.len);
	if (run_commit_hook(0 < option_edit, get_index_file(), "prepare-commit-msg",
			    git_path_merge_msg(the_repository), "merge", NULL))
		abort_commit(remoteheads, NULL);
	if (0 < option_edit) {
		if (launch_editor(git_path_merge_msg(the_repository), NULL, NULL))
			abort_commit(remoteheads, NULL);
	}

	if (!no_verify && run_commit_hook(0 < option_edit, get_index_file(),
					  "commit-msg",
					  git_path_merge_msg(the_repository), NULL))
		abort_commit(remoteheads, NULL);

	read_merge_msg(&msg);
	cleanup_message(&msg, cleanup_mode, 0);
	if (!msg.len)
		abort_commit(remoteheads, _("Empty commit message."));
	strbuf_release(&merge_msg);
	strbuf_addbuf(&merge_msg, &msg);
	strbuf_release(&msg);
}

static int merge_trivial(struct commit *head, struct commit_list *remoteheads)
{
	struct object_id result_tree, result_commit;
	struct commit_list *parents, **pptr = &parents;

	if (refresh_and_write_cache(REFRESH_QUIET, SKIP_IF_UNCHANGED, 0) < 0)
		return error(_("Unable to write index."));

	write_tree_trivial(&result_tree);
	printf(_("Wonderful.\n"));
	pptr = commit_list_append(head, pptr);
	pptr = commit_list_append(remoteheads->item, pptr);
	prepare_to_commit(remoteheads);
	if (commit_tree(merge_msg.buf, merge_msg.len, &result_tree, parents,
			&result_commit, NULL, sign_commit))
		die(_("failed to write commit object"));
	finish(head, remoteheads, &result_commit, "In-index merge");
	remove_merge_branch_state(the_repository);
	return 0;
}

static int finish_automerge(struct commit *head,
			    int head_subsumed,
			    struct commit_list *common,
			    struct commit_list *remoteheads,
			    struct object_id *result_tree,
			    const char *wt_strategy)
{
	struct commit_list *parents = NULL;
	struct strbuf buf = STRBUF_INIT;
	struct object_id result_commit;

	write_tree_trivial(result_tree);
	free_commit_list(common);
	parents = remoteheads;
	if (!head_subsumed || fast_forward == FF_NO)
		commit_list_insert(head, &parents);
	prepare_to_commit(remoteheads);
	if (commit_tree(merge_msg.buf, merge_msg.len, result_tree, parents,
			&result_commit, NULL, sign_commit))
		die(_("failed to write commit object"));
	strbuf_addf(&buf, "Merge made by the '%s' strategy.", wt_strategy);
	finish(head, remoteheads, &result_commit, buf.buf);
	strbuf_release(&buf);
	remove_merge_branch_state(the_repository);
	return 0;
}

static int suggest_conflicts(void)
{
	const char *filename;
	FILE *fp;
	struct strbuf msgbuf = STRBUF_INIT;

	filename = git_path_merge_msg(the_repository);
	fp = xfopen(filename, "a");

	/*
	 * We can't use cleanup_mode because if we're not using the editor,
	 * get_cleanup_mode will return COMMIT_MSG_CLEANUP_SPACE instead, even
	 * though the message is meant to be processed later by git-commit.
	 * Thus, we will get the cleanup mode which is returned when we _are_
	 * using an editor.
	 */
	append_conflicts_hint(&the_index, &msgbuf,
			      get_cleanup_mode(cleanup_arg, 1));
	fputs(msgbuf.buf, fp);
	strbuf_release(&msgbuf);
	fclose(fp);
	repo_rerere(the_repository, allow_rerere_auto);
	printf(_("Automatic merge failed; "
			"fix conflicts and then commit the result.\n"));
	return 1;
}

static int evaluate_result(void)
{
	int cnt = 0;
	struct rev_info rev;

	/* Check how many files differ. */
	repo_init_revisions(the_repository, &rev, "");
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
	if (!branch->remote_name)
		die(_("No remote for the current branch."));
	if (!branch->merge_nr)
		die(_("No default upstream defined for the current branch."));

	args = xcalloc(st_add(branch->merge_nr, 1), sizeof(char *));
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

static void write_merge_heads(struct commit_list *remoteheads)
{
	struct commit_list *j;
	struct strbuf buf = STRBUF_INIT;

	for (j = remoteheads; j; j = j->next) {
		struct object_id *oid;
		struct commit *c = j->item;
		struct merge_remote_desc *desc;

		desc = merge_remote_util(c);
		if (desc && desc->obj) {
			oid = &desc->obj->oid;
		} else {
			oid = &c->object.oid;
		}
		strbuf_addf(&buf, "%s\n", oid_to_hex(oid));
	}
	write_file_buf(git_path_merge_head(the_repository), buf.buf, buf.len);

	strbuf_reset(&buf);
	if (fast_forward == FF_NO)
		strbuf_addstr(&buf, "no-ff");
	write_file_buf(git_path_merge_mode(the_repository), buf.buf, buf.len);
	strbuf_release(&buf);
}

static void write_merge_state(struct commit_list *remoteheads)
{
	write_merge_heads(remoteheads);
	strbuf_addch(&merge_msg, '\n');
	write_file_buf(git_path_merge_msg(the_repository), merge_msg.buf,
		       merge_msg.len);
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
		int v = git_parse_maybe_bool(e);
		if (v < 0)
			die(_("Bad value '%s' in environment '%s'"), e, name);
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

static struct commit_list *reduce_parents(struct commit *head_commit,
					  int *head_subsumed,
					  struct commit_list *remoteheads)
{
	struct commit_list *parents, **remotes;

	/*
	 * Is the current HEAD reachable from another commit being
	 * merged?  If so we do not want to record it as a parent of
	 * the resulting merge, unless --no-ff is given.  We will flip
	 * this variable to 0 when we find HEAD among the independent
	 * tips being merged.
	 */
	*head_subsumed = 1;

	/* Find what parents to record by checking independent ones. */
	parents = reduce_heads(remoteheads);
	free_commit_list(remoteheads);

	remoteheads = NULL;
	remotes = &remoteheads;
	while (parents) {
		struct commit *commit = pop_commit(&parents);
		if (commit == head_commit)
			*head_subsumed = 0;
		else
			remotes = &commit_list_insert(commit, remotes)->next;
	}
	return remoteheads;
}

static void prepare_merge_message(struct strbuf *merge_names, struct strbuf *merge_msg)
{
	struct fmt_merge_msg_opts opts;

	memset(&opts, 0, sizeof(opts));
	opts.add_title = !have_message;
	opts.shortlog_len = shortlog_len;
	opts.credit_people = (0 < option_edit);

	fmt_merge_msg(merge_names, merge_msg, &opts);
	if (merge_msg->len)
		strbuf_setlen(merge_msg, merge_msg->len - 1);
}

static void handle_fetch_head(struct commit_list **remotes, struct strbuf *merge_names)
{
	const char *filename;
	int fd, pos, npos;
	struct strbuf fetch_head_file = STRBUF_INIT;
	const unsigned hexsz = the_hash_algo->hexsz;

	if (!merge_names)
		merge_names = &fetch_head_file;

	filename = git_path_fetch_head(the_repository);
	fd = xopen(filename, O_RDONLY);

	if (strbuf_read(merge_names, fd, 0) < 0)
		die_errno(_("could not read '%s'"), filename);
	if (close(fd) < 0)
		die_errno(_("could not close '%s'"), filename);

	for (pos = 0; pos < merge_names->len; pos = npos) {
		struct object_id oid;
		char *ptr;
		struct commit *commit;

		ptr = strchr(merge_names->buf + pos, '\n');
		if (ptr)
			npos = ptr - merge_names->buf + 1;
		else
			npos = merge_names->len;

		if (npos - pos < hexsz + 2 ||
		    get_oid_hex(merge_names->buf + pos, &oid))
			commit = NULL; /* bad */
		else if (memcmp(merge_names->buf + pos + hexsz, "\t\t", 2))
			continue; /* not-for-merge */
		else {
			char saved = merge_names->buf[pos + hexsz];
			merge_names->buf[pos + hexsz] = '\0';
			commit = get_merge_parent(merge_names->buf + pos);
			merge_names->buf[pos + hexsz] = saved;
		}
		if (!commit) {
			if (ptr)
				*ptr = '\0';
			die(_("not something we can merge in %s: %s"),
			    filename, merge_names->buf + pos);
		}
		remotes = &commit_list_insert(commit, remotes)->next;
	}

	if (merge_names == &fetch_head_file)
		strbuf_release(&fetch_head_file);
}

static struct commit_list *collect_parents(struct commit *head_commit,
					   int *head_subsumed,
					   int argc, const char **argv,
					   struct strbuf *merge_msg)
{
	int i;
	struct commit_list *remoteheads = NULL;
	struct commit_list **remotes = &remoteheads;
	struct strbuf merge_names = STRBUF_INIT, *autogen = NULL;

	if (merge_msg && (!have_message || shortlog_len))
		autogen = &merge_names;

	if (head_commit)
		remotes = &commit_list_insert(head_commit, remotes)->next;

	if (argc == 1 && !strcmp(argv[0], "FETCH_HEAD")) {
		handle_fetch_head(remotes, autogen);
		remoteheads = reduce_parents(head_commit, head_subsumed, remoteheads);
	} else {
		for (i = 0; i < argc; i++) {
			struct commit *commit = get_merge_parent(argv[i]);
			if (!commit)
				help_unknown_ref(argv[i], "merge",
						 _("not something we can merge"));
			remotes = &commit_list_insert(commit, remotes)->next;
		}
		remoteheads = reduce_parents(head_commit, head_subsumed, remoteheads);
		if (autogen) {
			struct commit_list *p;
			for (p = remoteheads; p; p = p->next)
				merge_name(merge_remote_util(p->item)->name, autogen);
		}
	}

	if (autogen) {
		prepare_merge_message(autogen, merge_msg);
		strbuf_release(autogen);
	}

	return remoteheads;
}

static int merging_a_throwaway_tag(struct commit *commit)
{
	char *tag_ref;
	struct object_id oid;
	int is_throwaway_tag = 0;

	/* Are we merging a tag? */
	if (!merge_remote_util(commit) ||
	    !merge_remote_util(commit)->obj ||
	    merge_remote_util(commit)->obj->type != OBJ_TAG)
		return is_throwaway_tag;

	/*
	 * Now we know we are merging a tag object.  Are we downstream
	 * and following the tags from upstream?  If so, we must have
	 * the tag object pointed at by "refs/tags/$T" where $T is the
	 * tagname recorded in the tag object.  We want to allow such
	 * a "just to catch up" merge to fast-forward.
	 *
	 * Otherwise, we are playing an integrator's role, making a
	 * merge with a throw-away tag from a contributor with
	 * something like "git pull $contributor $signed_tag".
	 * We want to forbid such a merge from fast-forwarding
	 * by default; otherwise we would not keep the signature
	 * anywhere.
	 */
	tag_ref = xstrfmt("refs/tags/%s",
			  ((struct tag *)merge_remote_util(commit)->obj)->tag);
	if (!read_ref(tag_ref, &oid) &&
	    oideq(&oid, &merge_remote_util(commit)->obj->oid))
		is_throwaway_tag = 0;
	else
		is_throwaway_tag = 1;
	free(tag_ref);
	return is_throwaway_tag;
}

int cmd_merge(int argc, const char **argv, const char *prefix)
{
	struct object_id result_tree, stash, head_oid;
	struct commit *head_commit;
	struct strbuf buf = STRBUF_INIT;
	int i, ret = 0, head_subsumed;
	int best_cnt = -1, merge_was_ok = 0, automerge_was_ok = 0;
	struct commit_list *common = NULL;
	const char *best_strategy = NULL, *wt_strategy = NULL;
	struct commit_list *remoteheads, *p;
	void *branch_to_free;
	int orig_argc = argc;

	if (argc == 2 && !strcmp(argv[1], "-h"))
		usage_with_options(builtin_merge_usage, builtin_merge_options);

	prepare_repo_settings(the_repository);
	the_repository->settings.command_requires_full_index = 0;

	/*
	 * Check if we are _not_ on a detached HEAD, i.e. if there is a
	 * current branch.
	 */
	branch = branch_to_free = resolve_refdup("HEAD", 0, &head_oid, NULL);
	if (branch)
		skip_prefix(branch, "refs/heads/", &branch);

	if (!pull_twohead) {
		char *default_strategy = getenv("GIT_TEST_MERGE_ALGORITHM");
		if (default_strategy && !strcmp(default_strategy, "ort"))
			pull_twohead = "ort";
	}

	init_diff_ui_defaults();
	git_config(git_merge_config, NULL);

	if (!branch || is_null_oid(&head_oid))
		head_commit = NULL;
	else
		head_commit = lookup_commit_or_die(&head_oid, "HEAD");

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
		struct strbuf stash_oid = STRBUF_INIT;

		if (orig_argc != 2)
			usage_msg_opt(_("--abort expects no arguments"),
			      builtin_merge_usage, builtin_merge_options);

		if (!file_exists(git_path_merge_head(the_repository)))
			die(_("There is no merge to abort (MERGE_HEAD missing)."));

		if (read_oneliner(&stash_oid, git_path_merge_autostash(the_repository),
		    READ_ONELINER_SKIP_IF_EMPTY))
			unlink(git_path_merge_autostash(the_repository));

		/* Invoke 'git reset --merge' */
		ret = cmd_reset(nargc, nargv, prefix);

		if (stash_oid.len)
			apply_autostash_oid(stash_oid.buf);

		strbuf_release(&stash_oid);
		goto done;
	}

	if (quit_current_merge) {
		if (orig_argc != 2)
			usage_msg_opt(_("--quit expects no arguments"),
				      builtin_merge_usage,
				      builtin_merge_options);

		remove_merge_branch_state(the_repository);
		goto done;
	}

	if (continue_current_merge) {
		int nargc = 1;
		const char *nargv[] = {"commit", NULL};

		if (orig_argc != 2)
			usage_msg_opt(_("--continue expects no arguments"),
			      builtin_merge_usage, builtin_merge_options);

		if (!file_exists(git_path_merge_head(the_repository)))
			die(_("There is no merge in progress (MERGE_HEAD missing)."));

		/* Invoke 'git commit' */
		ret = cmd_commit(nargc, nargv, prefix);
		goto done;
	}

	if (read_cache_unmerged())
		die_resolve_conflict("merge");

	if (file_exists(git_path_merge_head(the_repository))) {
		/*
		 * There is no unmerged entry, don't advise 'git
		 * add/rm <file>', just 'git commit'.
		 */
		if (advice_enabled(ADVICE_RESOLVE_CONFLICT))
			die(_("You have not concluded your merge (MERGE_HEAD exists).\n"
				  "Please, commit your changes before you merge."));
		else
			die(_("You have not concluded your merge (MERGE_HEAD exists)."));
	}
	if (ref_exists("CHERRY_PICK_HEAD")) {
		if (advice_enabled(ADVICE_RESOLVE_CONFLICT))
			die(_("You have not concluded your cherry-pick (CHERRY_PICK_HEAD exists).\n"
			    "Please, commit your changes before you merge."));
		else
			die(_("You have not concluded your cherry-pick (CHERRY_PICK_HEAD exists)."));
	}
	resolve_undo_clear();

	if (option_edit < 0)
		option_edit = default_edit_option();

	cleanup_mode = get_cleanup_mode(cleanup_arg, 0 < option_edit);

	if (verbosity < 0)
		show_diffstat = 0;

	if (squash) {
		if (fast_forward == FF_NO)
			die(_("You cannot combine --squash with --no-ff."));
		if (option_commit > 0)
			die(_("You cannot combine --squash with --commit."));
		/*
		 * squash can now silently disable option_commit - this is not
		 * a problem as it is only overriding the default, not a user
		 * supplied option.
		 */
		option_commit = 0;
	}

	if (option_commit < 0)
		option_commit = 1;

	if (!argc) {
		if (default_to_upstream)
			argc = setup_with_upstream(&argv);
		else
			die(_("No commit specified and merge.defaultToUpstream not set."));
	} else if (argc == 1 && !strcmp(argv[0], "-")) {
		argv[0] = "@{-1}";
	}

	if (!argc)
		usage_with_options(builtin_merge_usage,
			builtin_merge_options);

	if (!head_commit) {
		/*
		 * If the merged head is a valid one there is no reason
		 * to forbid "git merge" into a branch yet to be born.
		 * We do the same for "git pull".
		 */
		struct object_id *remote_head_oid;
		if (squash)
			die(_("Squash commit into empty head not supported yet"));
		if (fast_forward == FF_NO)
			die(_("Non-fast-forward commit does not make sense into "
			    "an empty head"));
		remoteheads = collect_parents(head_commit, &head_subsumed,
					      argc, argv, NULL);
		if (!remoteheads)
			die(_("%s - not something we can merge"), argv[0]);
		if (remoteheads->next)
			die(_("Can merge only exactly one commit into empty head"));

		if (verify_signatures)
			verify_merge_signature(remoteheads->item, verbosity,
					       check_trust_level);

		remote_head_oid = &remoteheads->item->object.oid;
		read_empty(remote_head_oid, 0);
		update_ref("initial pull", "HEAD", remote_head_oid, NULL, 0,
			   UPDATE_REFS_DIE_ON_ERR);
		goto done;
	}

	/*
	 * All the rest are the commits being merged; prepare
	 * the standard merge summary message to be appended
	 * to the given message.
	 */
	remoteheads = collect_parents(head_commit, &head_subsumed,
				      argc, argv, &merge_msg);

	if (!head_commit || !argc)
		usage_with_options(builtin_merge_usage,
			builtin_merge_options);

	if (verify_signatures) {
		for (p = remoteheads; p; p = p->next) {
			verify_merge_signature(p->item, verbosity,
					       check_trust_level);
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
			    oid_to_hex(&commit->object.oid));
		setenv(buf.buf, merge_remote_util(commit)->name, 1);
		strbuf_reset(&buf);
		if (fast_forward != FF_ONLY && merging_a_throwaway_tag(commit))
			fast_forward = FF_NO;
	}

	if (!use_strategies && !pull_twohead &&
	    remoteheads && !remoteheads->next) {
		char *default_strategy = getenv("GIT_TEST_MERGE_ALGORITHM");
		if (default_strategy)
			append_strategy(get_strategy(default_strategy));
	}
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
		common = get_merge_bases(head_commit, remoteheads->item);
	else {
		struct commit_list *list = remoteheads;
		commit_list_insert(head_commit, &list);
		common = get_octopus_merge_bases(list);
		free(list);
	}

	update_ref("updating ORIG_HEAD", "ORIG_HEAD",
		   &head_commit->object.oid, NULL, 0, UPDATE_REFS_DIE_ON_ERR);

	if (remoteheads && !common) {
		/* No common ancestors found. */
		if (!allow_unrelated_histories)
			die(_("refusing to merge unrelated histories"));
		/* otherwise, we need a real merge. */
	} else if (!remoteheads ||
		 (!remoteheads->next && !common->next &&
		  common->item == remoteheads->item)) {
		/*
		 * If head can reach all the merge then we are up to date.
		 * but first the most common case of merging one remote.
		 */
		finish_up_to_date();
		goto done;
	} else if (fast_forward != FF_NO && !remoteheads->next &&
			!common->next &&
			oideq(&common->item->object.oid, &head_commit->object.oid)) {
		/* Again the most common case of merging one remote. */
		struct strbuf msg = STRBUF_INIT;
		struct commit *commit;

		if (verbosity >= 0) {
			printf(_("Updating %s..%s\n"),
			       find_unique_abbrev(&head_commit->object.oid,
						  DEFAULT_ABBREV),
			       find_unique_abbrev(&remoteheads->item->object.oid,
						  DEFAULT_ABBREV));
		}
		strbuf_addstr(&msg, "Fast-forward");
		if (have_message)
			strbuf_addstr(&msg,
				" (no commit created; -m option ignored)");
		commit = remoteheads->item;
		if (!commit) {
			ret = 1;
			goto done;
		}

		if (autostash)
			create_autostash(the_repository,
					 git_path_merge_autostash(the_repository));
		if (checkout_fast_forward(the_repository,
					  &head_commit->object.oid,
					  &commit->object.oid,
					  overwrite_ignore)) {
			apply_autostash(git_path_merge_autostash(the_repository));
			ret = 1;
			goto done;
		}

		finish(head_commit, remoteheads, &commit->object.oid, msg.buf);
		remove_merge_branch_state(the_repository);
		strbuf_release(&msg);
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
			if (!read_tree_trivial(&common->item->object.oid,
					       &head_commit->object.oid,
					       &remoteheads->item->object.oid)) {
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
			common_one = get_merge_bases(head_commit, j->item);
			if (!oideq(&common_one->item->object.oid, &j->item->object.oid)) {
				up_to_date = 0;
				break;
			}
		}
		if (up_to_date) {
			finish_up_to_date();
			goto done;
		}
	}

	if (fast_forward == FF_ONLY)
		die_ff_impossible();

	if (autostash)
		create_autostash(the_repository,
				 git_path_merge_autostash(the_repository));

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
	    save_state(&stash))
		oidclr(&stash);

	for (i = 0; !merge_was_ok && i < use_strategies_nr; i++) {
		int ret, cnt;
		if (i) {
			printf(_("Rewinding the tree to pristine...\n"));
			restore_state(&head_commit->object.oid, &stash);
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
					 head_commit);
		/*
		 * The backend exits with 1 when conflicts are
		 * left to be resolved, with 2 when it does not
		 * handle the given merge at all.
		 */
		if (ret < 2) {
			if (!ret) {
				if (option_commit) {
					/* Automerge succeeded. */
					automerge_was_ok = 1;
					break;
				}
				merge_was_ok = 1;
			}
			cnt = (use_strategies_nr > 1) ? evaluate_result() : 0;
			if (best_cnt <= 0 || cnt <= best_cnt) {
				best_strategy = use_strategies[i]->name;
				best_cnt = cnt;
			}
		}
	}

	/*
	 * If we have a resulting tree, that means the strategy module
	 * auto resolved the merge cleanly.
	 */
	if (automerge_was_ok) {
		ret = finish_automerge(head_commit, head_subsumed,
				       common, remoteheads,
				       &result_tree, wt_strategy);
		goto done;
	}

	/*
	 * Pick the result from the best strategy and have the user fix
	 * it up.
	 */
	if (!best_strategy) {
		restore_state(&head_commit->object.oid, &stash);
		if (use_strategies_nr > 1)
			fprintf(stderr,
				_("No merge strategy handled the merge.\n"));
		else
			fprintf(stderr, _("Merge with strategy %s failed.\n"),
				use_strategies[0]->name);
		apply_autostash(git_path_merge_autostash(the_repository));
		ret = 2;
		goto done;
	} else if (best_strategy == wt_strategy)
		; /* We already have its result in the working tree. */
	else {
		printf(_("Rewinding the tree to pristine...\n"));
		restore_state(&head_commit->object.oid, &stash);
		printf(_("Using the %s strategy to prepare resolving by hand.\n"),
			best_strategy);
		try_merge_strategy(best_strategy, common, remoteheads,
				   head_commit);
	}

	if (squash) {
		finish(head_commit, remoteheads, NULL, NULL);

		git_test_write_commit_graph_or_die();
	} else
		write_merge_state(remoteheads);

	if (merge_was_ok)
		fprintf(stderr, _("Automatic merge went well; "
			"stopped before committing as requested\n"));
	else
		ret = suggest_conflicts();

done:
	strbuf_release(&buf);
	free(branch_to_free);
	return ret;
}
