#ifndef GIT_FSCK_H
#define GIT_FSCK_H

#define FSCK_ERROR 1
#define FSCK_WARN 2

/*
 * callback function for fsck_walk
 * type is the expected type of the object or OBJ_ANY
 * the return value is:
 *     0	everything OK
 *     <0	error signaled and abort
 *     >0	error signaled and do not abort
 */
typedef int (*fsck_walk_func)(struct object *obj, int type, void *data);

/* callback for fsck_object, type is FSCK_ERROR or FSCK_WARN */
typedef int (*fsck_error)(struct object *obj, int type, const char *err, ...);

__attribute__((format (printf, 3, 4)))
int fsck_error_function(struct object *obj, int type, const char *fmt, ...);

/* descend in all linked child objects
 * the return value is:
 *    -1	error in processing the object
 *    <0	return value of the callback, which lead to an abort
 *    >0	return value of the first signaled error >0 (in the case of no other errors)
 *    0		everything OK
 */
int fsck_walk(struct object *obj, fsck_walk_func walk, void *data);
int fsck_object(struct object *obj, int strict, fsck_error error_func);

#endif
