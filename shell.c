#include "cache.h"
#include "quote.h"
#include "exec_cmd.h"
#include "strbuf.h"

static int do_generic_cmd(const char *me, char *arg)
{
	const char *my_argv[4];

	setup_path();
	if (!arg || !(arg = sq_dequote(arg)))
		die("bad argument");
	if (prefixcmp(me, "git-"))
		die("bad command");

	my_argv[0] = me + 4;
	my_argv[1] = arg;
	my_argv[2] = NULL;

	return execv_git_cmd(my_argv);
}

static int do_cvs_cmd(const char *me, char *arg)
{
	const char *cvsserver_argv[3] = {
		"cvsserver", "server", NULL
	};

	if (!arg || strcmp(arg, "server"))
		die("git-cvsserver only handles server: %s", arg);

	setup_path();
	return execv_git_cmd(cvsserver_argv);
}


static struct commands {
	const char *name;
	int (*exec)(const char *me, char *arg);
} cmd_list[] = {
	{ "git-receive-pack", do_generic_cmd },
	{ "git-upload-pack", do_generic_cmd },
	{ "git-upload-archive", do_generic_cmd },
	{ "cvs", do_cvs_cmd },
	{ NULL },
};

int main(int argc, char **argv)
{
	char *prog;
	struct commands *cmd;
	int devnull_fd;

	/*
	 * Always open file descriptors 0/1/2 to avoid clobbering files
	 * in die().  It also avoids not messing up when the pipes are
	 * dup'ed onto stdin/stdout/stderr in the child processes we spawn.
	 */
	devnull_fd = open("/dev/null", O_RDWR);
	while (devnull_fd >= 0 && devnull_fd <= 2)
		devnull_fd = dup(devnull_fd);
	if (devnull_fd == -1)
		die_errno("opening /dev/null failed");
	close (devnull_fd);

	/*
	 * Special hack to pretend to be a CVS server
	 */
	if (argc == 2 && !strcmp(argv[1], "cvs server"))
		argv--;

	/*
	 * We do not accept anything but "-c" followed by "cmd arg",
	 * where "cmd" is a very limited subset of git commands.
	 */
	else if (argc != 3 || strcmp(argv[1], "-c"))
		die("What do you think I am? A shell?");

	prog = argv[2];
	if (!strncmp(prog, "git", 3) && isspace(prog[3]))
		/* Accept "git foo" as if the caller said "git-foo". */
		prog[3] = '-';

	for (cmd = cmd_list ; cmd->name ; cmd++) {
		int len = strlen(cmd->name);
		char *arg;
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
		exit(cmd->exec(cmd->name, arg));
	}
	die("unrecognized command '%s'", prog);
}
