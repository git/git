#ifndef NOTES_H
#define NOTES_H

/*
 * Flags controlling behaviour of notes tree initialization
 *
 * Default behaviour is to initialize the notes tree from the tree object
 * specified by the given (or default) notes ref.
 */
#define NOTES_INIT_EMPTY 1

/*
 * Initialize internal notes tree structure with the notes tree at the given
 * ref. If given ref is NULL, the value of the $GIT_NOTES_REF environment
 * variable is used, and if that is missing, the default notes ref is used
 * ("refs/notes/commits").
 *
 * If you need to re-intialize the internal notes tree structure (e.g. loading
 * from a different notes ref), please first de-initialize the current notes
 * tree by calling free_notes().
 */
void init_notes(const char *notes_ref, int flags);

/* Add the given note object to the internal notes tree structure */
void add_note(const unsigned char *object_sha1,
		const unsigned char *note_sha1);

/* Remove the given note object from the internal notes tree structure */
void remove_note(const unsigned char *object_sha1);

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
