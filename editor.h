#ifndef EDITOR_H
#define EDITOR_H

struct strbuf;

const char *git_editor(void);
const char *git_sequence_editor(void);
int is_terminal_dumb(void);

/**
 * Launch the user preferred editor to edit a file and fill the buffer
 * with the file's contents upon the user completing their editing. The
 * third argument can be used to set the environment which the editor is
 * run in. If the buffer is NULL the editor is launched as usual but the
 * file's contents are not read into the buffer upon completion.
 */
int launch_editor(const char *path, struct strbuf *buffer,
		  const char *const *env);

int launch_sequence_editor(const char *path, struct strbuf *buffer,
			   const char *const *env);

/*
 * In contrast to `launch_editor()`, this function writes out the contents
 * of the specified file first, then clears the `buffer`, then launches
 * the editor and reads back in the file contents into the `buffer`.
 * Finally, it deletes the temporary file.
 *
 * If `path` is relative, it refers to a file in the `.git` directory.
 */
int strbuf_edit_interactively(struct strbuf *buffer, const char *path,
			      const char *const *env);

#endif
