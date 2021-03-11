#ifndef GIT_FSCK_H
#define GIT_FSCK_H

#include "oidset.h"

#define FSCK_ERROR 1
#define FSCK_WARN 2
#define FSCK_IGNORE 3

struct fsck_options;
struct object;

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
typedef int (*fsck_walk_func)(struct object *obj, int type, void *data, struct fsck_options *options);

/* callback for fsck_object, type is FSCK_ERROR or FSCK_WARN */
typedef int (*fsck_error)(struct fsck_options *o,
			  const struct object_id *oid, enum object_type object_type,
			  int msg_type, const char *message);

int fsck_error_function(struct fsck_options *o,
			const struct object_id *oid, enum object_type object_type,
			int msg_type, const char *message);

struct fsck_options {
	fsck_walk_func walk;
	fsck_error error_func;
	unsigned strict:1;
	int *msg_type;
	struct oidset skiplist;
	kh_oid_map_t *object_names;
};

#define FSCK_OPTIONS_DEFAULT { NULL, fsck_error_function, 0, NULL, OIDSET_INIT }
#define FSCK_OPTIONS_STRICT { NULL, fsck_error_function, 1, NULL, OIDSET_INIT }

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

void register_found_gitmodules(const struct object_id *oid);

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

/*
 * git_config() callback for use by fsck-y tools that want to support
 * fsck.<msg> fsck.skipList etc.
 */
int fsck_config_internal(const char *var, const char *value, void *cb,
			 struct fsck_options *options);

#endif
