#include "cache.h"
#include "quote.h"
#include "exec_cmd.h"

static int do_generic_cmd(const char *me, char *arg)
{
	const char *my_argv[4];

	if (!arg || !(arg = sq_dequote(arg)))
		die("bad argument");
	if (prefixcmp(me, "git-"))
		die("bad command");

	my_argv[0] = me + 4;
	my_argv[1] = arg;
	my_argv[2] = NULL;

	return execv_git_cmd(my_argv);
}

static struct commands {
	const char *name;
	int (*exec)(const char *me, char *arg);
} cmd_list[] = {
	{ "git-receive-pack", do_generic_cmd },
	{ "git-upload-pack", do_generic_cmd },
	{ NULL },
};

int main(int argc, char **argv)
{
	char *prog;
	struct commands *cmd;

	/* We want to see "-c cmd args", and nothing else */
	if (argc != 3 || strcmp(argv[1], "-c"))
		die("What do you think I am? A shell?");

	prog = argv[2];
	argv += 2;
	argc -= 2;
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
