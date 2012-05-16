#ifndef REFLOG_WALK_H
#define REFLOG_WALK_H

#include "cache.h"

struct reflog_walk_info;

extern void init_reflog_walk(struct reflog_walk_info** info);
extern int add_reflog_for_walk(struct reflog_walk_info *info,
		struct commit *commit, const char *name);
extern void fake_reflog_parent(struct reflog_walk_info *info,
		struct commit *commit);
extern void show_reflog_message(struct reflog_walk_info *info, int,
				enum date_mode, int force_date);
extern void get_reflog_message(struct strbuf *sb,
		struct reflog_walk_info *reflog_info);
extern const char *get_reflog_ident(struct reflog_walk_info *reflog_info);
extern void get_reflog_selector(struct strbuf *sb,
		struct reflog_walk_info *reflog_info,
		enum date_mode dmode, int force_date,
		int shorten);

#endif
