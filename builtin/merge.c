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

#define DEFAULT_TWOHEAD (1<<0)
#define DEFAULT_OCTOPUS (1<<1)
#define NO_FAST_FORWARD (1<<2)
#define NO_TRIVIAL      (1<<3)

struct strategy {
	const char *name;
	unsigned attr;
};

static const char * const builtin_merge_usage[] = {
	"git merge [options] [<commit>...]",
	"git merge [options] <msg> HEAD <commit>",
	"git merge --abort",
	NULL
};

static int show_diffstat = 1, shortlog_len, squash;
static int option_commit = 1, allow_fast_forward = 1;
static int fast_forward_only, option_edit;
static int allow_trivial = 1, have_message;
static struct strbuf merge_msg;
static struct commit_list *remoteheads;
static unsigned char head[20], stash[20];
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
static int default_to_upstream;

static struct strategy all_strategy[] = {
	{ "recursive",  DEFAULT_TWOHEAD | NO_TRIVIAL },
	{ "octopus",    DEFAULT_OCTOPUS },
	{ "resolve",    0 },
	{ "ours",       NO_FAST_FORWARD | NO_TRIVIAL },
	{ "subtree",    NO_FAST_FORWARD | NO_TRIVIAL },
};

static const char *pull_twohead, *pull_octopus;

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
		"do not show a diffstat at the end of the merge",
		PARSE_OPT_NOARG, option_parse_n },
	OPT_BOOLEAN(0, "stat", &show_diffstat,
		"show a diffstat at the end of the merge"),
	OPT_BOOLEAN(0, "summary", &show_diffstat, "(synonym to --stat)"),
	{ OPTION_INTEGER, 0, "log", &shortlog_len, "n",
	  "add (at most <n>) entries from shortlog to merge commit message",
	  PARSE_OPT_OPTARG, NULL, DEFAULT_MERGE_LOG_LEN },
	OPT_BOOLEAN(0, "squash", &squash,
		"create a single commit instead of doing a merge"),
	OPT_BOOLEAN(0, "commit", &option_commit,
		"perform a commit if the merge succeeds (default)"),
	OPT_BOOLEAN('e', "edit", &option_edit,
		"edit message before committing"),
	OPT_BOOLEAN(0, "ff", &allow_fast_forward,
		"allow fast-forward (default)"),
	OPT_BOOLEAN(0, "ff-only", &fast_forward_only,
		"abort if fast-forward is not possible"),
	OPT_RERERE_AUTOUPDATE(&allow_rerere_auto),
	OPT_CALLBACK('s', "strategy", &use_strategies, "strategy",
		"merge strategy to use", option_parse_strategy),
	OPT_CALLBACK('X', "strategy-option", &xopts, "option=value",
		"option for selected merge strategy", option_parse_x),
	OPT_CALLBACK('m', "message", &merge_msg, "message",
		"merge commit message (for a non-fast-forward merge)",
		option_parse_message),
	OPT__VERBOSITY(&verbosity),
	OPT_BOOLEAN(0, "abort", &abort_current_merge,
		"abort the current in-progress merge"),
	OPT_SET_INT(0, "progress", &show_progress, "force progress reporting", 1),
	OPT_END()
};

/* Cleans up metadata that is uninteresting after a succeeded merge. */
static void drop_save(void)
{
	unlink(git_path("MERGE_HEAD"));
	unlink(git_path("MERGE_MSG"));
	unlink(git_path("MERGE_MODE"));
}

static void save_state(void)
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
	else if (!len)
		return;
	strbuf_setlen(&buffer, buffer.len-1);
	if (get_sha1(buffer.buf, stash))
		die(_("not a valid object: %s"), buffer.buf);
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

static void restore_state(void)
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

static void squash_message(void)
{
	struct rev_info rev;
	struct commit *commit;
	struct strbuf out = STRBUF_INIT;
	struct commit_list *j;
	int fd;
	struct pretty_print_context ctx = {0};

	printf(_("Squash commit -- not updating HEAD\n"));
	fd = open(git_path("SQUASH_MSG"), O_WRONLY | O_CREAT, 0666);
	if (fd < 0)
		die_errno(_("Could not write to '%s'"), git_path("SQUASH_MSG"));

	init_revisions(&rev, NULL);
	rev.ignore_merges = 1;
	rev.commit_format = CMIT_FMT_MEDIUM;

	commit = lookup_commit(head);
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
	if (write(fd, out.buf, out.len) < 0)
		die_errno(_("Writing SQUASH_MSG"));
	if (close(fd))
		die_errno(_("Finishing SQUASH_MSG"));
	strbuf_release(&out);
}

