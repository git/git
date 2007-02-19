#include "builtin.h"
#include "exec_cmd.h"
#include "cache.h"
#include "quote.h"

const char git_usage_string[] =
	"git [--version] [--exec-path[=GIT_EXEC_PATH]] [-p|--paginate] [--bare] [--git-dir=GIT_DIR] [--help] COMMAND [ARGS]";

static void prepend_to_path(const char *dir, int len)
{
	const char *old_path = getenv("PATH");
	char *path;
	int path_len = len;

	if (!old_path)
		old_path = "/usr/local/bin:/usr/bin:/bin";

	path_len = len + strlen(old_path) + 1;

	path = xmalloc(path_len + 1);

	memcpy(path, dir, len);
	path[len] = ':';
	memcpy(path + len + 1, old_path, path_len - len);

	setenv("PATH", path, 1);

	free(path);
}

static int handle_options(const char*** argv, int* argc)
{
	int handled = 0;

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
		if (!prefixcmp(cmd, "--exec-path")) {
			cmd += 11;
			if (*cmd == '=')
				git_set_exec_path(cmd + 1);
			else {
				puts(git_exec_path());
				exit(0);
			}
		} else if (!strcmp(cmd, "-p") || !strcmp(cmd, "--paginate")) {
			setup_pager();
		} else if (!strcmp(cmd, "--git-dir")) {
			if (*argc < 2) {
				fprintf(stderr, "No directory given for --git-dir.\n" );
				usage(git_usage_string);
			}
			setenv(GIT_DIR_ENVIRONMENT, (*argv)[1], 1);
			(*argv)++;
			(*argc)--;
		} else if (!prefixcmp(cmd, "--git-dir=")) {
			setenv(GIT_DIR_ENVIRONMENT, cmd + 10, 1);
		} else if (!strcmp(cmd, "--bare")) {
			static char git_dir[PATH_MAX+1];
			setenv(GIT_DIR_ENVIRONMENT, getcwd(git_dir, sizeof(git_dir)), 1);
		} else {
			fprintf(stderr, "Unknown option: %s\n", cmd);
			usage(git_usage_string);
		}

		(*argv)++;
		(*argc)--;
		handled++;
	}
	return handled;
}

static const char *alias_command;
static char *alias_string;

static int git_alias_config(const char *var, const char *value)
{
	if (!prefixcmp(var, "alias.") && !strcmp(var + 6, alias_command)) {
		alias_string = xstrdup(value);
	}
	return 0;
}

static int split_cmdline(char *cmdline, const char ***argv)
{
	int src, dst, count = 0, size = 16;
	char quoted = 0;

	*argv = malloc(sizeof(char*) * size);

	/* split alias_string */
	(*argv)[count++] = cmdline;
	for (src = dst = 0; cmdline[src];) {
		char c = cmdline[src];
		if (!quoted && isspace(c)) {
			cmdline[dst++] = 0;
			while (cmdline[++src]
					&& isspace(cmdline[src]))
				; /* skip */
			if (count >= size) {
				size += 16;
				*argv = xrealloc(*argv, sizeof(char*) * size);
			}
			(*argv)[count++] = cmdline + dst;
		} else if(!quoted && (c == '\'' || c == '"')) {
			quoted = c;
			src++;
		} else if (c == quoted) {
			quoted = 0;
			src++;
		} else {
			if (c == '\\' && quoted != '\'') {
				src++;
				c = cmdline[src];
				if (!c) {
					free(*argv);
					*argv = NULL;
					return error("cmdline ends with \\");
				}
			}
			cmdline[dst++] = c;
			src++;
		}
	}

	cmdline[dst] = 0;

	if (quoted) {
		free(*argv);
		*argv = NULL;
		return error("unclosed quote");
	}

	return count;
}

static int handle_alias(int *argcp, const char ***argv)
{
	int nongit = 0, ret = 0, saved_errno = errno;
	const char *subdir;
	int count, option_count;
	const char** new_argv;

	subdir = setup_git_directory_gently(&nongit);

	alias_command = (*argv)[0];
	git_config(git_alias_config);
	if (alias_string) {
		if (alias_string[0] == '!') {
			trace_printf("trace: alias to shell cmd: %s => %s\n",
				     alias_command, alias_string + 1);
			ret = system(alias_string + 1);
			if (ret >= 0 && WIFEXITED(ret) &&
			    WEXITSTATUS(ret) != 127)
				exit(WEXITSTATUS(ret));
			die("Failed to run '%s' when expanding alias '%s'\n",
			    alias_string + 1, alias_command);
		}
		count = split_cmdline(alias_string, &new_argv);
		option_count = handle_options(&new_argv, &count);
		memmove(new_argv - option_count, new_argv,
				count * sizeof(char *));
		new_argv -= option_count;

		if (count < 1)
			die("empty alias for %s", alias_command);

		if (!strcmp(alias_command, new_argv[0]))
			die("recursive alias: %s", alias_command);

		trace_argv_printf(new_argv, count,
				  "trace: alias expansion: %s =>",
				  alias_command);

		new_argv = xrealloc(new_argv, sizeof(char*) *
				    (count + *argcp + 1));
		/* insert after command name */
		memcpy(new_argv + count, *argv + 1, sizeof(char*) * *argcp);
		new_argv[count+*argcp] = NULL;

		*argv = new_argv;
		*argcp += count - 1;

		ret = 1;
	}

	if (subdir)
		chdir(subdir);

	errno = saved_errno;

	return ret;
}

