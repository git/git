#ifndef REFLOG_WALK_H
#define REFLOG_WALK_H

extern void init_reflog_walk(struct reflog_walk_info** info);
extern void add_reflog_for_walk(struct reflog_walk_info *info,
		struct commit *commit, const char *name);
extern void fake_reflog_parent(struct reflog_walk_info *info,
		struct commit *commit);
extern void show_reflog_message(struct reflog_walk_info *info, int, int);

#endif
