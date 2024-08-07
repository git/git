#ifndef REFLOG_H
#define REFLOG_H
#include "refs.h"

struct cmd_reflog_expire_cb {
	int stalefix;
	int explicit_expiry;
	timestamp_t expire_total;
	timestamp_t expire_unreachable;
	int recno;
};

struct expire_reflog_policy_cb {
	enum {
		UE_NORMAL,
		UE_ALWAYS,
		UE_HEAD
	} unreachable_expire_kind;
	struct commit_list *mark_list;
	unsigned long mark_limit;
	struct cmd_reflog_expire_cb cmd;
	struct commit *tip_commit;
	struct commit_list *tips;
	unsigned int dry_run:1;
};

int reflog_delete(const char *rev, enum expire_reflog_flags flags,
		  int verbose);
void reflog_expiry_cleanup(void *cb_data);
void reflog_expiry_prepare(const char *refname, const struct object_id *oid,
			   void *cb_data);
int should_expire_reflog_ent(struct object_id *ooid, struct object_id *noid,
			     const char *email, timestamp_t timestamp, int tz,
			     const char *message, void *cb_data);
int count_reflog_ent(struct object_id *ooid, struct object_id *noid,
		     const char *email, timestamp_t timestamp, int tz,
		     const char *message, void *cb_data);
int should_expire_reflog_ent_verbose(struct object_id *ooid,
				     struct object_id *noid,
				     const char *email,
				     timestamp_t timestamp, int tz,
				     const char *message, void *cb_data);
#endif /* REFLOG_H */
