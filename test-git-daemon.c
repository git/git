#include "git-compat-util.h"
#include "run-command.h"
#include "exec_cmd.h"
#include "strbuf.h"
#include <string.h>
#include <errno.h>

static int parse_daemon_output(char *s)
{
	if (*s++ != '[')
		return 1;
	s = strchr(s, ']');
	if (!s)
		return 1;
	if (strcmp(s, "] Ready to rumble\n"))
		return 1;

	return 0;
}

int main(int argc, char **argv)
{
	struct strbuf line = STRBUF_INIT;
	FILE *fp;
	struct child_process proc, cat;
	char *cat_argv[] = { "cat", NULL };

	setup_path();

	memset(&proc, 0, sizeof(proc));
	argv[0] = "git-daemon";
	proc.argv = (const char **)argv;
	proc.no_stdin = 1;
	proc.err = -1;

	if (start_command(&proc) < 0)
		return 1;

	strbuf_getwholeline_fd(&line, proc.err, '\n');
	fputs(line.buf, stderr);

	memset(&cat, 0, sizeof(cat));
	cat.argv = (const char **)cat_argv;
	cat.in = proc.err;
	cat.out = 2;

	if (start_command(&cat) < 0)
		return 1;

	if (parse_daemon_output(line.buf)) {
		kill(proc.pid, SIGTERM);
		finish_command(&proc);
		finish_command(&cat);
		return 1;
	}

	fp = fopen("git-daemon.pid", "w");
	fprintf(fp, "%"PRIuMAX"\n", (uintmax_t)proc.pid);
	fclose(fp);

	return 0;
}
