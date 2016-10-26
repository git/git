#include "builtin.h"
#include "exec_cmd.h"
#include "help.h"
#include "run-command.h"

const char git_usage_string[] =
	"git [--version] [--help] [-C <path>] [-c name=value]\n"
	"           [--exec-path[=<path>]] [--html-path] [--man-path] [--info-path]\n"
	"           [-p | --paginate | --no-pager] [--no-replace-objects] [--bare]\n"
	"           [--git-dir=<path>] [--work-tree=<path>] [--namespace=<name>]\n"
	"           <command> [<args>]";

const char git_more_info_string[] =
	N_("'git help -a' and 'git help -g' list available subcommands and some\n"
	   "concept guides. See 'git help <command>' or 'git help <concept>'\n"
	   "to read about a specific subcommand or concept.");

static int use_pager = -1;
static char *orig_cwd;
static const char *env_names[] = {
	GIT_DIR_ENVIRONMENT,
	GIT_WORK_TREE_ENVIRONMENT,
	GIT_IMPLICIT_WORK_TREE_ENVIRONMENT,
	GIT_PREFIX_ENVIRONMENT
};
static char *orig_env[4];
static int save_restore_env_balance;

static void save_env_before_alias(void)
{
	int i;

	assert(save_restore_env_balance == 0);
	save_restore_env_balance = 1;
	orig_cwd = xgetcwd();
	for (i = 0; i < ARRAY_SIZE(env_names); i++) {
		orig_env[i] = getenv(env_names[i]);
		orig_env[i] = xstrdup_or_null(orig_env[i]);
	}
}

static void restore_env(int external_alias)
{
	int i;

	assert(save_restore_env_balance == 1);
	save_restore_env_balance = 0;
	if (!external_alias && orig_cwd && chdir(orig_cwd))
		die_errno("could not move to %s", orig_cwd);
	free(orig_cwd);
	for (i = 0; i < ARRAY_SIZE(env_names); i++) {
		if (external_alias &&
		    !strcmp(env_names[i], GIT_PREFIX_ENVIRONMENT))
			continue;
		if (orig_env[i]) {
			setenv(env_names[i], orig_env[i], 1);
			free(orig_env[i]);
		} else {
			unsetenv(env_names[i]);
		}
	}
}

static void commit_pager_choice(void) {
	switch (use_pager) {
	case 0:
		setenv("GIT_PAGER", "cat", 1);
		break;
	case 1:
		setup_pager();
		break;
	default:
		break;
	}
}

