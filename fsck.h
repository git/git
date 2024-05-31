#ifndef GIT_FSCK_H
#define GIT_FSCK_H

#include "object.h"
#include "oidset.h"

enum fsck_msg_type {
	/* for internal use only */
	FSCK_IGNORE,
	FSCK_INFO,
	FSCK_FATAL,
	/* "public", fed to e.g. error_func callbacks */
	FSCK_ERROR,
	FSCK_WARN,
};

/*
 * Documentation/fsck-msgids.txt documents these; when
 * modifying this list in any way, make sure to keep the
 * two in sync.
 */

#define FOREACH_FSCK_MSG_ID(FUNC) \
	/* fatal errors */ \
	FUNC(NUL_IN_HEADER, FATAL) \
	FUNC(UNTERMINATED_HEADER, FATAL) \
	/* errors */ \
	FUNC(BAD_DATE, ERROR) \
	FUNC(BAD_DATE_OVERFLOW, ERROR) \
	FUNC(BAD_EMAIL, ERROR) \
	FUNC(BAD_NAME, ERROR) \
	FUNC(BAD_OBJECT_SHA1, ERROR) \
	FUNC(BAD_PARENT_SHA1, ERROR) \
	FUNC(BAD_TIMEZONE, ERROR) \
	FUNC(BAD_TREE, ERROR) \
	FUNC(BAD_TREE_SHA1, ERROR) \
	FUNC(BAD_TYPE, ERROR) \
	FUNC(DUPLICATE_ENTRIES, ERROR) \
	FUNC(MISSING_AUTHOR, ERROR) \
	FUNC(MISSING_COMMITTER, ERROR) \
	FUNC(MISSING_EMAIL, ERROR) \
	FUNC(MISSING_NAME_BEFORE_EMAIL, ERROR) \
	FUNC(MISSING_OBJECT, ERROR) \
	FUNC(MISSING_SPACE_BEFORE_DATE, ERROR) \
	FUNC(MISSING_SPACE_BEFORE_EMAIL, ERROR) \
	FUNC(MISSING_TAG, ERROR) \
	FUNC(MISSING_TAG_ENTRY, ERROR) \
	FUNC(MISSING_TREE, ERROR) \
	FUNC(MISSING_TYPE, ERROR) \
	FUNC(MISSING_TYPE_ENTRY, ERROR) \
	FUNC(MULTIPLE_AUTHORS, ERROR) \
	FUNC(TREE_NOT_SORTED, ERROR) \
	FUNC(UNKNOWN_TYPE, ERROR) \
	FUNC(ZERO_PADDED_DATE, ERROR) \
	FUNC(GITMODULES_MISSING, ERROR) \
	FUNC(GITMODULES_BLOB, ERROR) \
	FUNC(GITMODULES_LARGE, ERROR) \
	FUNC(GITMODULES_NAME, ERROR) \
	FUNC(GITMODULES_SYMLINK, ERROR) \
	FUNC(GITMODULES_URL, ERROR) \
	FUNC(GITMODULES_PATH, ERROR) \
	FUNC(GITMODULES_UPDATE, ERROR) \
	FUNC(GITATTRIBUTES_MISSING, ERROR) \
	FUNC(GITATTRIBUTES_LARGE, ERROR) \
	FUNC(GITATTRIBUTES_LINE_LENGTH, ERROR) \
	FUNC(GITATTRIBUTES_BLOB, ERROR) \
	/* warnings */ \
	FUNC(EMPTY_NAME, WARN) \
	FUNC(FULL_PATHNAME, WARN) \
	FUNC(HAS_DOT, WARN) \
	FUNC(HAS_DOTDOT, WARN) \
	FUNC(HAS_DOTGIT, WARN) \
	FUNC(NULL_SHA1, WARN) \
	FUNC(ZERO_PADDED_FILEMODE, WARN) \
	FUNC(NUL_IN_COMMIT, WARN) \
	/* infos (reported as warnings, but ignored by default) */ \
	FUNC(BAD_FILEMODE, INFO) \
	FUNC(GITMODULES_PARSE, INFO) \
	FUNC(GITIGNORE_SYMLINK, INFO) \
	FUNC(GITATTRIBUTES_SYMLINK, INFO) \
	FUNC(MAILMAP_SYMLINK, INFO) \
	FUNC(BAD_TAG_NAME, INFO) \
	FUNC(MISSING_TAGGER_ENTRY, INFO) \
	/* ignored (elevated when requested) */ \
	FUNC(EXTRA_HEADER_ENTRY, IGNORE)

#define MSG_ID(id, msg_type) FSCK_MSG_##id,
enum fsck_msg_id {
	FOREACH_FSCK_MSG_ID(MSG_ID)
	FSCK_MSG_MAX
};
#undef MSG_ID

struct fsck_options;
struct object;

void fsck_set_msg_type_from_ids(struct fsck_options *options,
				enum fsck_msg_id msg_id,
				enum fsck_msg_type msg_type);
void fsck_set_msg_type(struct fsck_options *options,
		       const char *msg_id, const char *msg_type);
void fsck_set_msg_types(struct fsck_options *options, const char *values);
int is_valid_msg_type(const char *msg_id, const char *msg_type);

