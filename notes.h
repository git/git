#ifndef NOTES_H
#define NOTES_H

#include "string-list.h"

/*
 * Function type for combining two notes annotating the same object.
 *
 * When adding a new note annotating the same object as an existing note, it is
 * up to the caller to decide how to combine the two notes. The decision is
 * made by passing in a function of the following form. The function accepts
 * two SHA1s -- of the existing note and the new note, respectively. The
 * function then combines the notes in whatever way it sees fit, and writes the
 * resulting SHA1 into the first SHA1 argument (cur_sha1). A non-zero return
 * value indicates failure.
 *
 * The two given SHA1s shall both be non-NULL and different from each other.
 * Either of them (but not both) may be == null_sha1, which indicates an
 * empty/non-existent note. If the resulting SHA1 (cur_sha1) is == null_sha1,
 * the note will be removed from the notes tree.
 *
 * The default combine_notes function (you get this when passing NULL) is
 * combine_notes_concatenate(), which appends the contents of the new note to
 * the contents of the existing note.
 */
typedef int (*combine_notes_fn)(unsigned char *cur_sha1, const unsigned char *new_sha1);

/* Common notes combinators */
int combine_notes_concatenate(unsigned char *cur_sha1, const unsigned char *new_sha1);
int combine_notes_overwrite(unsigned char *cur_sha1, const unsigned char *new_sha1);
int combine_notes_ignore(unsigned char *cur_sha1, const unsigned char *new_sha1);
int combine_notes_cat_sort_uniq(unsigned char *cur_sha1, const unsigned char *new_sha1);

/*
 * Notes tree object
 *
 * Encapsulates the internal notes tree structure associated with a notes ref.
 * Whenever a struct notes_tree pointer is required below, you may pass NULL in
 * order to use the default/internal notes tree. E.g. you only need to pass a
 * non-NULL value if you need to refer to several different notes trees
 * simultaneously.
 */
extern struct notes_tree {
	struct int_node *root;
	struct non_note *first_non_note, *prev_non_note;
	char *ref;
	combine_notes_fn combine_notes;
	int initialized;
	int dirty;
} default_notes_tree;

/*
 * Return the default notes ref.
 *
 * The default notes ref is the notes ref that is used when notes_ref == NULL
 * is passed to init_notes().
 *
 * This the first of the following to be defined:
 * 1. The '--ref' option to 'git notes', if given
 * 2. The $GIT_NOTES_REF environment variable, if set
 * 3. The value of the core.notesRef config variable, if set
 * 4. GIT_NOTES_DEFAULT_REF (i.e. "refs/notes/commits")
 */
const char *default_notes_ref(void);

/*
 * Flags controlling behaviour of notes tree initialization
 *
 * Default behaviour is to initialize the notes tree from the tree object
 * specified by the given (or default) notes ref.
 */
#define NOTES_INIT_EMPTY 1

/*
 * Initialize the given notes_tree with the notes tree structure at the given
 * ref. If given ref is NULL, the value of the $GIT_NOTES_REF environment
 * variable is used, and if that is missing, the default notes ref is used
 * ("refs/notes/commits").
 *
 * If you need to re-intialize a notes_tree structure (e.g. when switching from
 * one notes ref to another), you must first de-initialize the notes_tree
 * structure by calling free_notes(struct notes_tree *).
 *
 * If you pass t == NULL, the default internal notes_tree will be initialized.
 *
 * The combine_notes function that is passed becomes the default combine_notes
 * function for the given notes_tree. If NULL is passed, the default
 * combine_notes function is combine_notes_concatenate().
 *
 * Precondition: The notes_tree structure is zeroed (this can be achieved with
 * memset(t, 0, sizeof(struct notes_tree)))
 */
void init_notes(struct notes_tree *t, const char *notes_ref,
		combine_notes_fn combine_notes, int flags);

/*
 * Add the given note object to the given notes_tree structure
 *
 * If there already exists a note for the given object_sha1, the given
 * combine_notes function is invoked to break the tie. If not given (i.e.
 * combine_notes == NULL), the default combine_notes function for the given
 * notes_tree is used.
 *
 * Passing note_sha1 == null_sha1 indicates the addition of an
 * empty/non-existent note. This is a (potentially expensive) no-op unless
 * there already exists a note for the given object_sha1, AND combining that
 * note with the empty note (using the given combine_notes function) results
 * in a new/changed note.
 *
 * Returns zero on success; non-zero means combine_notes failed.
 *
 * IMPORTANT: The changes made by add_note() to the given notes_tree structure
 * are not persistent until a subsequent call to write_notes_tree() returns
 * zero.
 */
int add_note(struct notes_tree *t, const unsigned char *object_sha1,
		const unsigned char *note_sha1, combine_notes_fn combine_notes);

/*
 * Remove the given note object from the given notes_tree structure
 *
 * IMPORTANT: The changes made by remove_note() to the given notes_tree
 * structure are not persistent until a subsequent call to write_notes_tree()
 * returns zero.
 *
 * Return 0 if a note was removed; 1 if there was no note to remove.
 */
