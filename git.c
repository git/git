#define USE_THE_REPOSITORY_VARIABLE

#include "builtin.h"
#include "config.h"
#include "environment.h"
#include "exec-cmd.h"
#include "gettext.h"
#include "help.h"
#include "object-file.h"
#include "pager.h"
#include "read-cache-ll.h"
#include "run-command.h"
#include "alias.h"
#include "replace-object.h"
#include "setup.h"
#include "attr.h"
#include "shallow.h"
#include "trace.h"
#include "trace2.h"

#define RUN_SETUP		(1<<0)
#define RUN_SETUP_GENTLY	(1<<1)
#define USE_PAGER		(1<<2)
/*
 * require working tree to be present -- anything uses this needs
 * RUN_SETUP for reading from the configuration file.
 */
#define NEED_WORK_TREE		(1<<3)
#define DELAY_PAGER_CONFIG	(1<<4)
#define NO_PARSEOPT		(1<<5) /* parse-options is not used */

struct cmd_struct {
	const char *cmd;
	int (*fn)(int, const char **, const char *);
	unsigned int option;
};

const char git_usage_string[] =
	N_("git [-v | --version] [-h | --help] [-C <path>] [-c <name>=<value>]\n"
	   "           [--exec-path[=<path>]] [--html-path] [--man-path] [--info-path]\n"
	   "           [-p | --paginate | -P | --no-pager] [--no-replace-objects] [--no-lazy-fetch]\n"
	   "           [--no-optional-locks] [--no-advice] [--bare] [--git-dir=<path>]\n"
	   "           [--work-tree=<path>] [--namespace=<name>] [--config-env=<name>=<envvar>]\n"
	   "           <command> [<args>]");

const char git_more_info_string[] =
	N_("'git help -a' and 'git help -g' list available subcommands and some\n"
	   "concept guides. See 'git help <command>' or 'git help <concept>'\n"
	   "to read about a specific subcommand or concept.\n"
	   "See 'git help git' for an overview of the system.");

static int use_pager = -1;

static void list_builtins(struct string_list *list, unsigned int exclude_option);

static void exclude_helpers_from_list(struct string_list *list)
{
	int i = 0;

	while (i < list->nr) {
		if (strstr(list->items[i].string, "--"))
			unsorted_string_list_delete_item(list, i, 0);
		else
			i++;
	}
}

static int match_token(const char *spec, int len, const char *token)
{
	int token_len = strlen(token);

	return len == token_len && !strncmp(spec, token, token_len);
}

static int list_cmds(const char *spec)
{
	struct string_list list = STRING_LIST_INIT_DUP;
	int i;
	int nongit;

	/*
	* Set up the repository so we can pick up any repo-level config (like
	* completion.commands).
	*/
	setup_git_directory_gently(&nongit);

	while (*spec) {
		const char *sep = strchrnul(spec, ',');
		int len = sep - spec;

		if (match_token(spec, len, "builtins"))
			list_builtins(&list, 0);
		else if (match_token(spec, len, "main"))
			list_all_main_cmds(&list);
		else if (match_token(spec, len, "others"))
			list_all_other_cmds(&list);
		else if (match_token(spec, len, "nohelpers"))
			exclude_helpers_from_list(&list);
		else if (match_token(spec, len, "alias"))
			list_aliases(&list);
		else if (match_token(spec, len, "config"))
			list_cmds_by_config(&list);
		else if (len > 5 && !strncmp(spec, "list-", 5)) {
			struct strbuf sb = STRBUF_INIT;

			strbuf_add(&sb, spec + 5, len - 5);
			list_cmds_by_category(&list, sb.buf);
			strbuf_release(&sb);
		}
		else
			die(_("unsupported command listing type '%s'"), spec);
		spec += len;
		if (*spec == ',')
			spec++;
	}
	for (i = 0; i < list.nr; i++)
		puts(list.items[i].string);
	string_list_clear(&list, 0);
	return 0;
}