/*
 * callback function for fsck_walk
 * type is the expected type of the object or OBJ_ANY
 * the return value is:
 *     0	everything OK
 *     <0	error signaled and abort
 *     >0	error signaled and do not abort
 */
typedef int (*fsck_walk_func)(struct object *obj, enum object_type object_type,
			      void *data, struct fsck_options *options);

/* callback for fsck_object, type is FSCK_ERROR or FSCK_WARN */
typedef int (*fsck_error)(struct fsck_options *o,
			  const struct object_id *oid, enum object_type object_type,
			  enum fsck_msg_type msg_type, enum fsck_msg_id msg_id,
			  const char *message);

int fsck_error_function(struct fsck_options *o,
			const struct object_id *oid, enum object_type object_type,
			enum fsck_msg_type msg_type, enum fsck_msg_id msg_id,
			const char *message);
int fsck_error_cb_print_missing_gitmodules(struct fsck_options *o,
					   const struct object_id *oid,
					   enum object_type object_type,
					   enum fsck_msg_type msg_type,
					   enum fsck_msg_id msg_id,
					   const char *message);

struct fsck_options {
	fsck_walk_func walk;
	fsck_error error_func;
	unsigned strict:1;
	enum fsck_msg_type *msg_type;
	struct oidset skiplist;
	struct oidset gitmodules_found;
	struct oidset gitmodules_done;
	struct oidset gitattributes_found;
	struct oidset gitattributes_done;
	kh_oid_map_t *object_names;
};

#define FSCK_OPTIONS_DEFAULT { \
	.skiplist = OIDSET_INIT, \
	.gitmodules_found = OIDSET_INIT, \
	.gitmodules_done = OIDSET_INIT, \
	.gitattributes_found = OIDSET_INIT, \
	.gitattributes_done = OIDSET_INIT, \
	.error_func = fsck_error_function \
}
#define FSCK_OPTIONS_STRICT { \
	.strict = 1, \
	.gitmodules_found = OIDSET_INIT, \
	.gitmodules_done = OIDSET_INIT, \
	.gitattributes_found = OIDSET_INIT, \
	.gitattributes_done = OIDSET_INIT, \
	.error_func = fsck_error_function, \
}
#define FSCK_OPTIONS_MISSING_GITMODULES { \
	.strict = 1, \
	.gitmodules_found = OIDSET_INIT, \
	.gitmodules_done = OIDSET_INIT, \
	.gitattributes_found = OIDSET_INIT, \
	.gitattributes_done = OIDSET_INIT, \
	.error_func = fsck_error_cb_print_missing_gitmodules, \
}

/* descend in all linked child objects
 * the return value is:
 *    -1	error in processing the object
 *    <0	return value of the callback, which lead to an abort
 *    >0	return value of the first signaled error >0 (in the case of no other errors)
 *    0		everything OK
 */
int fsck_walk(struct object *obj, void *data, struct fsck_options *options);

/*
 * Blob objects my pass a NULL data pointer, which indicates they are too large
 * to fit in memory. All other types must pass a real buffer.
 */
int fsck_object(struct object *obj, void *data, unsigned long size,
	struct fsck_options *options);

/*
 * Same as fsck_object(), but for when the caller doesn't have an object
 * struct.
 */
int fsck_buffer(const struct object_id *oid, enum object_type,
		void *data, unsigned long size,
		struct fsck_options *options);

/*
 * fsck a tag, and pass info about it back to the caller. This is
 * exposed fsck_object() internals for git-mktag(1).
 */
int fsck_tag_standalone(const struct object_id *oid, const char *buffer,
			unsigned long size, struct fsck_options *options,
			struct object_id *tagged_oid,
			int *tag_type);

/*
 * Some fsck checks are context-dependent, and may end up queued; run this
 * after completing all fsck_object() calls in order to resolve any remaining
 * checks.
 */
int fsck_finish(struct fsck_options *options);

/*
 * Subsystem for storing human-readable names for each object.
 *
 * If fsck_enable_object_names() has not been called, all other functions are
 * noops.
 *
 * Use fsck_put_object_name() to seed initial names (e.g. from refnames); the
 * fsck code will extend that while walking trees, etc.
 *
 * Use fsck_get_object_name() to get a single name (or NULL if none). Or the
 * more convenient describe_object(), which always produces an output string
 * with the oid combined with the name (if any). Note that the return value
 * points to a rotating array of static buffers, and may be invalidated by a
 * subsequent call.
 */
void fsck_enable_object_names(struct fsck_options *options);
const char *fsck_get_object_name(struct fsck_options *options,
				 const struct object_id *oid);
__attribute__((format (printf,3,4)))
void fsck_put_object_name(struct fsck_options *options,
			  const struct object_id *oid,
			  const char *fmt, ...);
const char *fsck_describe_object(struct fsck_options *options,
				 const struct object_id *oid);

struct key_value_info;
/*
 * git_config() callback for use by fsck-y tools that want to support
 * fsck.<msg> fsck.skipList etc.
 */
int git_fsck_config(const char *var, const char *value,
		    const struct config_context *ctx, void *cb);

#endif
