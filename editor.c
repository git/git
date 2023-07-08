#include "git-compat-util.h"
#include "abspath.h"
#include "advice.h"
#include "config.h"
#include "editor.h"
#include "environment.h"
#include "gettext.h"
#include "pager.h"
#include "path.h"
#include "strbuf.h"
#include "strvec.h"
#include "run-command.h"
#include "sigchain.h"
#include "wrapper.h"

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
		git_config_get_string_tmp("sequence.editor", &editor);
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
		struct strbuf realpath = STRBUF_INIT;
		struct child_process p = CHILD_PROCESS_INIT;
		int ret, sig;
		int print_waiting_for_editor = advice_enabled(ADVICE_WAITING_FOR_EDITOR) && isatty(2);

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

		strbuf_realpath(&realpath, path, 1);

		strvec_pushl(&p.args, editor, realpath.buf, NULL);
		if (env)
			strvec_pushv(&p.env, (const char **)env);
		p.use_shell = 1;
		p.trace2_child_class = "editor";
		if (start_command(&p) < 0) {
			strbuf_release(&realpath);
			return error("unable to start editor '%s'", editor);
		}

		sigchain_push(SIGINT, SIG_IGN);
		sigchain_push(SIGQUIT, SIG_IGN);
		ret = finish_command(&p);
		strbuf_release(&realpath);
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
			 * Erase the entire line to avoid wasting the
			 * vertical space.
			 */
			term_clear_line();
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

int strbuf_edit_interactively(struct strbuf *buffer, const char *path,
			      const char *const *env)
{
	char *path2 = NULL;
	int fd, res = 0;

	if (!is_absolute_path(path))
		path = path2 = xstrdup(git_path("%s", path));

	fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
	if (fd < 0)
		res = error_errno(_("could not open '%s' for writing"), path);
	else if (write_in_full(fd, buffer->buf, buffer->len) < 0) {
		res = error_errno(_("could not write to '%s'"), path);
		close(fd);
	} else if (close(fd) < 0)
		res = error_errno(_("could not close '%s'"), path);
	else {
		strbuf_reset(buffer);
		if (launch_editor(path, buffer, env) < 0)
			res = error_errno(_("could not edit '%s'"), path);
		unlink(path);
	}

	free(path2);
	return res;
}