static void commit_pager_choice(void)
{
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

void setup_auto_pager(const char *cmd, int def)
{
	if (use_pager != -1 || pager_in_use())
		return;
	use_pager = check_pager_config(cmd);
	if (use_pager == -1)
		use_pager = def;
	commit_pager_choice();
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
		if (!strcmp(cmd, "--help") || !strcmp(cmd, "-h") ||
		    !strcmp(cmd, "--version") || !strcmp(cmd, "-v"))
			break;

		/*
		 * Check remaining flags.
		 */
		if (skip_prefix(cmd, "--exec-path", &cmd)) {
			if (*cmd == '=')
				git_set_exec_path(cmd + 1);
			else {
				puts(git_exec_path());
				trace2_cmd_name("_query_");
				exit(0);
			}
		} else if (!strcmp(cmd, "--html-path")) {
			puts(system_path(GIT_HTML_PATH));
			trace2_cmd_name("_query_");
			exit(0);
		} else if (!strcmp(cmd, "--man-path")) {
			puts(system_path(GIT_MAN_PATH));
			trace2_cmd_name("_query_");
			exit(0);
		} else if (!strcmp(cmd, "--info-path")) {
			puts(system_path(GIT_INFO_PATH));
			trace2_cmd_name("_query_");
			exit(0);
		} else if (!strcmp(cmd, "-p") || !strcmp(cmd, "--paginate")) {
			use_pager = 1;
		} else if (!strcmp(cmd, "-P") || !strcmp(cmd, "--no-pager")) {
			use_pager = 0;
			if (envchanged)
				*envchanged = 1;
		} else if (!strcmp(cmd, "--no-lazy-fetch")) {
			fetch_if_missing = 0;
			setenv(NO_LAZY_FETCH_ENVIRONMENT, "1", 1);
			if (envchanged)
				*envchanged = 1;
		} else if (!strcmp(cmd, "--no-replace-objects")) {
			disable_replace_refs();
			setenv(NO_REPLACE_OBJECTS_ENVIRONMENT, "1", 1);
			if (envchanged)
				*envchanged = 1;
		} else if (!strcmp(cmd, "--git-dir")) {
			if (*argc < 2) {
				fprintf(stderr, _("no directory given for '%s' option\n" ), "--git-dir");
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
				fprintf(stderr, _("no namespace given for --namespace\n" ));
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
				fprintf(stderr, _("no directory given for '%s' option\n" ), "--work-tree");
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
				fprintf(stderr, _("-c expects a configuration string\n" ));
				usage(git_usage_string);
			}
			git_config_push_parameter((*argv)[1]);
			(*argv)++;
			(*argc)--;
		} else if (!strcmp(cmd, "--config-env")) {
			if (*argc < 2) {
				fprintf(stderr, _("no config key given for --config-env\n" ));
				usage(git_usage_string);
			}
			git_config_push_env((*argv)[1]);
			(*argv)++;
			(*argc)--;
		} else if (skip_prefix(cmd, "--config-env=", &cmd)) {
			git_config_push_env(cmd);
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
		} else if (!strcmp(cmd, "--no-optional-locks")) {
			setenv(GIT_OPTIONAL_LOCKS_ENVIRONMENT, "0", 1);
			if (envchanged)
				*envchanged = 1;
		} else if (!strcmp(cmd, "--shallow-file")) {
			(*argv)++;
			(*argc)--;
			set_alternate_shallow_file(the_repository, (*argv)[0], 1);
			if (envchanged)
				*envchanged = 1;
		} else if (!strcmp(cmd, "-C")) {
			if (*argc < 2) {
				fprintf(stderr, _("no directory given for '%s' option\n" ), "-C");
				usage(git_usage_string);
			}
			if ((*argv)[1][0]) {
				if (chdir((*argv)[1]))
					die_errno("cannot change to '%s'", (*argv)[1]);
				if (envchanged)
					*envchanged = 1;
			}
			(*argv)++;
			(*argc)--;
		} else if (skip_prefix(cmd, "--list-cmds=", &cmd)) {
			trace2_cmd_name("_query_");
			if (!strcmp(cmd, "parseopt")) {
				struct string_list list = STRING_LIST_INIT_DUP;
				int i;

				list_builtins(&list, NO_PARSEOPT);
				for (i = 0; i < list.nr; i++)
					printf("%s ", list.items[i].string);
				string_list_clear(&list, 0);
				exit(0);
			} else {
				exit(list_cmds(cmd));
			}
		} else if (!strcmp(cmd, "--attr-source")) {
			if (*argc < 2) {
				fprintf(stderr, _("no attribute source given for --attr-source\n" ));
				usage(git_usage_string);
			}
			setenv(GIT_ATTR_SOURCE_ENVIRONMENT, (*argv)[1], 1);
			if (envchanged)
				*envchanged = 1;
			(*argv)++;
			(*argc)--;
		} else if (skip_prefix(cmd, "--attr-source=", &cmd)) {
			set_git_attr_source(cmd);
			setenv(GIT_ATTR_SOURCE_ENVIRONMENT, cmd, 1);
			if (envchanged)
				*envchanged = 1;
		} else if (!strcmp(cmd, "--no-advice")) {
			setenv(GIT_ADVICE_ENVIRONMENT, "0", 1);
			if (envchanged)
				*envchanged = 1;
		} else {
			fprintf(stderr, _("unknown option: %s\n"), cmd);
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

	alias_command = (*argv)[0];
	alias_string = alias_lookup(alias_command);
	if (alias_string) {
		if (*argcp > 1 && !strcmp((*argv)[1], "-h"))
			fprintf_ln(stderr, _("'%s' is aliased to '%s'"),
				   alias_command, alias_string);
		if (alias_string[0] == '!') {
			struct child_process child = CHILD_PROCESS_INIT;
			int nongit_ok;

			/* Aliases expect GIT_PREFIX, GIT_DIR etc to be set */
			setup_git_directory_gently(&nongit_ok);

			commit_pager_choice();

			child.use_shell = 1;
			child.clean_on_exit = 1;
			child.wait_after_clean = 1;
			child.trace2_child_class = "shell_alias";
			strvec_push(&child.args, alias_string + 1);
			strvec_pushv(&child.args, (*argv) + 1);

			trace2_cmd_alias(alias_command, child.args.v);
			trace2_cmd_name("_run_shell_alias_");

			ret = run_command(&child);
			if (ret >= 0)   /* normal exit */
				exit(ret);

			die_errno(_("while expanding alias '%s': '%s'"),
				  alias_command, alias_string + 1);
		}
		count = split_cmdline(alias_string, &new_argv);
		if (count < 0)
			die(_("bad alias.%s string: %s"), alias_command,
			    _(split_cmdline_strerror(count)));
		option_count = handle_options(&new_argv, &count, &envchanged);
		if (envchanged)
			die(_("alias '%s' changes environment variables.\n"
			      "You can use '!git' in the alias to do this"),
			    alias_command);
		MOVE_ARRAY(new_argv - option_count, new_argv, count);
		new_argv -= option_count;

		if (count < 1)
			die(_("empty alias for %s"), alias_command);

		if (!strcmp(alias_command, new_argv[0]))
			die(_("recursive alias: %s"), alias_command);

		trace_argv_printf(new_argv,
				  "trace: alias expansion: %s =>",
				  alias_command);

		REALLOC_ARRAY(new_argv, count + *argcp);
		/* insert after command name */
		COPY_ARRAY(new_argv + count, *argv + 1, *argcp);

		trace2_cmd_alias(alias_command, new_argv);

		*argv = new_argv;
		*argcp += count - 1;

		ret = 1;
	}

	errno = saved_errno;

	return ret;
}

static int run_builtin(struct cmd_struct *p, int argc, const char **argv)
{
	int status, help;
	struct stat st;
	const char *prefix;
	int run_setup = (p->option & (RUN_SETUP | RUN_SETUP_GENTLY));

	help = argc == 2 && !strcmp(argv[1], "-h");
	if (help && (run_setup & RUN_SETUP))
		/* demote to GENTLY to allow 'git cmd -h' outside repo */
		run_setup = RUN_SETUP_GENTLY;

	if (run_setup & RUN_SETUP) {
		prefix = setup_git_directory();
	} else if (run_setup & RUN_SETUP_GENTLY) {
		int nongit_ok;
		prefix = setup_git_directory_gently(&nongit_ok);
	} else {
		prefix = NULL;
	}
	assert(!prefix || *prefix);
	precompose_argv_prefix(argc, argv, NULL);
	if (use_pager == -1 && run_setup &&
		!(p->option & DELAY_PAGER_CONFIG))
		use_pager = check_pager_config(p->cmd);
	if (use_pager == -1 && p->option & USE_PAGER)
		use_pager = 1;
	if (run_setup && startup_info->have_repository)
		/* get_git_dir() may set up repo, avoid that */
		trace_repo_setup();
	commit_pager_choice();

	if (!help && p->option & NEED_WORK_TREE)
		setup_work_tree();

	trace_argv_printf(argv, "trace: built-in: git");
	trace2_cmd_name(p->cmd);

	validate_cache_entries(the_repository->index);
	status = p->fn(argc, argv, prefix);
	validate_cache_entries(the_repository->index);

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
		die_errno(_("write failure on standard output"));
	if (ferror(stdout))
		die(_("unknown write failure on standard output"));
	if (fclose(stdout))
		die_errno(_("close failed on standard output"));
	return 0;
}

static struct cmd_struct commands[] = {
	{ "add", cmd_add, RUN_SETUP | NEED_WORK_TREE },
	{ "am", cmd_am, RUN_SETUP | NEED_WORK_TREE },
	{ "annotate", cmd_annotate, RUN_SETUP },
	{ "apply", cmd_apply, RUN_SETUP_GENTLY },
	{ "archive", cmd_archive, RUN_SETUP_GENTLY },
	{ "bisect", cmd_bisect, RUN_SETUP },
	{ "blame", cmd_blame, RUN_SETUP },
	{ "branch", cmd_branch, RUN_SETUP | DELAY_PAGER_CONFIG },
	{ "bugreport", cmd_bugreport, RUN_SETUP_GENTLY },
	{ "bundle", cmd_bundle, RUN_SETUP_GENTLY },
	{ "cat-file", cmd_cat_file, RUN_SETUP },
	{ "check-attr", cmd_check_attr, RUN_SETUP },
	{ "check-ignore", cmd_check_ignore, RUN_SETUP | NEED_WORK_TREE },
	{ "check-mailmap", cmd_check_mailmap, RUN_SETUP },
	{ "check-ref-format", cmd_check_ref_format, NO_PARSEOPT  },
	{ "checkout", cmd_checkout, RUN_SETUP | NEED_WORK_TREE },
	{ "checkout--worker", cmd_checkout__worker,
		RUN_SETUP | NEED_WORK_TREE },
	{ "checkout-index", cmd_checkout_index,
		RUN_SETUP | NEED_WORK_TREE},
	{ "cherry", cmd_cherry, RUN_SETUP },
	{ "cherry-pick", cmd_cherry_pick, RUN_SETUP | NEED_WORK_TREE },
	{ "clean", cmd_clean, RUN_SETUP | NEED_WORK_TREE },
	{ "clone", cmd_clone },
	{ "column", cmd_column, RUN_SETUP_GENTLY },
	{ "commit", cmd_commit, RUN_SETUP | NEED_WORK_TREE },
	{ "commit-graph", cmd_commit_graph, RUN_SETUP },
	{ "commit-tree", cmd_commit_tree, RUN_SETUP },
	{ "config", cmd_config, RUN_SETUP_GENTLY | DELAY_PAGER_CONFIG },
	{ "count-objects", cmd_count_objects, RUN_SETUP },
	{ "credential", cmd_credential, RUN_SETUP_GENTLY | NO_PARSEOPT },
	{ "credential-cache", cmd_credential_cache },
	{ "credential-cache--daemon", cmd_credential_cache_daemon },
	{ "credential-store", cmd_credential_store },
	{ "describe", cmd_describe, RUN_SETUP },
	{ "diagnose", cmd_diagnose, RUN_SETUP_GENTLY },
	{ "diff", cmd_diff, NO_PARSEOPT },
	{ "diff-files", cmd_diff_files, RUN_SETUP | NEED_WORK_TREE | NO_PARSEOPT },
	{ "diff-index", cmd_diff_index, RUN_SETUP | NO_PARSEOPT },
	{ "diff-tree", cmd_diff_tree, RUN_SETUP | NO_PARSEOPT },
	{ "difftool", cmd_difftool, RUN_SETUP_GENTLY },
	{ "fast-export", cmd_fast_export, RUN_SETUP },
	{ "fast-import", cmd_fast_import, RUN_SETUP | NO_PARSEOPT },
	{ "fetch", cmd_fetch, RUN_SETUP },
	{ "fetch-pack", cmd_fetch_pack, RUN_SETUP | NO_PARSEOPT },
	{ "fmt-merge-msg", cmd_fmt_merge_msg, RUN_SETUP },
	{ "for-each-ref", cmd_for_each_ref, RUN_SETUP },
	{ "for-each-repo", cmd_for_each_repo, RUN_SETUP_GENTLY },
	{ "format-patch", cmd_format_patch, RUN_SETUP },
	{ "fsck", cmd_fsck, RUN_SETUP },
	{ "fsck-objects", cmd_fsck, RUN_SETUP },
	{ "fsmonitor--daemon", cmd_fsmonitor__daemon, RUN_SETUP },
	{ "gc", cmd_gc, RUN_SETUP },
	{ "get-tar-commit-id", cmd_get_tar_commit_id, NO_PARSEOPT },
	{ "grep", cmd_grep, RUN_SETUP_GENTLY },
	{ "hash-object", cmd_hash_object },
	{ "help", cmd_help },
	{ "hook", cmd_hook, RUN_SETUP },
	{ "index-pack", cmd_index_pack, RUN_SETUP_GENTLY | NO_PARSEOPT },
	{ "init", cmd_init_db },
	{ "init-db", cmd_init_db },
	{ "interpret-trailers", cmd_interpret_trailers, RUN_SETUP_GENTLY },
	{ "log", cmd_log, RUN_SETUP },
	{ "ls-files", cmd_ls_files, RUN_SETUP },
	{ "ls-remote", cmd_ls_remote, RUN_SETUP_GENTLY },
	{ "ls-tree", cmd_ls_tree, RUN_SETUP },
	{ "mailinfo", cmd_mailinfo, RUN_SETUP_GENTLY },
	{ "mailsplit", cmd_mailsplit, NO_PARSEOPT },
	{ "maintenance", cmd_maintenance, RUN_SETUP },
	{ "merge", cmd_merge, RUN_SETUP | NEED_WORK_TREE },
	{ "merge-base", cmd_merge_base, RUN_SETUP },
	{ "merge-file", cmd_merge_file, RUN_SETUP_GENTLY },
	{ "merge-index", cmd_merge_index, RUN_SETUP | NO_PARSEOPT },
	{ "merge-ours", cmd_merge_ours, RUN_SETUP | NO_PARSEOPT },
	{ "merge-recursive", cmd_merge_recursive, RUN_SETUP | NEED_WORK_TREE | NO_PARSEOPT },
	{ "merge-recursive-ours", cmd_merge_recursive, RUN_SETUP | NEED_WORK_TREE | NO_PARSEOPT },
	{ "merge-recursive-theirs", cmd_merge_recursive, RUN_SETUP | NEED_WORK_TREE | NO_PARSEOPT },
	{ "merge-subtree", cmd_merge_recursive, RUN_SETUP | NEED_WORK_TREE | NO_PARSEOPT },
	{ "merge-tree", cmd_merge_tree, RUN_SETUP },
	{ "mktag", cmd_mktag, RUN_SETUP },
	{ "mktree", cmd_mktree, RUN_SETUP },
	{ "multi-pack-index", cmd_multi_pack_index, RUN_SETUP },
	{ "mv", cmd_mv, RUN_SETUP | NEED_WORK_TREE },
	{ "name-rev", cmd_name_rev, RUN_SETUP },
	{ "notes", cmd_notes, RUN_SETUP },
	{ "pack-objects", cmd_pack_objects, RUN_SETUP },
	{ "pack-redundant", cmd_pack_redundant, RUN_SETUP | NO_PARSEOPT },
	{ "pack-refs", cmd_pack_refs, RUN_SETUP },
	{ "patch-id", cmd_patch_id, RUN_SETUP_GENTLY | NO_PARSEOPT },
	{ "pickaxe", cmd_blame, RUN_SETUP },
	{ "prune", cmd_prune, RUN_SETUP },
	{ "prune-packed", cmd_prune_packed, RUN_SETUP },
	{ "pull", cmd_pull, RUN_SETUP | NEED_WORK_TREE },
	{ "push", cmd_push, RUN_SETUP },
	{ "range-diff", cmd_range_diff, RUN_SETUP | USE_PAGER },
	{ "read-tree", cmd_read_tree, RUN_SETUP },
	{ "rebase", cmd_rebase, RUN_SETUP | NEED_WORK_TREE },
	{ "receive-pack", cmd_receive_pack },
	{ "reflog", cmd_reflog, RUN_SETUP },
	{ "refs", cmd_refs, RUN_SETUP },
	{ "remote", cmd_remote, RUN_SETUP },
	{ "remote-ext", cmd_remote_ext, NO_PARSEOPT },
	{ "remote-fd", cmd_remote_fd, NO_PARSEOPT },
	{ "repack", cmd_repack, RUN_SETUP },
	{ "replace", cmd_replace, RUN_SETUP },
	{ "replay", cmd_replay, RUN_SETUP },
	{ "rerere", cmd_rerere, RUN_SETUP },
	{ "reset", cmd_reset, RUN_SETUP },
	{ "restore", cmd_restore, RUN_SETUP | NEED_WORK_TREE },
	{ "rev-list", cmd_rev_list, RUN_SETUP | NO_PARSEOPT },
	{ "rev-parse", cmd_rev_parse, NO_PARSEOPT },
	{ "revert", cmd_revert, RUN_SETUP | NEED_WORK_TREE },
	{ "rm", cmd_rm, RUN_SETUP },
	{ "send-pack", cmd_send_pack, RUN_SETUP },
	{ "shortlog", cmd_shortlog, RUN_SETUP_GENTLY | USE_PAGER },
	{ "show", cmd_show, RUN_SETUP },
	{ "show-branch", cmd_show_branch, RUN_SETUP },
	{ "show-index", cmd_show_index, RUN_SETUP_GENTLY },
	{ "show-ref", cmd_show_ref, RUN_SETUP },
	{ "sparse-checkout", cmd_sparse_checkout, RUN_SETUP },
	{ "stage", cmd_add, RUN_SETUP | NEED_WORK_TREE },
	{ "stash", cmd_stash, RUN_SETUP | NEED_WORK_TREE },
	{ "status", cmd_status, RUN_SETUP | NEED_WORK_TREE },
	{ "stripspace", cmd_stripspace },
	{ "submodule--helper", cmd_submodule__helper, RUN_SETUP },
	{ "switch", cmd_switch, RUN_SETUP | NEED_WORK_TREE },
	{ "symbolic-ref", cmd_symbolic_ref, RUN_SETUP },
	{ "tag", cmd_tag, RUN_SETUP | DELAY_PAGER_CONFIG },
	{ "unpack-file", cmd_unpack_file, RUN_SETUP | NO_PARSEOPT },
	{ "unpack-objects", cmd_unpack_objects, RUN_SETUP | NO_PARSEOPT },
	{ "update-index", cmd_update_index, RUN_SETUP },
	{ "update-ref", cmd_update_ref, RUN_SETUP },
	{ "update-server-info", cmd_update_server_info, RUN_SETUP },
	{ "upload-archive", cmd_upload_archive, NO_PARSEOPT },
	{ "upload-archive--writer", cmd_upload_archive_writer, NO_PARSEOPT },
	{ "upload-pack", cmd_upload_pack },
	{ "var", cmd_var, RUN_SETUP_GENTLY | NO_PARSEOPT },
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

static void list_builtins(struct string_list *out, unsigned int exclude_option)
{
	int i;
	for (i = 0; i < ARRAY_SIZE(commands); i++) {
		if (exclude_option &&
		    (commands[i].option & exclude_option))
			continue;
		string_list_append(out, commands[i].cmd);
	}
}

void load_builtin_commands(const char *prefix, struct cmdnames *cmds)
{
	const char *name;
	int i;

	/*
	 * Callers can ask for a subset of the commands based on a certain
	 * prefix, which is then dropped from the added names. The names in
	 * the `commands[]` array do not have the `git-` prefix, though,
	 * therefore we must expect the `prefix` to at least start with `git-`.
	 */
	if (!skip_prefix(prefix, "git-", &prefix))
		BUG("prefix '%s' must start with 'git-'", prefix);

	for (i = 0; i < ARRAY_SIZE(commands); i++)
		if (skip_prefix(commands[i].cmd, prefix, &name))
			add_cmdname(cmds, name, strlen(name));
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
	struct strvec args = STRVEC_INIT;
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
			strvec_push(&args, argv[i]);
			if (!i)
				strvec_push(&args, "--exclude-guides");
		}

		argc++;
		argv = args.v;
	}

	builtin = get_builtin(cmd);
	if (builtin)
		exit(run_builtin(builtin, argc, argv));
	strvec_clear(&args);
}

static void execv_dashed_external(const char **argv)
{
	struct child_process cmd = CHILD_PROCESS_INIT;
	int status;

	if (use_pager == -1 && !is_builtin(argv[0]))
		use_pager = check_pager_config(argv[0]);
	commit_pager_choice();

	strvec_pushf(&cmd.args, "git-%s", argv[0]);
	strvec_pushv(&cmd.args, argv + 1);
	cmd.clean_on_exit = 1;
	cmd.wait_after_clean = 1;
	cmd.silent_exec_failure = 1;
	cmd.trace2_child_class = "dashed";

	trace2_cmd_name("_run_dashed_");

	/*
	 * The code in run_command() logs trace2 child_start/child_exit
	 * events, so we do not need to report exec/exec_result events here.
	 */
	trace_argv_printf(cmd.args.v, "trace: exec:");

	/*
	 * If we fail because the command is not found, it is
	 * OK to return. Otherwise, we just pass along the status code,
	 * or our usual generic code if we were not even able to exec
	 * the program.
	 */
	status = run_command(&cmd);

	/*
	 * If the child process ran and we are now going to exit, emit a
	 * generic string as our trace2 command verb to indicate that we
	 * launched a dashed command.
	 */
	if (status >= 0)
		exit(status);
	else if (errno != ENOENT)
		exit(128);
}

static int run_argv(int *argcp, const char ***argv)
{
	int done_alias = 0;
	struct string_list cmd_list = STRING_LIST_INIT_NODUP;
	struct string_list_item *seen;

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
		else if (get_builtin(**argv)) {
			struct child_process cmd = CHILD_PROCESS_INIT;
			int i;

			/*
			 * The current process is committed to launching a
			 * child process to run the command named in (**argv)
			 * and exiting.  Log a generic string as the trace2
			 * command verb to indicate this.  Note that the child
			 * process will log the actual verb when it runs.
			 */
			trace2_cmd_name("_run_git_alias_");

			commit_pager_choice();

			strvec_push(&cmd.args, "git");
			for (i = 0; i < *argcp; i++)
				strvec_push(&cmd.args, (*argv)[i]);

			trace_argv_printf(cmd.args.v, "trace: exec:");

			/*
			 * if we fail because the command is not found, it is
			 * OK to return. Otherwise, we just pass along the status code.
			 */
			cmd.silent_exec_failure = 1;
			cmd.clean_on_exit = 1;
			cmd.wait_after_clean = 1;
			cmd.trace2_child_class = "git_alias";
			i = run_command(&cmd);
			if (i >= 0 || errno != ENOENT)
				exit(i);
			die("could not execute builtin %s", **argv);
		}

		/* .. then try the external ones */
		execv_dashed_external(*argv);

		seen = unsorted_string_list_lookup(&cmd_list, *argv[0]);
		if (seen) {
			int i;
			struct strbuf sb = STRBUF_INIT;
			for (i = 0; i < cmd_list.nr; i++) {
				struct string_list_item *item = &cmd_list.items[i];

				strbuf_addf(&sb, "\n  %s", item->string);
				if (item == seen)
					strbuf_addstr(&sb, " <==");
				else if (i == cmd_list.nr - 1)
					strbuf_addstr(&sb, " ==>");
			}
			die(_("alias loop detected: expansion of '%s' does"
			      " not terminate:%s"), cmd_list.items[0].string, sb.buf);
		}

		string_list_append(&cmd_list, *argv[0]);

		/*
		 * It could be an alias -- this works around the insanity
		 * of overriding "git log" with "git show" by having
		 * alias.log = show
		 */
		if (!handle_alias(argcp, argv))
			break;
		done_alias = 1;
	}

	string_list_clear(&cmd_list, 0);

	return done_alias;
}

