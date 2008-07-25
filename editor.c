#include "cache.h"
#include "strbuf.h"
#include "run-command.h"

void launch_editor(const char *path, struct strbuf *buffer, const char *const *env)
{
	const char *editor, *terminal;

	editor = getenv("GIT_EDITOR");
	if (!editor && editor_program)
		editor = editor_program;
	if (!editor)
		editor = getenv("VISUAL");
	if (!editor)
		editor = getenv("EDITOR");

	terminal = getenv("TERM");
	if (!editor && (!terminal || !strcmp(terminal, "dumb"))) {
		fprintf(stderr,
		"Terminal is dumb but no VISUAL nor EDITOR defined.\n"
		"Please supply the message using either -m or -F option.\n");
		exit(1);
	}

	if (!editor)
		editor = "vi";

	if (strcmp(editor, ":")) {
		size_t len = strlen(editor);
		int i = 0;
		const char *args[6];
		struct strbuf arg0;

		strbuf_init(&arg0, 0);
		if (strcspn(editor, "$ \t'") != len) {
			/* there are specials */
			strbuf_addf(&arg0, "%s \"$@\"", editor);
			args[i++] = "sh";
			args[i++] = "-c";
			args[i++] = arg0.buf;
		}
		args[i++] = editor;
		args[i++] = path;
		args[i] = NULL;

		if (run_command_v_opt_cd_env(args, 0, NULL, env))
			die("There was a problem with the editor %s.", editor);
		strbuf_release(&arg0);
	}

	if (!buffer)
		return;
	if (strbuf_read_file(buffer, path, 0) < 0)
		die("could not read message file '%s': %s",
		    path, strerror(errno));
}
