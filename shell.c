#include "git-compat-util.h"
#include "quote.h"
#include "exec-cmd.h"
#include "strbuf.h"
#include "run-command.h"
#include "alias.h"

#define COMMAND_DIR "git-shell-commands"
#define HELP_COMMAND COMMAND_DIR "/help"
#define NOLOGIN_COMMAND COMMAND_DIR "/no-interactive-login"

static int do_generic_cmd(const char *me, char *arg)
{
	const char *my_argv[4];

	setup_path();
	if (!arg || !(arg = sq_dequote(arg)) || *arg == '-')
		die("bad argument");
	if (!skip_prefix(me, "git-", &me))
		die("bad command");

	my_argv[0] = me;
	my_argv[1] = arg;
	my_argv[2] = NULL;

	return execv_git_cmd(my_argv);
}

static int is_valid_cmd_name(const char *cmd)
{
	/* Test command contains no . or / characters */
	return cmd[strcspn(cmd, "./")] == '\0';
}

static char *make_cmd(const char *prog)
{
	return xstrfmt("%s/%s", COMMAND_DIR, prog);
}

static void cd_to_homedir(void)
{
	const char *home = getenv("HOME");
	if (!home)
		die("could not determine user's home directory; HOME is unset");
	if (chdir(home) == -1)
		die("could not chdir to user's home directory");
}

#define MAX_INTERACTIVE_COMMAND (4*1024*1024)

static void run_shell(void)
{
	int done = 0;
	struct child_process help_cmd = CHILD_PROCESS_INIT;

	if (!access(NOLOGIN_COMMAND, F_OK)) {
		/* Interactive login disabled. */
		struct child_process nologin_cmd = CHILD_PROCESS_INIT;
		int status;

		strvec_push(&nologin_cmd.args, NOLOGIN_COMMAND);
		status = run_command(&nologin_cmd);
		if (status < 0)
			exit(127);
		exit(status);
	}

	/* Print help if enabled */
	help_cmd.silent_exec_failure = 1;
	strvec_push(&help_cmd.args, HELP_COMMAND);
	run_command(&help_cmd);

	do {
		const char *prog;
		char *full_cmd;
		char *rawargs;
		size_t len;
		char *split_args;
		const char **argv;
		int code;
		int count;

		fprintf(stderr, "git> ");

		/*
		 * Avoid using a strbuf or git_read_line_interactively() here.
		 * We don't want to allocate arbitrary amounts of memory on
		 * behalf of a possibly untrusted client, and we're subject to
		 * OS limits on command length anyway.
		 */
		fflush(stdout);
		rawargs = xmalloc(MAX_INTERACTIVE_COMMAND);
		if (!fgets(rawargs, MAX_INTERACTIVE_COMMAND, stdin)) {
			fprintf(stderr, "\n");
			free(rawargs);
			break;
		}
		len = strlen(rawargs);

		/*
		 * If we truncated due to our input buffer size, reject the
		 * command. That's better than running bogus input, and
		 * there's a good chance it's just malicious garbage anyway.
		 */
		if (len >= MAX_INTERACTIVE_COMMAND - 1)
			die("invalid command format: input too long");

		if (len > 0 && rawargs[len - 1] == '\n') {
			if (--len > 0 && rawargs[len - 1] == '\r')
				--len;
			rawargs[len] = '\0';
		}

		split_args = xstrdup(rawargs);
		count = split_cmdline(split_args, &argv);
		if (count < 0) {
			fprintf(stderr, "invalid command format '%s': %s\n", rawargs,
				split_cmdline_strerror(count));
			free(split_args);
			free(rawargs);
			continue;
		}

		prog = argv[0];
		if (!strcmp(prog, "")) {
		} else if (!strcmp(prog, "quit") || !strcmp(prog, "logout") ||
			   !strcmp(prog, "exit") || !strcmp(prog, "bye")) {
			done = 1;
		} else if (is_valid_cmd_name(prog)) {
			struct child_process cmd = CHILD_PROCESS_INIT;

			full_cmd = make_cmd(prog);
			argv[0] = full_cmd;
			cmd.silent_exec_failure = 1;
			strvec_pushv(&cmd.args, argv);
			code = run_command(&cmd);
			if (code == -1 && errno == ENOENT) {
				fprintf(stderr, "unrecognized command '%s'\n", prog);
			}
			free(full_cmd);
		} else {
			fprintf(stderr, "invalid command format '%s'\n", prog);
		}

		free(argv);
		free(split_args);
		free(rawargs);
	} while (!done);
}

static struct commands {
	const char *name;
	int (*exec)(const char *me, char *arg);
} cmd_list[] = {
	{ "git-receive-pack", do_generic_cmd },
	{ "git-upload-pack", do_generic_cmd },
	{ "git-upload-archive", do_generic_cmd },
	{ NULL },
};

int cmd_main(int argc, const char **argv)
{
	char *prog;
	const char **user_argv;
	struct commands *cmd;
	int count;

	/*
	 * Special hack to pretend to be a CVS server
	 */
	if (argc == 2 && !strcmp(argv[1], "cvs server")) {
		argv--;
	} else if (argc == 1) {
		/* Allow the user to run an interactive shell */
		cd_to_homedir();
		if (access(COMMAND_DIR, R_OK | X_OK) == -1) {
			die("Interactive git shell is not enabled.\n"
			    "hint: ~/" COMMAND_DIR " should exist "
			    "and have read and execute access.");
		}
		run_shell();
		exit(0);
	} else if (argc != 3 || strcmp(argv[1], "-c")) {
		/*
		 * We do not accept any other modes except "-c" followed by
		 * "cmd arg", where "cmd" is a very limited subset of git
		 * commands or a command in the COMMAND_DIR
		 */
		die("Run with no arguments or with -c cmd");
	}

	prog = xstrdup(argv[2]);
	if (!strncmp(prog, "git", 3) && isspace(prog[3]))
		/* Accept "git foo" as if the caller said "git-foo". */
		prog[3] = '-';

	cd_to_homedir();
	for (cmd = cmd_list ; cmd->name ; cmd++) {
		int len = strlen(cmd->name);
		char *arg;
		char *full_cmd;
		if (strncmp(cmd->name, prog, len))
			continue;
		arg = NULL;
		switch (prog[len]) {
		case '\0':
			arg = NULL;
			break;
		case ' ':
			arg = prog + len + 1;
			break;
		default:
			continue;
		}
		/* Allow overriding built-in commands */
		full_cmd = make_cmd(cmd->name);
		if (!access(full_cmd, X_OK)) {
			const char *argv[3] = { cmd->name, arg, NULL };
			return execv(full_cmd, (char *const *) argv);
		}
		return cmd->exec(cmd->name, arg);
	}

	count = split_cmdline(prog, &user_argv);
	if (count >= 0) {
		if (is_valid_cmd_name(user_argv[0])) {
			char *cmd = make_cmd(user_argv[0]);
			execv(cmd, (char *const *) user_argv);
		}
		free(prog);
		free(user_argv);
		die("unrecognized command '%s'", argv[2]);
	} else {
		free(prog);
		die("invalid command format '%s': %s", argv[2],
		    split_cmdline_strerror(count));
	}
}