int cmd_main(int argc, const char **argv)
{
	const char *cmd;
	int done_help = 0;

	cmd = argv[0];
	if (!cmd)
		cmd = "git-help";
	else {
		const char *slash = find_last_dir_sep(cmd);
		if (slash)
			cmd = slash + 1;
	}

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
		die(_("cannot handle %s as a builtin"), cmd);
	}

	/* Look for flags.. */
	argv++;
	argc--;
	handle_options(&argv, &argc, NULL);

	if (!argc) {
		/* The user didn't specify a command; give them help */
		commit_pager_choice();
		printf(_("usage: %s\n\n"), git_usage_string);
		list_common_cmds_help();
		printf("\n%s\n", _(git_more_info_string));
		exit(1);
	}

	if (!strcmp("--version", argv[0]) || !strcmp("-v", argv[0]))
		argv[0] = "version";
	else if (!strcmp("--help", argv[0]) || !strcmp("-h", argv[0]))
		argv[0] = "help";

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
			fprintf(stderr, _("expansion of alias '%s' failed; "
					  "'%s' is not a git command\n"),
				cmd, argv[0]);
			exit(1);
		}
		if (!done_help) {
			cmd = argv[0] = help_unknown_cmd(cmd);
			done_help = 1;
		} else
			break;
	}

	fprintf(stderr, _("failed to run command '%s': %s\n"),
		cmd, strerror(errno));

	return 1;
}