int remove_note(struct notes_tree *t, const unsigned char *object_sha1);

/*
 * Get the note object SHA1 containing the note data for the given object
 *
 * Return NULL if the given object has no notes.
 */
const unsigned char *get_note(struct notes_tree *t,
		const unsigned char *object_sha1);

/*
 * Copy a note from one object to another in the given notes_tree.
 *
 * Returns 1 if the to_obj already has a note and 'force' is false. Otherwise,
 * returns non-zero if 'force' is true, but the given combine_notes function
 * failed to combine from_obj's note with to_obj's existing note.
 * Returns zero on success.
 *
 * IMPORTANT: The changes made by copy_note() to the given notes_tree structure
 * are not persistent until a subsequent call to write_notes_tree() returns
 * zero.
 */
int copy_note(struct notes_tree *t,
	      const unsigned char *from_obj, const unsigned char *to_obj,
	      int force, combine_notes_fn combine_notes);

/*
 * Flags controlling behaviour of for_each_note()
 *
 * Default behaviour of for_each_note() is to traverse every single note object
 * in the given notes tree, unpacking subtree entries along the way.
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
 * Invoke the specified callback function for each note in the given notes_tree
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
 * - copy_note()
 * - free_notes()
 */
typedef int each_note_fn(const unsigned char *object_sha1,
		const unsigned char *note_sha1, char *note_path,
		void *cb_data);
int for_each_note(struct notes_tree *t, int flags, each_note_fn fn,
		void *cb_data);

/*
 * Write the given notes_tree structure to the object database
 *
 * Creates a new tree object encapsulating the current state of the given
 * notes_tree, and stores its SHA1 into the 'result' argument.
 *
 * Returns zero on success, non-zero on failure.
 *
 * IMPORTANT: Changes made to the given notes_tree are not persistent until
 * this function has returned zero. Please also remember to create a
 * corresponding commit object, and update the appropriate notes ref.
 */
int write_notes_tree(struct notes_tree *t, unsigned char *result);

/* Flags controlling the operation of prune */
#define NOTES_PRUNE_VERBOSE 1
#define NOTES_PRUNE_DRYRUN 2
/*
 * Remove all notes annotating non-existing objects from the given notes tree
 *
 * All notes in the given notes_tree that are associated with objects that no
 * longer exist in the database, are removed from the notes tree.
 *
 * IMPORTANT: The changes made by prune_notes() to the given notes_tree
 * structure are not persistent until a subsequent call to write_notes_tree()
 * returns zero.
 */
void prune_notes(struct notes_tree *t, int flags);

/*
 * Free (and de-initialize) the given notes_tree structure
 *
 * IMPORTANT: Changes made to the given notes_tree since the last, successful
 * call to write_notes_tree() will be lost.
 */
void free_notes(struct notes_tree *t);

/* Flags controlling how notes are formatted */
#define NOTES_SHOW_HEADER 1
#define NOTES_INDENT 2

struct string_list;

struct display_notes_opt {
	int use_default_notes;
	struct string_list extra_notes_refs;
};

/*
 * Load the notes machinery for displaying several notes trees.
 *
 * If 'opt' is not NULL, then it specifies additional settings for the
 * displaying:
 *
 * - suppress_default_notes indicates that the notes from
 *   core.notesRef and notes.displayRef should not be loaded.
 *
 * - extra_notes_refs may contain a list of globs (in the same style
 *   as notes.displayRef) where notes should be loaded from.
 */
void init_display_notes(struct display_notes_opt *opt);

/*
 * Append notes for the given 'object_sha1' from all trees set up by
 * init_display_notes() to 'sb'.  The 'flags' are a bitwise
 * combination of
 *
 * - NOTES_SHOW_HEADER: add a 'Notes (refname):' header
 *
 * - NOTES_INDENT: indent the notes by 4 places
 *
 * You *must* call init_display_notes() before using this function.
 */
void format_display_notes(const unsigned char *object_sha1,
			  struct strbuf *sb, const char *output_encoding, int flags);

/*
 * Load the notes tree from each ref listed in 'refs'.  The output is
 * an array of notes_tree*, terminated by a NULL.
 */
struct notes_tree **load_notes_trees(struct string_list *refs);

/*
 * Add all refs that match 'glob' to the 'list'.
 */
void string_list_add_refs_by_glob(struct string_list *list, const char *glob);

/*
 * Add all refs from a colon-separated glob list 'globs' to the end of
 * 'list'.  Empty components are ignored.  This helper is used to
 * parse GIT_NOTES_DISPLAY_REF style environment variables.
 */
void string_list_add_refs_from_colon_sep(struct string_list *list,
					 const char *globs);

/* Expand inplace a note ref like "foo" or "notes/foo" into "refs/notes/foo" */
void expand_notes_ref(struct strbuf *sb);

#endif