const char git_version_string[] = GIT_VERSION;

#define RUN_SETUP	(1<<0)
#define USE_PAGER	(1<<1)
/*
 * require working tree to be present -- anything uses this needs
 * RUN_SETUP for reading from the configuration file.
 */
#define NOT_BARE 	(1<<2)

static void handle_internal_command(int argc, const char **argv, char **envp)
{
	const char *cmd = argv[0];
	static struct cmd_struct {
		const char *cmd;
		int (*fn)(int, const char **, const char *);
		int option;
	} commands[] = {
		{ "add", cmd_add, RUN_SETUP | NOT_BARE },
		{ "annotate", cmd_annotate, USE_PAGER },
		{ "apply", cmd_apply },
		{ "archive", cmd_archive },
		{ "blame", cmd_blame, RUN_SETUP },
		{ "branch", cmd_branch, RUN_SETUP },
		{ "cat-file", cmd_cat_file, RUN_SETUP },
		{ "checkout-index", cmd_checkout_index, RUN_SETUP },
		{ "check-ref-format", cmd_check_ref_format },
		{ "cherry", cmd_cherry, RUN_SETUP },
		{ "commit-tree", cmd_commit_tree, RUN_SETUP },
		{ "config", cmd_config },
		{ "count-objects", cmd_count_objects, RUN_SETUP },
		{ "describe", cmd_describe, RUN_SETUP },
		{ "diff", cmd_diff, RUN_SETUP | USE_PAGER },
		{ "diff-files", cmd_diff_files, RUN_SETUP },
		{ "diff-index", cmd_diff_index, RUN_SETUP },
		{ "diff-tree", cmd_diff_tree, RUN_SETUP },
		{ "fmt-merge-msg", cmd_fmt_merge_msg, RUN_SETUP },
		{ "for-each-ref", cmd_for_each_ref, RUN_SETUP },
		{ "format-patch", cmd_format_patch, RUN_SETUP },
		{ "fsck", cmd_fsck, RUN_SETUP },
		{ "fsck-objects", cmd_fsck, RUN_SETUP },
		{ "get-tar-commit-id", cmd_get_tar_commit_id },
		{ "grep", cmd_grep, RUN_SETUP | USE_PAGER },
		{ "help", cmd_help },
		{ "init", cmd_init_db },
		{ "init-db", cmd_init_db },
		{ "log", cmd_log, RUN_SETUP | USE_PAGER },
		{ "ls-files", cmd_ls_files, RUN_SETUP },
		{ "ls-tree", cmd_ls_tree, RUN_SETUP },
		{ "mailinfo", cmd_mailinfo },
		{ "mailsplit", cmd_mailsplit },
		{ "merge-base", cmd_merge_base, RUN_SETUP },
		{ "merge-file", cmd_merge_file },
		{ "mv", cmd_mv, RUN_SETUP | NOT_BARE },
		{ "name-rev", cmd_name_rev, RUN_SETUP },
		{ "pack-objects", cmd_pack_objects, RUN_SETUP },
		{ "pickaxe", cmd_blame, RUN_SETUP | USE_PAGER },
		{ "prune", cmd_prune, RUN_SETUP },
		{ "prune-packed", cmd_prune_packed, RUN_SETUP },
		{ "push", cmd_push, RUN_SETUP },
		{ "read-tree", cmd_read_tree, RUN_SETUP },
		{ "reflog", cmd_reflog, RUN_SETUP },
		{ "repo-config", cmd_config },
		{ "rerere", cmd_rerere, RUN_SETUP },
		{ "rev-list", cmd_rev_list, RUN_SETUP },
		{ "rev-parse", cmd_rev_parse, RUN_SETUP },
		{ "rm", cmd_rm, RUN_SETUP | NOT_BARE },
		{ "runstatus", cmd_runstatus, RUN_SETUP | NOT_BARE },
		{ "shortlog", cmd_shortlog, RUN_SETUP | USE_PAGER },
		{ "show-branch", cmd_show_branch, RUN_SETUP },
		{ "show", cmd_show, RUN_SETUP | USE_PAGER },
		{ "stripspace", cmd_stripspace },
		{ "symbolic-ref", cmd_symbolic_ref, RUN_SETUP },
		{ "tar-tree", cmd_tar_tree },
		{ "unpack-objects", cmd_unpack_objects, RUN_SETUP },
		{ "update-index", cmd_update_index, RUN_SETUP },
		{ "update-ref", cmd_update_ref, RUN_SETUP },
		{ "upload-archive", cmd_upload_archive },
		{ "version", cmd_version },
		{ "whatchanged", cmd_whatchanged, RUN_SETUP | USE_PAGER },
		{ "write-tree", cmd_write_tree, RUN_SETUP },
		{ "verify-pack", cmd_verify_pack },
		{ "show-ref", cmd_show_ref, RUN_SETUP },
		{ "pack-refs", cmd_pack_refs, RUN_SETUP },
	};
	int i;

	/* Turn "git cmd --help" into "git help cmd" */
	if (argc > 1 && !strcmp(argv[1], "--help")) {
		argv[1] = argv[0];
		argv[0] = cmd = "help";
	}

	for (i = 0; i < ARRAY_SIZE(commands); i++) {
		struct cmd_struct *p = commands+i;
		const char *prefix;
		if (strcmp(p->cmd, cmd))
			continue;

		prefix = NULL;
		if (p->option & RUN_SETUP)
			prefix = setup_git_directory();
		if (p->option & USE_PAGER)
			setup_pager();
		if ((p->option & NOT_BARE) &&
				(is_bare_repository() || is_inside_git_dir()))
			die("%s must be run in a work tree", cmd);
		trace_argv_printf(argv, argc, "trace: built-in: git");

		exit(p->fn(argc, argv, prefix));
	}
}

