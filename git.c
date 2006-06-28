#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include "git-compat-util.h"
#include "exec_cmd.h"
#include "cache.h"

#include "builtin.h"

static void prepend_to_path(const char *dir, int len)
{
	char *path, *old_path = getenv("PATH");
	int path_len = len;

	if (!old_path)
		old_path = "/usr/local/bin:/usr/bin:/bin";

	path_len = len + strlen(old_path) + 1;

	path = malloc(path_len + 1);

	memcpy(path, dir, len);
	path[len] = ':';
	memcpy(path + len + 1, old_path, path_len - len);

	setenv("PATH", path, 1);
}

static const char *alias_command;
static char *alias_string = NULL;

static int git_alias_config(const char *var, const char *value)
{
	if (!strncmp(var, "alias.", 6) && !strcmp(var + 6, alias_command)) {
		alias_string = strdup(value);
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
				*argv = realloc(*argv, sizeof(char*) * size);
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
	int nongit = 0, ret = 0;
	const char *subdir;

	subdir = setup_git_directory_gently(&nongit);
	if (!nongit) {
		int count;
		const char** new_argv;

		alias_command = (*argv)[0];
		git_config(git_alias_config);
		if (alias_string) {

			count = split_cmdline(alias_string, &new_argv);

			if (count < 1)
				die("empty alias for %s", alias_command);

			if (!strcmp(alias_command, new_argv[0]))
				die("recursive alias: %s", alias_command);

			/* insert after command name */
			if (*argcp > 1) {
				new_argv = realloc(new_argv, sizeof(char*) *
						   (count + *argcp));
				memcpy(new_argv + count, *argv + 1,
				       sizeof(char*) * *argcp);
			}

			*argv = new_argv;
			*argcp += count - 1;

			ret = 1;
		}
	}

	if (subdir)
		chdir(subdir);

	return ret;
}

const char git_version_string[] = GIT_VERSION;

static void handle_internal_command(int argc, const char **argv, char **envp)
{
	const char *cmd = argv[0];
	static struct cmd_struct {
		const char *cmd;
		int (*fn)(int, const char **, char **);
	} commands[] = {
		{ "version", cmd_version },
		{ "help", cmd_help },
		{ "log", cmd_log },
		{ "whatchanged", cmd_whatchanged },
		{ "show", cmd_show },
		{ "push", cmd_push },
		{ "format-patch", cmd_format_patch },
		{ "count-objects", cmd_count_objects },
		{ "diff", cmd_diff },
		{ "grep", cmd_grep },
		{ "rm", cmd_rm },
		{ "add", cmd_add },
		{ "rev-list", cmd_rev_list },
		{ "init-db", cmd_init_db },
		{ "get-tar-commit-id", cmd_get_tar_commit_id },
		{ "upload-tar", cmd_upload_tar },
		{ "check-ref-format", cmd_check_ref_format },
		{ "ls-files", cmd_ls_files },
		{ "ls-tree", cmd_ls_tree },
		{ "tar-tree", cmd_tar_tree },
		{ "read-tree", cmd_read_tree },
		{ "commit-tree", cmd_commit_tree },
		{ "apply", cmd_apply },
		{ "show-branch", cmd_show_branch },
		{ "diff-files", cmd_diff_files },
		{ "diff-index", cmd_diff_index },
		{ "diff-stages", cmd_diff_stages },
		{ "diff-tree", cmd_diff_tree },
		{ "cat-file", cmd_cat_file },
		{ "rev-parse", cmd_rev_parse },
		{ "write-tree", cmd_write_tree },
		{ "mailsplit", cmd_mailsplit },
		{ "mailinfo", cmd_mailinfo },
		{ "stripspace", cmd_stripspace },
		{ "update-index", cmd_update_index },
		{ "update-ref", cmd_update_ref }
	};
	int i;

	/* Turn "git cmd --help" into "git help cmd" */
	if (argc > 1 && !strcmp(argv[1], "--help")) {
		argv[1] = argv[0];
		argv[0] = cmd = "help";
	}

	for (i = 0; i < ARRAY_SIZE(commands); i++) {
		struct cmd_struct *p = commands+i;
		if (strcmp(p->cmd, cmd))
			continue;
		exit(p->fn(argc, argv, envp));
	}
}

int main(int argc, const char **argv, char **envp)
{
	const char *cmd = argv[0];
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
	if (!strncmp(cmd, "git-", 4)) {
		cmd += 4;
		argv[0] = cmd;
		handle_internal_command(argc, argv, envp);
		die("cannot handle %s internally", cmd);
	}

	/* Default command: "help" */
	cmd = "help";

	/* Look for flags.. */
	while (argc > 1) {
		cmd = *++argv;
		argc--;

		if (strncmp(cmd, "--", 2))
			break;

		cmd += 2;

		/*
		 * For legacy reasons, the "version" and "help"
		 * commands can be written with "--" prepended
		 * to make them look like flags.
		 */
		if (!strcmp(cmd, "help"))
			break;
		if (!strcmp(cmd, "version"))
			break;

		/*
		 * Check remaining flags (which by now must be
		 * "--exec-path", but maybe we will accept
		 * other arguments some day)
		 */
		if (!strncmp(cmd, "exec-path", 9)) {
			cmd += 9;
			if (*cmd == '=') {
				git_set_exec_path(cmd + 1);
				continue;
			}
			puts(git_exec_path());
			exit(0);
		}
		cmd_usage(0, NULL, NULL);
	}
	argv[0] = cmd;

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

	if (errno == ENOENT)
		cmd_usage(0, exec_path, "'%s' is not a git-command", cmd);

	fprintf(stderr, "Failed to run command '%s': %s\n",
		cmd, strerror(errno));

	return 1;
}