static int handle_options(const char ***argv, int *argc, int *envchanged)
{
	const char **orig_argv = *argv;

	while (*argc > 0) {
		const char *cmd = (*argv)[0];
		if (cmd[0] != '-')
			break;

		/*
		 * For legacy reasons, the "version" and "help"
		 * commands can be written with "--" prepended
		 * to make them look like flags.
		 */
		if (!strcmp(cmd, "--help") || !strcmp(cmd, "--version"))
			break;

		/*
		 * Check remaining flags.
		 */
		if (skip_prefix(cmd, "--exec-path", &cmd)) {
			if (*cmd == '=')
				git_set_argv_exec_path(cmd + 1);
			else {
				puts(git_exec_path());
				exit(0);
			}
		} else if (!strcmp(cmd, "--html-path")) {
			puts(system_path(GIT_HTML_PATH));
			exit(0);
		} else if (!strcmp(cmd, "--man-path")) {
			puts(system_path(GIT_MAN_PATH));
			exit(0);
		} else if (!strcmp(cmd, "--info-path")) {
			puts(system_path(GIT_INFO_PATH));
			exit(0);
		} else if (!strcmp(cmd, "-p") || !strcmp(cmd, "--paginate")) {
			use_pager = 1;
		} else if (!strcmp(cmd, "--no-pager")) {
			use_pager = 0;
			if (envchanged)
				*envchanged = 1;
		} else if (!strcmp(cmd, "--no-replace-objects")) {
			check_replace_refs = 0;
			setenv(NO_REPLACE_OBJECTS_ENVIRONMENT, "1", 1);
			if (envchanged)
				*envchanged = 1;
		} else if (!strcmp(cmd, "--git-dir")) {
			if (*argc < 2) {
				fprintf(stderr, "No directory given for --git-dir.\n" );
				usage(git_usage_string);
			}
			setenv(GIT_DIR_ENVIRONMENT, (*argv)[1], 1);
			if (envchanged)
				*envchanged = 1;
			(*argv)++;
			(*argc)--;
		} else if (skip_prefix(cmd, "--git-dir=", &cmd)) {
			setenv(GIT_DIR_ENVIRONMENT, cmd, 1);
			if (envchanged)
				*envchanged = 1;
		} else if (!strcmp(cmd, "--namespace")) {
			if (*argc < 2) {
				fprintf(stderr, "No namespace given for --namespace.\n" );
				usage(git_usage_string);
			}
			setenv(GIT_NAMESPACE_ENVIRONMENT, (*argv)[1], 1);
			if (envchanged)
				*envchanged = 1;
			(*argv)++;
			(*argc)--;
		} else if (skip_prefix(cmd, "--namespace=", &cmd)) {
			setenv(GIT_NAMESPACE_ENVIRONMENT, cmd, 1);
			if (envchanged)
				*envchanged = 1;
		} else if (!strcmp(cmd, "--work-tree")) {
			if (*argc < 2) {
				fprintf(stderr, "No directory given for --work-tree.\n" );
				usage(git_usage_string);
			}
			setenv(GIT_WORK_TREE_ENVIRONMENT, (*argv)[1], 1);
			if (envchanged)
				*envchanged = 1;
			(*argv)++;
			(*argc)--;
		} else if (skip_prefix(cmd, "--work-tree=", &cmd)) {
			setenv(GIT_WORK_TREE_ENVIRONMENT, cmd, 1);
			if (envchanged)
				*envchanged = 1;
		} else if (!strcmp(cmd, "--super-prefix")) {
			if (*argc < 2) {
				fprintf(stderr, "No prefix given for --super-prefix.\n" );
				usage(git_usage_string);
			}
			setenv(GIT_SUPER_PREFIX_ENVIRONMENT, (*argv)[1], 1);
			if (envchanged)
				*envchanged = 1;
			(*argv)++;
			(*argc)--;
		} else if (skip_prefix(cmd, "--super-prefix=", &cmd)) {
			setenv(GIT_SUPER_PREFIX_ENVIRONMENT, cmd, 1);
			if (envchanged)
				*envchanged = 1;
		} else if (!strcmp(cmd, "--bare")) {
			char *cwd = xgetcwd();
			is_bare_repository_cfg = 1;
			setenv(GIT_DIR_ENVIRONMENT, cwd, 0);
			free(cwd);
			setenv(GIT_IMPLICIT_WORK_TREE_ENVIRONMENT, "0", 1);
			if (envchanged)
				*envchanged = 1;
		} else if (!strcmp(cmd, "-c")) {
			if (*argc < 2) {
				fprintf(stderr, "-c expects a configuration string\n" );
				usage(git_usage_string);
			}
			git_config_push_parameter((*argv)[1]);
			(*argv)++;
			(*argc)--;
		} else if (!strcmp(cmd, "--literal-pathspecs")) {
			setenv(GIT_LITERAL_PATHSPECS_ENVIRONMENT, "1", 1);
			if (envchanged)
				*envchanged = 1;
		} else if (!strcmp(cmd, "--no-literal-pathspecs")) {
			setenv(GIT_LITERAL_PATHSPECS_ENVIRONMENT, "0", 1);
			if (envchanged)
				*envchanged = 1;
		} else if (!strcmp(cmd, "--glob-pathspecs")) {
			setenv(GIT_GLOB_PATHSPECS_ENVIRONMENT, "1", 1);
			if (envchanged)
				*envchanged = 1;
		} else if (!strcmp(cmd, "--noglob-pathspecs")) {
			setenv(GIT_NOGLOB_PATHSPECS_ENVIRONMENT, "1", 1);
			if (envchanged)
				*envchanged = 1;
		} else if (!strcmp(cmd, "--icase-pathspecs")) {
			setenv(GIT_ICASE_PATHSPECS_ENVIRONMENT, "1", 1);
			if (envchanged)
				*envchanged = 1;
		} else if (!strcmp(cmd, "--shallow-file")) {
			(*argv)++;
			(*argc)--;
			set_alternate_shallow_file((*argv)[0], 1);
			if (envchanged)
				*envchanged = 1;
		} else if (!strcmp(cmd, "-C")) {
			if (*argc < 2) {
				fprintf(stderr, "No directory given for -C.\n" );
				usage(git_usage_string);
			}
			if ((*argv)[1][0]) {
				if (chdir((*argv)[1]))
					die_errno("Cannot change to '%s'", (*argv)[1]);
				if (envchanged)
					*envchanged = 1;
			}
			(*argv)++;
			(*argc)--;
		} else {
			fprintf(stderr, "Unknown option: %s\n", cmd);
			usage(git_usage_string);
		}

		(*argv)++;
		(*argc)--;
	}
	return (*argv) - orig_argv;
}

