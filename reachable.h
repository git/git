#ifndef REACHEABLE_H
#define REACHEABLE_H

struct progress;
struct rev_info;
struct object;
struct packed_git;

typedef void report_recent_object_fn(const struct object *, struct packed_git *,
				     off_t, time_t);

int add_unseen_recent_objects_to_traversal(struct rev_info *revs,
					   timestamp_t timestamp,
					   report_recent_object_fn cb,
					   int ignore_in_core_kept_packs);
void mark_reachable_objects(struct rev_info *revs, int mark_reflog,
			    timestamp_t mark_recent, struct progress *);

#endif
