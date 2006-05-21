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
		{ "rev-list", cmd_rev_list },
		{ "init-db", cmd_init_db },
		{ "check-ref-format", cmd_check_ref_format }
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
	char git_command[PATH_MAX + 1];
	const char *exec_path = NULL;

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

	/* See if it's an internal command */
	handle_internal_command(argc, argv, envp);

	/* .. then try the external ones */
	execv_git_cmd(argv);

	if (errno == ENOENT)
		cmd_usage(0, exec_path, "'%s' is not a git-command", cmd);

	fprintf(stderr, "Failed to run command '%s': %s\n",
		git_command, strerror(errno));

	return 1;
}