static int handle_alias(int *argcp, const char ***argv)
{
	int envchanged = 0, ret = 0, saved_errno = errno;
	int count, option_count;
	const char **new_argv;
	const char *alias_command;
	char *alias_string;
	int unused_nongit;

	save_env_before_alias();
	setup_git_directory_gently(&unused_nongit);

	alias_command = (*argv)[0];
	alias_string = alias_lookup(alias_command);
	if (alias_string) {
		if (alias_string[0] == '!') {
			struct child_process child = CHILD_PROCESS_INIT;

			commit_pager_choice();
			restore_env(1);

			child.use_shell = 1;
			argv_array_push(&child.args, alias_string + 1);
			argv_array_pushv(&child.args, (*argv) + 1);

			ret = run_command(&child);
			if (ret >= 0)   /* normal exit */
				exit(ret);

			die_errno("While expanding alias '%s': '%s'",
			    alias_command, alias_string + 1);
		}
		count = split_cmdline(alias_string, &new_argv);
		if (count < 0)
			die("Bad alias.%s string: %s", alias_command,
			    split_cmdline_strerror(count));
		option_count = handle_options(&new_argv, &count, &envchanged);
		if (envchanged)
			die("alias '%s' changes environment variables\n"
				 "You can use '!git' in the alias to do this.",
				 alias_command);
		memmove(new_argv - option_count, new_argv,
				count * sizeof(char *));
		new_argv -= option_count;

		if (count < 1)
			die("empty alias for %s", alias_command);

		if (!strcmp(alias_command, new_argv[0]))
			die("recursive alias: %s", alias_command);

		trace_argv_printf(new_argv,
				  "trace: alias expansion: %s =>",
				  alias_command);

		REALLOC_ARRAY(new_argv, count + *argcp);
		/* insert after command name */
		memcpy(new_argv + count, *argv + 1, sizeof(char *) * *argcp);

		*argv = new_argv;
		*argcp += count - 1;

		ret = 1;
	}

	restore_env(0);

	errno = saved_errno;

	return ret;
}

#define RUN_SETUP		(1<<0)
#define RUN_SETUP_GENTLY	(1<<1)
#define USE_PAGER		(1<<2)
/*
 * require working tree to be present -- anything uses this needs
 * RUN_SETUP for reading from the configuration file.
 */
#define NEED_WORK_TREE		(1<<3)
#define SUPPORT_SUPER_PREFIX	(1<<4)

struct cmd_struct {
	const char *cmd;
	int (*fn)(int, const char **, const char *);
	int option;
};