static void finish(const unsigned char *new_head, const char *msg)
{
	struct strbuf reflog_message = STRBUF_INIT;

	if (!msg)
		strbuf_addstr(&reflog_message, getenv("GIT_REFLOG_ACTION"));
	else {
		if (verbosity >= 0)
			printf("%s\n", msg);
		strbuf_addf(&reflog_message, "%s: %s",
			getenv("GIT_REFLOG_ACTION"), msg);
	}
	if (squash) {
		squash_message();
	} else {
		if (verbosity >= 0 && !merge_msg.len)
			printf(_("No merge message -- not updating HEAD\n"));
		else {
			const char *argv_gc_auto[] = { "gc", "--auto", NULL };
			update_ref(reflog_message.buf, "HEAD",
				new_head, head, 0,
				DIE_ON_ERR);
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
		opts.output_format |=
			DIFF_FORMAT_SUMMARY | DIFF_FORMAT_DIFFSTAT;
		opts.detect_rename = DIFF_DETECT_RENAME;
		if (diff_setup_done(&opts) < 0)
			die(_("diff_setup_done failed"));
		diff_tree_sha1(head, new_head, "", &opts);
		diffcore_std(&opts);
		diff_flush(&opts);
	}

	/* Run a post-merge hook */
	run_hook(NULL, "post-merge", squash ? "1" : "0", NULL);

	strbuf_release(&reflog_message);
}

/* Get the name for the merge commit's message. */
static void merge_name(const char *remote, struct strbuf *msg)
{
	struct object *remote_head;
	unsigned char branch_head[20], buf_sha[20];
	struct strbuf buf = STRBUF_INIT;
	struct strbuf bname = STRBUF_INIT;
	const char *ptr;
	char *found_ref;
	int len, early;

	strbuf_branchname(&bname, remote);
	remote = bname.buf;

	memset(branch_head, 0, sizeof(branch_head));
	remote_head = peel_to_type(remote, 0, NULL, OBJ_COMMIT);
	if (!remote_head)
		die(_("'%s' does not point to a commit"), remote);

	if (dwim_ref(remote, strlen(remote), branch_head, &found_ref) > 0) {
		if (!prefixcmp(found_ref, "refs/heads/")) {
			strbuf_addf(msg, "%s\t\tbranch '%s' of .\n",
				    sha1_to_hex(branch_head), remote);
			goto cleanup;
		}
		if (!prefixcmp(found_ref, "refs/remotes/")) {
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
		if (resolve_ref(truname.buf, buf_sha, 1, NULL)) {
			strbuf_addf(msg,
				    "%s\t\tbranch '%s'%s of .\n",
				    sha1_to_hex(remote_head->sha1),
				    truname.buf + 11,
				    (early ? " (early part)" : ""));
			strbuf_release(&truname);
			goto cleanup;
		}
	}

	if (!strcmp(remote, "FETCH_HEAD") &&
			!access(git_path("FETCH_HEAD"), R_OK)) {
		FILE *fp;
		struct strbuf line = STRBUF_INIT;
		char *ptr;

		fp = fopen(git_path("FETCH_HEAD"), "r");
		if (!fp)
			die_errno(_("could not open '%s' for reading"),
				  git_path("FETCH_HEAD"));
		strbuf_getline(&line, fp, '\n');
		fclose(fp);
		ptr = strstr(line.buf, "\tnot-for-merge\t");
		if (ptr)
			strbuf_remove(&line, ptr-line.buf+1, 13);
		strbuf_addbuf(msg, &line);
		strbuf_release(&line);
		goto cleanup;
	}
	strbuf_addf(msg, "%s\t\tcommit '%s'\n",
		sha1_to_hex(remote_head->sha1), remote);
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
	if (branch && !prefixcmp(k, "branch.") &&
		!prefixcmp(k + 7, branch) &&
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
	else if (!strcmp(k, "merge.log") || !strcmp(k, "merge.summary")) {
		int is_bool;
		shortlog_len = git_config_bool_or_int(k, v, &is_bool);
		if (!is_bool && shortlog_len < 0)
			return error(_("%s: negative length %s"), k, v);
		if (is_bool && shortlog_len)
			shortlog_len = DEFAULT_MERGE_LOG_LEN;
		return 0;
	} else if (!strcmp(k, "merge.ff")) {
		int boolval = git_config_maybe_bool(k, v);
		if (0 <= boolval) {
			allow_fast_forward = boolval;
		} else if (v && !strcmp(v, "only")) {
			allow_fast_forward = 1;
			fast_forward_only = 1;
		} /* do not barf on values from future versions of git */
		return 0;
	} else if (!strcmp(k, "merge.defaulttoupstream")) {
		default_to_upstream = git_config_bool(k, v);
		return 0;
	}
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

static const char *merge_argument(struct commit *commit)
{
	if (commit)
		return sha1_to_hex(commit->object.sha1);
	else
		return EMPTY_TREE_SHA1_HEX;
}

int try_merge_command(const char *strategy, size_t xopts_nr,
		      const char **xopts, struct commit_list *common,
		      const char *head_arg, struct commit_list *remotes)
{
	const char **args;
	int i = 0, x = 0, ret;
	struct commit_list *j;
	struct strbuf buf = STRBUF_INIT;

	args = xmalloc((4 + xopts_nr + commit_list_count(common) +
			commit_list_count(remotes)) * sizeof(char *));
	strbuf_addf(&buf, "merge-%s", strategy);
	args[i++] = buf.buf;
	for (x = 0; x < xopts_nr; x++) {
		char *s = xmalloc(strlen(xopts[x])+2+1);
		strcpy(s, "--");
		strcpy(s+2, xopts[x]);
		args[i++] = s;
	}
	for (j = common; j; j = j->next)
		args[i++] = xstrdup(merge_argument(j->item));
	args[i++] = "--";
	args[i++] = head_arg;
	for (j = remotes; j; j = j->next)
		args[i++] = xstrdup(merge_argument(j->item));
	args[i] = NULL;
	ret = run_command_v_opt(args, RUN_GIT_CMD);
	strbuf_release(&buf);
	i = 1;
	for (x = 0; x < xopts_nr; x++)
		free((void *)args[i++]);
	for (j = common; j; j = j->next)
		free((void *)args[i++]);
	i += 2;
	for (j = remotes; j; j = j->next)
		free((void *)args[i++]);
	free(args);
	discard_cache();
	if (read_cache() < 0)
		die(_("failed to read the cache"));
	resolve_undo_clear();

	return ret;
}

static int try_merge_strategy(const char *strategy, struct commit_list *common,
			      const char *head_arg)
{
	int index_fd;
	struct lock_file *lock = xcalloc(1, sizeof(struct lock_file));

	index_fd = hold_locked_index(lock, 1);
	refresh_cache(REFRESH_QUIET);
	if (active_cache_changed &&
			(write_cache(index_fd, active_cache, active_nr) ||
			 commit_locked_index(lock)))
		return error(_("Unable to write index."));
	rollback_lock_file(lock);

	if (!strcmp(strategy, "recursive") || !strcmp(strategy, "subtree")) {
		int clean, x;
		struct commit *result;
		struct lock_file *lock = xcalloc(1, sizeof(struct lock_file));
		int index_fd;
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
		o.branch2 = remoteheads->item->util;

		for (j = common; j; j = j->next)
			commit_list_insert(j->item, &reversed);

		index_fd = hold_locked_index(lock, 1);
		clean = merge_recursive(&o, lookup_commit(head),
				remoteheads->item, reversed, &result);
		if (active_cache_changed &&
				(write_cache(index_fd, active_cache, active_nr) ||
				 commit_locked_index(lock)))
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

int checkout_fast_forward(const unsigned char *head, const unsigned char *remote)
{
	struct tree *trees[MAX_UNPACK_TREES];
	struct unpack_trees_options opts;
	struct tree_desc t[MAX_UNPACK_TREES];
	int i, fd, nr_trees = 0;
	struct dir_struct dir;
	struct lock_file *lock_file = xcalloc(1, sizeof(struct lock_file));

	refresh_cache(REFRESH_QUIET);

	fd = hold_locked_index(lock_file, 1);

	memset(&trees, 0, sizeof(trees));
	memset(&opts, 0, sizeof(opts));
	memset(&t, 0, sizeof(t));
	memset(&dir, 0, sizeof(dir));
	dir.flags |= DIR_SHOW_IGNORED;
	dir.exclude_per_dir = ".gitignore";
	opts.dir = &dir;

	opts.head_idx = 1;
	opts.src_index = &the_index;
	opts.dst_index = &the_index;
	opts.update = 1;
	opts.verbose_update = 1;
	opts.merge = 1;
	opts.fn = twoway_merge;
	setup_unpack_trees_porcelain(&opts, "merge");

	trees[nr_trees] = parse_tree_indirect(head);
	if (!trees[nr_trees++])
		return -1;
	trees[nr_trees] = parse_tree_indirect(remote);
	if (!trees[nr_trees++])
		return -1;
	for (i = 0; i < nr_trees; i++) {
		parse_tree(trees[i]);
		init_tree_desc(t+i, trees[i]->buffer, trees[i]->size);
	}
	if (unpack_trees(nr_trees, t, &opts))
		return -1;
	if (write_cache(fd, active_cache, active_nr) ||
		commit_locked_index(lock_file))
		die(_("unable to write new index file"));
	return 0;
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
	int fd = open(git_path("MERGE_MSG"), O_WRONLY | O_CREAT, 0666);
	if (fd < 0)
		die_errno(_("Could not open '%s' for writing"),
			  git_path("MERGE_MSG"));
	if (write_in_full(fd, msg->buf, msg->len) != msg->len)
		die_errno(_("Could not write to '%s'"), git_path("MERGE_MSG"));
	close(fd);
}

static void read_merge_msg(struct strbuf *msg)
{
	strbuf_reset(msg);
	if (strbuf_read_file(msg, git_path("MERGE_MSG"), 0) < 0)
		die_errno(_("Could not read from '%s'"), git_path("MERGE_MSG"));
}

static void write_merge_state(void);
static void abort_commit(const char *err_msg)
{
	if (err_msg)
		error("%s", err_msg);
	fprintf(stderr,
		_("Not committing merge; use 'git commit' to complete the merge.\n"));
	write_merge_state();
	exit(1);
}

static void prepare_to_commit(void)
{
	struct strbuf msg = STRBUF_INIT;
	strbuf_addbuf(&msg, &merge_msg);
	strbuf_addch(&msg, '\n');
	write_merge_msg(&msg);
	run_hook(get_index_file(), "prepare-commit-msg",
		 git_path("MERGE_MSG"), "merge", NULL, NULL);
	if (option_edit) {
		if (launch_editor(git_path("MERGE_MSG"), NULL, NULL))
			abort_commit(NULL);
	}
	read_merge_msg(&msg);
	stripspace(&msg, option_edit);
	if (!msg.len)
		abort_commit(_("Empty commit message."));
	strbuf_release(&merge_msg);
	strbuf_addbuf(&merge_msg, &msg);
	strbuf_release(&msg);
}

static int merge_trivial(void)
{
	unsigned char result_tree[20], result_commit[20];
	struct commit_list *parent = xmalloc(sizeof(*parent));

	write_tree_trivial(result_tree);
	printf(_("Wonderful.\n"));
	parent->item = lookup_commit(head);
	parent->next = xmalloc(sizeof(*parent->next));
	parent->next->item = remoteheads->item;
	parent->next->next = NULL;
	prepare_to_commit();
	commit_tree(merge_msg.buf, result_tree, parent, result_commit, NULL);
	finish(result_commit, "In-index merge");
	drop_save();
	return 0;
}

static int finish_automerge(struct commit_list *common,
			    unsigned char *result_tree,
			    const char *wt_strategy)
{
	struct commit_list *parents = NULL, *j;
	struct strbuf buf = STRBUF_INIT;
	unsigned char result_commit[20];

	free_commit_list(common);
	if (allow_fast_forward) {
		parents = remoteheads;
		commit_list_insert(lookup_commit(head), &parents);
		parents = reduce_heads(parents);
	} else {
		struct commit_list **pptr = &parents;

		pptr = &commit_list_insert(lookup_commit(head),
				pptr)->next;
		for (j = remoteheads; j; j = j->next)
			pptr = &commit_list_insert(j->item, pptr)->next;
	}
	strbuf_addch(&merge_msg, '\n');
	prepare_to_commit();
	free_commit_list(remoteheads);
	commit_tree(merge_msg.buf, result_tree, parents, result_commit, NULL);
	strbuf_addf(&buf, "Merge made by the '%s' strategy.", wt_strategy);
	finish(result_commit, buf.buf);
	strbuf_release(&buf);
	drop_save();
	return 0;
}

static int suggest_conflicts(int renormalizing)
{
	FILE *fp;
	int pos;

	fp = fopen(git_path("MERGE_MSG"), "a");
	if (!fp)
		die_errno(_("Could not open '%s' for writing"),
			  git_path("MERGE_MSG"));
	fprintf(fp, "\nConflicts:\n");
	for (pos = 0; pos < active_nr; pos++) {
		struct cache_entry *ce = active_cache[pos];

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

static struct commit *is_old_style_invocation(int argc, const char **argv)
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
 * Pretend as if the user told us to merge with the tracking
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
			die(_("No remote tracking branch for %s from %s"),
			    branch->merge[i]->src, branch->remote_name);
		args[i] = branch->merge[i]->dst;
	}
	args[i] = NULL;
	*argv = args;
	return i;
}

static void write_merge_state(void)
{
	int fd;
	struct commit_list *j;
	struct strbuf buf = STRBUF_INIT;

	for (j = remoteheads; j; j = j->next)
		strbuf_addf(&buf, "%s\n",
			sha1_to_hex(j->item->object.sha1));
	fd = open(git_path("MERGE_HEAD"), O_WRONLY | O_CREAT, 0666);
	if (fd < 0)
		die_errno(_("Could not open '%s' for writing"),
			  git_path("MERGE_HEAD"));
	if (write_in_full(fd, buf.buf, buf.len) != buf.len)
		die_errno(_("Could not write to '%s'"), git_path("MERGE_HEAD"));
	close(fd);
	strbuf_addch(&merge_msg, '\n');
	write_merge_msg(&merge_msg);
	fd = open(git_path("MERGE_MODE"), O_WRONLY | O_CREAT | O_TRUNC, 0666);
	if (fd < 0)
		die_errno(_("Could not open '%s' for writing"),
			  git_path("MERGE_MODE"));
	strbuf_reset(&buf);
	if (!allow_fast_forward)
		strbuf_addf(&buf, "no-ff");
	if (write_in_full(fd, buf.buf, buf.len) != buf.len)
		die_errno(_("Could not write to '%s'"), git_path("MERGE_MODE"));
	close(fd);
}

int cmd_merge(int argc, const char **argv, const char *prefix)
{
	unsigned char result_tree[20];
	struct strbuf buf = STRBUF_INIT;
	const char *head_arg;
	int flag, head_invalid = 0, i;
	int best_cnt = -1, merge_was_ok = 0, automerge_was_ok = 0;
	struct commit_list *common = NULL;
	const char *best_strategy = NULL, *wt_strategy = NULL;
	struct commit_list **remotes = &remoteheads;

	if (argc == 2 && !strcmp(argv[1], "-h"))
		usage_with_options(builtin_merge_usage, builtin_merge_options);

	/*
	 * Check if we are _not_ on a detached HEAD, i.e. if there is a
	 * current branch.
	 */
	branch = resolve_ref("HEAD", head, 0, &flag);
	if (branch && !prefixcmp(branch, "refs/heads/"))
		branch += 11;
	if (is_null_sha1(head))
		head_invalid = 1;

	git_config(git_merge_config, NULL);

	if (branch_mergeoptions)
		parse_branch_merge_options(branch_mergeoptions);
	argc = parse_options(argc, argv, prefix, builtin_merge_options,
			builtin_merge_usage, 0);

	if (verbosity < 0 && show_progress == -1)
		show_progress = 0;

	if (abort_current_merge) {
		int nargc = 2;
		const char *nargv[] = {"reset", "--merge", NULL};

		if (!file_exists(git_path("MERGE_HEAD")))
			die(_("There is no merge to abort (MERGE_HEAD missing)."));

		/* Invoke 'git reset --merge' */
		return cmd_reset(nargc, nargv, prefix);
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
		if (!allow_fast_forward)
			die(_("You cannot combine --squash with --no-ff."));
		option_commit = 0;
	}

	if (!allow_fast_forward && fast_forward_only)
		die(_("You cannot combine --no-ff with --ff-only."));

	if (!abort_current_merge) {
		if (!argc && default_to_upstream)
			argc = setup_with_upstream(&argv);
		else if (argc == 1 && !strcmp(argv[0], "-"))
			argv[0] = "@{-1}";
	}
	if (!argc)
		usage_with_options(builtin_merge_usage,
			builtin_merge_options);

	/*
	 * This could be traditional "merge <msg> HEAD <commit>..."  and
	 * the way we can tell it is to see if the second token is HEAD,
	 * but some people might have misused the interface and used a
	 * committish that is the same as HEAD there instead.
	 * Traditional format never would have "-m" so it is an
	 * additional safety measure to check for it.
	 */

	if (!have_message && is_old_style_invocation(argc, argv)) {
		strbuf_addstr(&merge_msg, argv[0]);
		head_arg = argv[1];
		argv += 2;
		argc -= 2;
	} else if (head_invalid) {
		struct object *remote_head;
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
		if (!allow_fast_forward)
			die(_("Non-fast-forward commit does not make sense into "
			    "an empty head"));
		remote_head = peel_to_type(argv[0], 0, NULL, OBJ_COMMIT);
		if (!remote_head)
			die(_("%s - not something we can merge"), argv[0]);
		read_empty(remote_head->sha1, 0);
		update_ref("initial pull", "HEAD", remote_head->sha1, NULL, 0,
				DIE_ON_ERR);
		return 0;
	} else {
		struct strbuf merge_names = STRBUF_INIT;

		/* We are invoked directly as the first-class UI. */
		head_arg = "HEAD";

		/*
		 * All the rest are the commits being merged;
		 * prepare the standard merge summary message to
		 * be appended to the given message.  If remote
		 * is invalid we will die later in the common
		 * codepath so we discard the error in this
		 * loop.
		 */
		for (i = 0; i < argc; i++)
			merge_name(argv[i], &merge_names);

		if (!have_message || shortlog_len) {
			fmt_merge_msg(&merge_names, &merge_msg, !have_message,
				      shortlog_len);
			if (merge_msg.len)
				strbuf_setlen(&merge_msg, merge_msg.len - 1);
		}
	}

	if (head_invalid || !argc)
		usage_with_options(builtin_merge_usage,
			builtin_merge_options);

	strbuf_addstr(&buf, "merge");
	for (i = 0; i < argc; i++)
		strbuf_addf(&buf, " %s", argv[i]);
	setenv("GIT_REFLOG_ACTION", buf.buf, 0);
	strbuf_reset(&buf);

	for (i = 0; i < argc; i++) {
		struct object *o;
		struct commit *commit;

		o = peel_to_type(argv[i], 0, NULL, OBJ_COMMIT);
		if (!o)
			die(_("%s - not something we can merge"), argv[i]);
		commit = lookup_commit(o->sha1);
		commit->util = (void *)argv[i];
		remotes = &commit_list_insert(commit, remotes)->next;

		strbuf_addf(&buf, "GITHEAD_%s", sha1_to_hex(o->sha1));
		setenv(buf.buf, argv[i], 1);
		strbuf_reset(&buf);
	}

	if (!use_strategies) {
		if (!remoteheads->next)
			add_strategies(pull_twohead, DEFAULT_TWOHEAD);
		else
			add_strategies(pull_octopus, DEFAULT_OCTOPUS);
	}

	for (i = 0; i < use_strategies_nr; i++) {
		if (use_strategies[i]->attr & NO_FAST_FORWARD)
			allow_fast_forward = 0;
		if (use_strategies[i]->attr & NO_TRIVIAL)
			allow_trivial = 0;
	}

	if (!remoteheads->next)
		common = get_merge_bases(lookup_commit(head),
				remoteheads->item, 1);
	else {
		struct commit_list *list = remoteheads;
		commit_list_insert(lookup_commit(head), &list);
		common = get_octopus_merge_bases(list);
		free(list);
	}

	update_ref("updating ORIG_HEAD", "ORIG_HEAD", head, NULL, 0,
		DIE_ON_ERR);

	if (!common)
		; /* No common ancestors found. We need a real merge. */
	else if (!remoteheads->next && !common->next &&
			common->item == remoteheads->item) {
		/*
		 * If head can reach all the merge then we are up to date.
		 * but first the most common case of merging one remote.
		 */
		finish_up_to_date("Already up-to-date.");
		return 0;
	} else if (allow_fast_forward && !remoteheads->next &&
			!common->next &&
			!hashcmp(common->item->object.sha1, head)) {
		/* Again the most common case of merging one remote. */
		struct strbuf msg = STRBUF_INIT;
		struct object *o;
		char hex[41];

		strcpy(hex, find_unique_abbrev(head, DEFAULT_ABBREV));

		if (verbosity >= 0)
			printf(_("Updating %s..%s\n"),
				hex,
				find_unique_abbrev(remoteheads->item->object.sha1,
				DEFAULT_ABBREV));
		strbuf_addstr(&msg, "Fast-forward");
		if (have_message)
			strbuf_addstr(&msg,
				" (no commit created; -m option ignored)");
		o = peel_to_type(sha1_to_hex(remoteheads->item->object.sha1),
			0, NULL, OBJ_COMMIT);
		if (!o)
			return 1;

		if (checkout_fast_forward(head, remoteheads->item->object.sha1))
			return 1;

		finish(o->sha1, msg.buf);
		drop_save();
		return 0;
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
		if (allow_trivial && !fast_forward_only) {
			/* See if it is really trivial. */
			git_committer_info(IDENT_ERROR_ON_NO_NAME);
			printf(_("Trying really trivial in-index merge...\n"));
			if (!read_tree_trivial(common->item->object.sha1,
					head, remoteheads->item->object.sha1))
				return merge_trivial();
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
			common_one = get_merge_bases(lookup_commit(head),
				j->item, 1);
			if (hashcmp(common_one->item->object.sha1,
				j->item->object.sha1)) {
				up_to_date = 0;
				break;
			}
		}
		if (up_to_date) {
			finish_up_to_date("Already up-to-date. Yeeah!");
			return 0;
		}
	}

	if (fast_forward_only)
		die(_("Not possible to fast-forward, aborting."));

	/* We are going to make a new commit. */
	git_committer_info(IDENT_ERROR_ON_NO_NAME);

	/*
	 * At this point, we need a real merge.  No matter what strategy
	 * we use, it would operate on the index, possibly affecting the
	 * working tree, and when resolved cleanly, have the desired
	 * tree in the index -- this means that the index must be in
	 * sync with the head commit.  The strategies are responsible
	 * to ensure this.
	 */
	if (use_strategies_nr != 1) {
		/*
		 * Stash away the local changes so that we can try more
		 * than one.
		 */
		save_state();
	} else {
		memcpy(stash, null_sha1, 20);
	}

	for (i = 0; i < use_strategies_nr; i++) {
		int ret;
		if (i) {
			printf(_("Rewinding the tree to pristine...\n"));
			restore_state();
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
			common, head_arg);
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
	if (automerge_was_ok)
		return finish_automerge(common, result_tree, wt_strategy);

	/*
	 * Pick the result from the best strategy and have the user fix
	 * it up.
	 */
	if (!best_strategy) {
		restore_state();
		if (use_strategies_nr > 1)
			fprintf(stderr,
				_("No merge strategy handled the merge.\n"));
		else
			fprintf(stderr, _("Merge with strategy %s failed.\n"),
				use_strategies[0]->name);
		return 2;
	} else if (best_strategy == wt_strategy)
		; /* We already have its result in the working tree. */
	else {
		printf(_("Rewinding the tree to pristine...\n"));
		restore_state();
		printf(_("Using the %s to prepare resolving by hand.\n"),
			best_strategy);
		try_merge_strategy(best_strategy, common, head_arg);
	}

	if (squash)
		finish(NULL, NULL);
	else
		write_merge_state();

	if (merge_was_ok) {
		fprintf(stderr, _("Automatic merge went well; "
			"stopped before committing as requested\n"));
		return 0;
	} else
		return suggest_conflicts(option_renormalize);
}
