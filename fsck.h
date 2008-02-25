#ifndef GIT_FSCK_H
#define GIT_FSCK_H

/*
 * callback function for fsck_walk
 * type is the expected type of the object or OBJ_ANY
 * the return value is:
 *     0	everything OK
 *     <0	error signaled and abort
 *     >0	error signaled and do not abort
 */
typedef int (*fsck_walk_func)(struct object *obj, int type, void *data);

/* descend in all linked child objects
 * the return value is:
 *    -1	error in processing the object
 *    <0	return value of the callback, which lead to an abort
 *    >0	return value of the first sigaled error >0 (in the case of no other errors)
 *    0		everything OK
 */
int fsck_walk(struct object *obj, fsck_walk_func walk, void *data);

#endif