static int run_builtin(struct cmd_struct *p, int argc, const char **argv)
{
	int status, help;
	struct stat st;
	const char *prefix;

	prefix = NULL;
	help = argc == 2 && !strcmp(argv[1], "-h");
	if (!help) {
		if (p->option & RUN_SETUP)
			prefix = setup_git_directory();
		else if (p->option & RUN_SETUP_GENTLY) {
			int nongit_ok;
			prefix = setup_git_directory_gently(&nongit_ok);
		}

		if (use_pager == -1 && p->option & (RUN_SETUP | RUN_SETUP_GENTLY))
			use_pager = check_pager_config(p->cmd);
		if (use_pager == -1 && p->option & USE_PAGER)
			use_pager = 1;

		if ((p->option & (RUN_SETUP | RUN_SETUP_GENTLY)) &&
		    startup_info->have_repository) /* get_git_dir() may set up repo, avoid that */
			trace_repo_setup(prefix);
	}
	commit_pager_choice();

	if (!help && get_super_prefix()) {
		if (!(p->option & SUPPORT_SUPER_PREFIX))
			die("%s doesn't support --super-prefix", p->cmd);
		if (prefix)
			die("can't use --super-prefix from a subdirectory");
	}

	if (!help && p->option & NEED_WORK_TREE)
		setup_work_tree();

	trace_argv_printf(argv, "trace: built-in: git");

	status = p->fn(argc, argv, prefix);
	if (status)
		return status;

	/* Somebody closed stdout? */
	if (fstat(fileno(stdout), &st))
		return 0;
	/* Ignore write errors for pipes and sockets.. */
	if (S_ISFIFO(st.st_mode) || S_ISSOCK(st.st_mode))
		return 0;

	/* Check for ENOSPC and EIO errors.. */
	if (fflush(stdout))
		die_errno("write failure on standard output");
	if (ferror(stdout))
		die("unknown write failure on standard output");
	if (fclose(stdout))
		die_errno("close failed on standard output");
	return 0;
}