int main(int argc, const char **argv, char **envp)
{
	const char *cmd = argv[0] ? argv[0] : "git-help";
	char *slash = strrchr(cmd, '/');
	const char *exec_path = NULL;
	int done_alias = 0;

	/*
	 * Take the basename of argv[0] as the command
	 * name, and the dirname as the default exec_path
	 * if it's an absolute path and we don't have
	 * anything better.
	 */
	if (slash) {
		*slash++ = 0;
		if (*cmd == '/')
			exec_path = cmd;
		cmd = slash;
	}

	/*
	 * "git-xxxx" is the same as "git xxxx", but we obviously:
	 *
	 *  - cannot take flags in between the "git" and the "xxxx".
	 *  - cannot execute it externally (since it would just do
	 *    the same thing over again)
	 *
	 * So we just directly call the internal command handler, and
	 * die if that one cannot handle it.
	 */
	if (!prefixcmp(cmd, "git-")) {
		cmd += 4;
		argv[0] = cmd;
		handle_internal_command(argc, argv, envp);
		die("cannot handle %s internally", cmd);
	}

	/* Look for flags.. */
	argv++;
	argc--;
	handle_options(&argv, &argc);
	if (argc > 0) {
		if (!prefixcmp(argv[0], "--"))
			argv[0] += 2;
	} else {
		/* Default command: "help" */
		argv[0] = "help";
		argc = 1;
	}
	cmd = argv[0];

	/*
	 * We search for git commands in the following order:
	 *  - git_exec_path()
	 *  - the path of the "git" command if we could find it
	 *    in $0
	 *  - the regular PATH.
	 */
	if (exec_path)
		prepend_to_path(exec_path, strlen(exec_path));
	exec_path = git_exec_path();
	prepend_to_path(exec_path, strlen(exec_path));

	while (1) {
		/* See if it's an internal command */
		handle_internal_command(argc, argv, envp);

		/* .. then try the external ones */
		execv_git_cmd(argv);

		/* It could be an alias -- this works around the insanity
		 * of overriding "git log" with "git show" by having
		 * alias.log = show
		 */
		if (done_alias || !handle_alias(&argc, &argv))
			break;
		done_alias = 1;
	}

	if (errno == ENOENT) {
		if (done_alias) {
			fprintf(stderr, "Expansion of alias '%s' failed; "
				"'%s' is not a git-command\n",
				cmd, argv[0]);
			exit(1);
		}
		help_unknown_cmd(cmd);
	}

	fprintf(stderr, "Failed to run command '%s': %s\n",
		cmd, strerror(errno));

	return 1;
}
