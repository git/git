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

/*
 * Add the given note object to the internal notes tree structure
 *
 * IMPORTANT: The changes made by add_note() to the internal notes tree structure
 * are not persistent until a subsequent call to write_notes_tree() returns
 * zero.
 */
void add_note(const unsigned char *object_sha1,
		const unsigned char *note_sha1);

/*
 * Remove the given note object from the internal notes tree structure
 *
 * IMPORTANT: The changes made by remove_note() to the internal notes tree
 * structure are not persistent until a subsequent call to write_notes_tree()
 * returns zero.
 */
void remove_note(const unsigned char *object_sha1);

/*
 * Get the note object SHA1 containing the note data for the given object
 *
 * Return NULL if the given object has no notes.
 */
const unsigned char *get_note(const unsigned char *object_sha1);

/*
 * Flags controlling behaviour of for_each_note()
 *
 * Default behaviour of for_each_note() is to traverse every single note object
 * in the notes tree, unpacking subtree entries along the way.
 * The following flags can be used to alter the default behaviour:
 *
 * - DONT_UNPACK_SUBTREES causes for_each_note() NOT to unpack and recurse into
 *   subtree entries while traversing the notes tree. This causes notes within
 *   those subtrees NOT to be passed to the callback. Use this flag if you
 *   don't want to traverse _all_ notes, but only want to traverse the parts
 *   of the notes tree that have already been unpacked (this includes at least
 *   all notes that have been added/changed).
 *
 * - YIELD_SUBTREES causes any subtree entries that are encountered to be
 *   passed to the callback, before recursing into them. Subtree entries are
 *   not note objects, but represent intermediate directories in the notes
 *   tree. When passed to the callback, subtree entries will have a trailing
 *   slash in their path, which the callback may use to differentiate between
 *   note entries and subtree entries. Note that already-unpacked subtree
 *   entries are not part of the notes tree, and will therefore not be yielded.
 *   If this flag is used together with DONT_UNPACK_SUBTREES, for_each_note()
 *   will yield the subtree entry, but not recurse into it.
 */
#define FOR_EACH_NOTE_DONT_UNPACK_SUBTREES 1
#define FOR_EACH_NOTE_YIELD_SUBTREES 2

/*
 * Invoke the specified callback function for each note
 *
 * If the callback returns nonzero, the note walk is aborted, and the return
 * value from the callback is returned from for_each_note(). Hence, a zero
 * return value from for_each_note() indicates that all notes were walked
 * successfully.
 *
 * IMPORTANT: The callback function is NOT allowed to change the notes tree.
 * In other words, the following functions can NOT be invoked (on the current
 * notes tree) from within the callback:
 * - add_note()
 * - remove_note()
 * - free_notes()
 */
typedef int each_note_fn(const unsigned char *object_sha1,
		const unsigned char *note_sha1, char *note_path,
		void *cb_data);
int for_each_note(int flags, each_note_fn fn, void *cb_data);

/*
 * Write the internal notes tree structure to the object database
 *
 * Creates a new tree object encapsulating the current state of the
 * internal notes tree, and stores its SHA1 into the 'result' argument.
 *
 * Returns zero on success, non-zero on failure.
 *
 * IMPORTANT: Changes made to the internal notes tree structure are not
 * persistent until this function has returned zero. Please also remember
 * to create a corresponding commit object, and update the appropriate
 * notes ref.
 */
int write_notes_tree(unsigned char *result);

/*
 * Free (and de-initialize) the internal notes tree structure
 *
 * IMPORTANT: Changes made to the notes tree since the last, successful
 * call to write_notes_tree() will be lost.
 */
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
