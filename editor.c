#include "cache.h"
#include "strbuf.h"
#include "run-command.h"

#ifndef DEFAULT_EDITOR
#define DEFAULT_EDITOR "vi"
#endif

const char *git_editor(void)
{
	const char *editor = getenv("GIT_EDITOR");
	const char *terminal = getenv("TERM");
	int terminal_is_dumb = !terminal || !strcmp(terminal, "dumb");

	if (!editor && editor_program)
		editor = editor_program;
	if (!editor && !terminal_is_dumb)
		editor = getenv("VISUAL");
	if (!editor)
		editor = getenv("EDITOR");

	if (!editor && terminal_is_dumb)
		return NULL;

	if (!editor)
		editor = DEFAULT_EDITOR;

	return editor;
}

int launch_editor(const char *path, struct strbuf *buffer, const char *const *env)
{
	const char *editor = git_editor();

	if (!editor)
		return error("Terminal is dumb, but EDITOR unset");

	if (strcmp(editor, ":")) {
		const char *args[] = { editor, path, NULL };

		if (run_command_v_opt_cd_env(args, RUN_USING_SHELL, NULL, env))
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