static struct cmd_struct commands[] = {
	{ "add", cmd_add, RUN_SETUP | NEED_WORK_TREE },
	{ "am", cmd_am, RUN_SETUP | NEED_WORK_TREE },
	{ "annotate", cmd_annotate, RUN_SETUP },
	{ "apply", cmd_apply, RUN_SETUP_GENTLY },
	{ "archive", cmd_archive },
	{ "bisect--helper", cmd_bisect__helper, RUN_SETUP },
	{ "blame", cmd_blame, RUN_SETUP },
	{ "branch", cmd_branch, RUN_SETUP },
	{ "bundle", cmd_bundle, RUN_SETUP_GENTLY },
	{ "cat-file", cmd_cat_file, RUN_SETUP },
	{ "check-attr", cmd_check_attr, RUN_SETUP },
	{ "check-ignore", cmd_check_ignore, RUN_SETUP | NEED_WORK_TREE },
	{ "check-mailmap", cmd_check_mailmap, RUN_SETUP },
	{ "check-ref-format", cmd_check_ref_format },
	{ "checkout", cmd_checkout, RUN_SETUP | NEED_WORK_TREE },
	{ "checkout-index", cmd_checkout_index,
		RUN_SETUP | NEED_WORK_TREE},
	{ "cherry", cmd_cherry, RUN_SETUP },
	{ "cherry-pick", cmd_cherry_pick, RUN_SETUP | NEED_WORK_TREE },
	{ "clean", cmd_clean, RUN_SETUP | NEED_WORK_TREE },
	{ "clone", cmd_clone },
	{ "column", cmd_column, RUN_SETUP_GENTLY },
	{ "commit", cmd_commit, RUN_SETUP | NEED_WORK_TREE },
	{ "commit-tree", cmd_commit_tree, RUN_SETUP },
	{ "config", cmd_config, RUN_SETUP_GENTLY },
	{ "count-objects", cmd_count_objects, RUN_SETUP },
	{ "credential", cmd_credential, RUN_SETUP_GENTLY },
	{ "describe", cmd_describe, RUN_SETUP },
	{ "diff", cmd_diff },
	{ "diff-files", cmd_diff_files, RUN_SETUP | NEED_WORK_TREE },
	{ "diff-index", cmd_diff_index, RUN_SETUP },
	{ "diff-tree", cmd_diff_tree, RUN_SETUP },
	{ "fast-export", cmd_fast_export, RUN_SETUP },
	{ "fetch", cmd_fetch, RUN_SETUP },
	{ "fetch-pack", cmd_fetch_pack, RUN_SETUP },
	{ "fmt-merge-msg", cmd_fmt_merge_msg, RUN_SETUP },
	{ "for-each-ref", cmd_for_each_ref, RUN_SETUP },
	{ "format-patch", cmd_format_patch, RUN_SETUP },
	{ "fsck", cmd_fsck, RUN_SETUP },
	{ "fsck-objects", cmd_fsck, RUN_SETUP },
	{ "gc", cmd_gc, RUN_SETUP },
	{ "get-tar-commit-id", cmd_get_tar_commit_id },
	{ "grep", cmd_grep, RUN_SETUP_GENTLY },
	{ "hash-object", cmd_hash_object },
	{ "help", cmd_help },
	{ "index-pack", cmd_index_pack, RUN_SETUP_GENTLY },
	{ "init", cmd_init_db },
	{ "init-db", cmd_init_db },
	{ "interpret-trailers", cmd_interpret_trailers, RUN_SETUP_GENTLY },
	{ "log", cmd_log, RUN_SETUP },
	{ "ls-files", cmd_ls_files, RUN_SETUP | SUPPORT_SUPER_PREFIX },
	{ "ls-remote", cmd_ls_remote, RUN_SETUP_GENTLY },
	{ "ls-tree", cmd_ls_tree, RUN_SETUP },
	{ "mailinfo", cmd_mailinfo },
	{ "mailsplit", cmd_mailsplit },
	{ "merge", cmd_merge, RUN_SETUP | NEED_WORK_TREE },
	{ "merge-base", cmd_merge_base, RUN_SETUP },
	{ "merge-file", cmd_merge_file, RUN_SETUP_GENTLY },
	{ "merge-index", cmd_merge_index, RUN_SETUP },
	{ "merge-ours", cmd_merge_ours, RUN_SETUP },
	{ "merge-recursive", cmd_merge_recursive, RUN_SETUP | NEED_WORK_TREE },
	{ "merge-recursive-ours", cmd_merge_recursive, RUN_SETUP | NEED_WORK_TREE },
	{ "merge-recursive-theirs", cmd_merge_recursive, RUN_SETUP | NEED_WORK_TREE },
	{ "merge-subtree", cmd_merge_recursive, RUN_SETUP | NEED_WORK_TREE },
	{ "merge-tree", cmd_merge_tree, RUN_SETUP },
	{ "mktag", cmd_mktag, RUN_SETUP },
	{ "mktree", cmd_mktree, RUN_SETUP },
	{ "mv", cmd_mv, RUN_SETUP | NEED_WORK_TREE },
	{ "name-rev", cmd_name_rev, RUN_SETUP },
	{ "notes", cmd_notes, RUN_SETUP },
	{ "pack-objects", cmd_pack_objects, RUN_SETUP },
	{ "pack-redundant", cmd_pack_redundant, RUN_SETUP },
	{ "pack-refs", cmd_pack_refs, RUN_SETUP },
	{ "patch-id", cmd_patch_id, RUN_SETUP_GENTLY },
	{ "pickaxe", cmd_blame, RUN_SETUP },
	{ "prune", cmd_prune, RUN_SETUP },
	{ "prune-packed", cmd_prune_packed, RUN_SETUP },
	{ "pull", cmd_pull, RUN_SETUP | NEED_WORK_TREE },
	{ "push", cmd_push, RUN_SETUP },
	{ "read-tree", cmd_read_tree, RUN_SETUP },
	{ "receive-pack", cmd_receive_pack },
	{ "reflog", cmd_reflog, RUN_SETUP },
	{ "remote", cmd_remote, RUN_SETUP },
	{ "remote-ext", cmd_remote_ext },
	{ "remote-fd", cmd_remote_fd },
	{ "repack", cmd_repack, RUN_SETUP },
	{ "replace", cmd_replace, RUN_SETUP },
	{ "rerere", cmd_rerere, RUN_SETUP },
	{ "reset", cmd_reset, RUN_SETUP },
	{ "rev-list", cmd_rev_list, RUN_SETUP },
	{ "rev-parse", cmd_rev_parse },
	{ "revert", cmd_revert, RUN_SETUP | NEED_WORK_TREE },
	{ "rm", cmd_rm, RUN_SETUP },
	{ "send-pack", cmd_send_pack, RUN_SETUP },
	{ "shortlog", cmd_shortlog, RUN_SETUP_GENTLY | USE_PAGER },
	{ "show", cmd_show, RUN_SETUP },
	{ "show-branch", cmd_show_branch, RUN_SETUP },
	{ "show-ref", cmd_show_ref, RUN_SETUP },
	{ "stage", cmd_add, RUN_SETUP | NEED_WORK_TREE },
	{ "status", cmd_status, RUN_SETUP | NEED_WORK_TREE },
	{ "stripspace", cmd_stripspace },
	{ "submodule--helper", cmd_submodule__helper, RUN_SETUP },
	{ "symbolic-ref", cmd_symbolic_ref, RUN_SETUP },
	{ "tag", cmd_tag, RUN_SETUP },
	{ "unpack-file", cmd_unpack_file, RUN_SETUP },
	{ "unpack-objects", cmd_unpack_objects, RUN_SETUP },
	{ "update-index", cmd_update_index, RUN_SETUP },
	{ "update-ref", cmd_update_ref, RUN_SETUP },
	{ "update-server-info", cmd_update_server_info, RUN_SETUP },
	{ "upload-archive", cmd_upload_archive },
	{ "upload-archive--writer", cmd_upload_archive_writer },
	{ "var", cmd_var, RUN_SETUP_GENTLY },
	{ "verify-commit", cmd_verify_commit, RUN_SETUP },
	{ "verify-pack", cmd_verify_pack },
	{ "verify-tag", cmd_verify_tag, RUN_SETUP },
	{ "version", cmd_version },
	{ "whatchanged", cmd_whatchanged, RUN_SETUP },
	{ "worktree", cmd_worktree, RUN_SETUP },
	{ "write-tree", cmd_write_tree, RUN_SETUP },
};

