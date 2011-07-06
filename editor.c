#include "cache.h"
#include "strbuf.h"
#include "run-command.h"

int launch_editor(const char *path, struct strbuf *buffer, const char *const *env)
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
	if (!editor && (!terminal || !strcmp(terminal, "dumb")))
		return error("Terminal is dumb but no VISUAL nor EDITOR defined.");

	if (!editor)
		editor = "vi";

	if (strcmp(editor, ":")) {
		size_t len = strlen(editor);
		int i = 0;
		int failed;
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

		failed = run_command_v_opt_cd_env(args, 0, NULL, env);
		strbuf_release(&arg0);
		if (failed)
			return error("There was a problem with the editor '%s'.",
					editor);
	}

	if (!buffer)
		return 0;
	if (strbuf_read_file(buffer, path, 0) < 0)
		return error("could not read file '%s': %s",
				path, strerror(errno));
	return 0;
}
