#include "cache.h"
#include "config.h"
#include "strbuf.h"
#include "run-command.h"
#include "sigchain.h"

#ifndef DEFAULT_EDITOR
#define DEFAULT_EDITOR "vi"
#endif

int is_terminal_dumb(void)
{
	const char *terminal = getenv("TERM");
	return !terminal || !strcmp(terminal, "dumb");
}

const char *git_editor(void)
{
	const char *editor = getenv("GIT_EDITOR");
	int terminal_is_dumb = is_terminal_dumb();

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

const char *git_sequence_editor(void)
{
	const char *editor = getenv("GIT_SEQUENCE_EDITOR");

	if (!editor)
		git_config_get_string_const("sequence.editor", &editor);
	if (!editor)
		editor = git_editor();

	return editor;
}

static int launch_specified_editor(const char *editor, const char *path,
				   struct strbuf *buffer, const char *const *env)
{
	if (!editor)
		return error("Terminal is dumb, but EDITOR unset");

	if (strcmp(editor, ":")) {
		const char *args[] = { editor, real_path(path), NULL };
		struct child_process p = CHILD_PROCESS_INIT;
		int ret, sig;
		int print_waiting_for_editor = advice_waiting_for_editor && isatty(2);

		if (print_waiting_for_editor) {
			/*
			 * A dumb terminal cannot erase the line later on. Add a
			 * newline to separate the hint from subsequent output.
			 *
			 * Make sure that our message is separated with a whitespace
			 * from further cruft that may be written by the editor.
			 */
			const char term = is_terminal_dumb() ? '\n' : ' ';

			fprintf(stderr,
				_("hint: Waiting for your editor to close the file...%c"),
				term);
			fflush(stderr);
		}

		p.argv = args;
		p.env = env;
		p.use_shell = 1;
		p.trace2_child_class = "editor";
		if (start_command(&p) < 0)
			return error("unable to start editor '%s'", editor);

		sigchain_push(SIGINT, SIG_IGN);
		sigchain_push(SIGQUIT, SIG_IGN);
		ret = finish_command(&p);
		sig = ret - 128;
		sigchain_pop(SIGINT);
		sigchain_pop(SIGQUIT);
		if (sig == SIGINT || sig == SIGQUIT)
			raise(sig);
		if (ret)
			return error("There was a problem with the editor '%s'.",
					editor);

		if (print_waiting_for_editor && !is_terminal_dumb())
			/*
			 * Go back to the beginning and erase the entire line to
			 * avoid wasting the vertical space.
			 */
			fputs("\r\033[K", stderr);
	}

	if (!buffer)
		return 0;
	if (strbuf_read_file(buffer, path, 0) < 0)
		return error_errno("could not read file '%s'", path);
	return 0;
}

int launch_editor(const char *path, struct strbuf *buffer, const char *const *env)
{
	return launch_specified_editor(git_editor(), path, buffer, env);
}

int launch_sequence_editor(const char *path, struct strbuf *buffer,
			   const char *const *env)
{
	return launch_specified_editor(git_sequence_editor(), path, buffer, env);
}