static struct cmd_struct *get_builtin(const char *s)
{
	int i;
	for (i = 0; i < ARRAY_SIZE(commands); i++) {
		struct cmd_struct *p = commands + i;
		if (!strcmp(s, p->cmd))
			return p;
	}
	return NULL;
}

int is_builtin(const char *s)
{
	return !!get_builtin(s);
}

#ifdef STRIP_EXTENSION
static void strip_extension(const char **argv)
{
	size_t len;

	if (strip_suffix(argv[0], STRIP_EXTENSION, &len))
		argv[0] = xmemdupz(argv[0], len);
}
#else
#define strip_extension(cmd)
#endif

static void handle_builtin(int argc, const char **argv)
{
	struct argv_array args = ARGV_ARRAY_INIT;
	const char *cmd;
	struct cmd_struct *builtin;

	strip_extension(argv);
	cmd = argv[0];

	/* Turn "git cmd --help" into "git help --exclude-guides cmd" */
	if (argc > 1 && !strcmp(argv[1], "--help")) {
		int i;

		argv[1] = argv[0];
		argv[0] = cmd = "help";

		for (i = 0; i < argc; i++) {
			argv_array_push(&args, argv[i]);
			if (!i)
				argv_array_push(&args, "--exclude-guides");
		}

		argc++;
		argv = args.argv;
	}

	builtin = get_builtin(cmd);
	if (builtin)
		exit(run_builtin(builtin, argc, argv));
	argv_array_clear(&args);
}

