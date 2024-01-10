#ifndef REFLOG_WALK_H
#define REFLOG_WALK_H

struct commit;
struct reflog_walk_info;
struct date_mode;

void init_reflog_walk(struct reflog_walk_info **info);
void reflog_walk_info_release(struct reflog_walk_info *info);
int add_reflog_for_walk(struct reflog_walk_info *info,
			struct commit *commit, const char *name);
void show_reflog_message(struct reflog_walk_info *info, int,
			 const struct date_mode *, int force_date);
void get_reflog_message(struct strbuf *sb,
			struct reflog_walk_info *reflog_info);
const char *get_reflog_ident(struct reflog_walk_info *reflog_info);
timestamp_t get_reflog_timestamp(struct reflog_walk_info *reflog_info);
void get_reflog_selector(struct strbuf *sb,
			 struct reflog_walk_info *reflog_info,
			 const struct date_mode *dmode, int force_date,
			 int shorten);

int reflog_walk_empty(struct reflog_walk_info *walk);

struct commit *next_reflog_entry(struct reflog_walk_info *reflog_info);

#endif
