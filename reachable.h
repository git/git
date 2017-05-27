#ifndef REACHEABLE_H
#define REACHEABLE_H

struct progress;
extern int add_unseen_recent_objects_to_traversal(struct rev_info *revs,
						  timestamp_t timestamp);
extern void mark_reachable_objects(struct rev_info *revs, int mark_reflog,
				   timestamp_t mark_recent, struct progress *);

#endif