static void execv_dashed_external(const char **argv)
{
	struct strbuf cmd = STRBUF_INIT;
	const char *tmp;
	int status;

	if (get_super_prefix())
		die("%s doesn't support --super-prefix", argv[0]);

	if (use_pager == -1)
		use_pager = check_pager_config(argv[0]);
	commit_pager_choice();

	strbuf_addf(&cmd, "git-%s", argv[0]);

	/*
	 * argv[0] must be the git command, but the argv array
	 * belongs to the caller, and may be reused in
	 * subsequent loop iterations. Save argv[0] and
	 * restore it on error.
	 */
	tmp = argv[0];
	argv[0] = cmd.buf;

	trace_argv_printf(argv, "trace: exec:");

	/*
	 * if we fail because the command is not found, it is
	 * OK to return. Otherwise, we just pass along the status code.
	 */
	status = run_command_v_opt(argv, RUN_SILENT_EXEC_FAILURE | RUN_CLEAN_ON_EXIT);
	if (status >= 0 || errno != ENOENT)
		exit(status);

	argv[0] = tmp;

	strbuf_release(&cmd);
}

static int run_argv(int *argcp, const char ***argv)
{
	int done_alias = 0;

	while (1) {
		/*
		 * If we tried alias and futzed with our environment,
		 * it no longer is safe to invoke builtins directly in
		 * general.  We have to spawn them as dashed externals.
		 *
		 * NEEDSWORK: if we can figure out cases
		 * where it is safe to do, we can avoid spawning a new
		 * process.
		 */
		if (!done_alias)
			handle_builtin(*argcp, *argv);

		/* .. then try the external ones */
		execv_dashed_external(*argv);

		/* It could be an alias -- this works around the insanity
		 * of overriding "git log" with "git show" by having
		 * alias.log = show
		 */
		if (done_alias)
			break;
		if (!handle_alias(argcp, argv))
			break;
		done_alias = 1;
	}

	return done_alias;
}

int cmd_main(int argc, const char **argv)
{
	const char *cmd;
	int done_help = 0;

	cmd = argv[0];
	if (!cmd)
		cmd = "git-help";

	trace_command_performance(argv);

	/*
	 * "git-xxxx" is the same as "git xxxx", but we obviously:
	 *
	 *  - cannot take flags in between the "git" and the "xxxx".
	 *  - cannot execute it externally (since it would just do
	 *    the same thing over again)
	 *
	 * So we just directly call the builtin handler, and die if
	 * that one cannot handle it.
	 */
	if (skip_prefix(cmd, "git-", &cmd)) {
		argv[0] = cmd;
		handle_builtin(argc, argv);
		die("cannot handle %s as a builtin", cmd);
	}

	/* Look for flags.. */
	argv++;
	argc--;
	handle_options(&argv, &argc, NULL);
	if (argc > 0) {
		/* translate --help and --version into commands */
		skip_prefix(argv[0], "--", &argv[0]);
	} else {
		/* The user didn't specify a command; give them help */
		commit_pager_choice();
		printf("usage: %s\n\n", git_usage_string);
		list_common_cmds_help();
		printf("\n%s\n", _(git_more_info_string));
		exit(1);
	}
	cmd = argv[0];

	/*
	 * We use PATH to find git commands, but we prepend some higher
	 * precedence paths: the "--exec-path" option, the GIT_EXEC_PATH
	 * environment, and the $(gitexecdir) from the Makefile at build
	 * time.
	 */
	setup_path();

	while (1) {
		int was_alias = run_argv(&argc, &argv);
		if (errno != ENOENT)
			break;
		if (was_alias) {
			fprintf(stderr, "Expansion of alias '%s' failed; "
				"'%s' is not a git command\n",
				cmd, argv[0]);
			exit(1);
		}
		if (!done_help) {
			cmd = argv[0] = help_unknown_cmd(cmd);
			done_help = 1;
		} else
			break;
	}

	fprintf(stderr, "Failed to run command '%s': %s\n",
		cmd, strerror(errno));

	return 1;
}
