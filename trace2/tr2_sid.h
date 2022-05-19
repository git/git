#ifndef TR2_SID_H
#define TR2_SID_H

/*
 * Get our session id. Compute if necessary.
 */
const char *tr2_sid_get(void);

/*
 * Get our process depth.  A top-level but process invoked from the
 * command line will have depth=0.  A child but process will have
 * depth=1 and so on.
 */
int tr2_sid_depth(void);

void tr2_sid_release(void);

#endif /* TR2_SID_H */
