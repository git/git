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
	struct object *obj, int type, const char *message);

int fsck_error_function(struct fsck_options *o,
	struct object *obj, int type, const char *message);

struct fsck_options {
	fsck_walk_func walk;
	fsck_error error_func;
	unsigned strict:1;
	int *msg_type;
	struct oidset skiplist;
	struct decoration *object_names;
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
/* If NULL is passed for data, we assume the object is local and read it. */
int fsck_object(struct object *obj, void *data, unsigned long size,
	struct fsck_options *options);

/*
 * Some fsck checks are context-dependent, and may end up queued; run this
 * after completing all fsck_object() calls in order to resolve any remaining
 * checks.
 */
int fsck_finish(struct fsck_options *options);

#endif
