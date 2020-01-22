#include "git-compat-util.h"
#include "run-command.h"
#include "strbuf.h"

int cmd_main(int argc, const char **argv)
{
	const char *trash_directory = getenv("TRASH_DIRECTORY");
	struct strbuf buf = STRBUF_INIT;
	FILE *f;
	int i;
	const char *child_argv[] = { NULL, NULL };

	/* First, print all parameters into $TRASH_DIRECTORY/ssh-output */
	if (!trash_directory)
		die("Need a TRASH_DIRECTORY!");
	strbuf_addf(&buf, "%s/ssh-output", trash_directory);
	f = fopen(buf.buf, "w");
	if (!f)
		die("Could not write to %s", buf.buf);
	for (i = 0; i < argc; i++)
		fprintf(f, "%s%s", i > 0 ? " " : "", i > 0 ? argv[i] : "ssh:");
	fprintf(f, "\n");
	fclose(f);

	/* Now, evaluate the *last* parameter */
	if (argc < 2)
		return 0;
	child_argv[0] = argv[argc - 1];
	return run_command_v_opt(child_argv, RUN_USING_SHELL);
}
