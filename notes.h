#ifndef NOTES_H
#define NOTES_H

/* Free (and de-initialize) the internal notes tree structure */
void free_notes(void);

/* Flags controlling how notes are formatted */
#define NOTES_SHOW_HEADER 1
#define NOTES_INDENT 2

/*
 * Fill the given strbuf with the notes associated with the given object.
 *
 * If the internal notes structure is not initialized, it will be auto-
 * initialized to the default value (see documentation for init_notes() above).
 *
 * 'flags' is a bitwise combination of the above formatting flags.
 */
void format_note(const unsigned char *object_sha1, struct strbuf *sb,
		const char *output_encoding, int flags);

#endif
