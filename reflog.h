#ifndef REFLOG_H
#define REFLOG_H
#include "refs.h"

#define REFLOG_EXPIRE_TOTAL   (1 << 0)
#define REFLOG_EXPIRE_UNREACH (1 << 1)

struct reflog_expire_entry_option {
	struct reflog_expire_entry_option *next;
	timestamp_t expire_total;
	timestamp_t expire_unreachable;
	char pattern[FLEX_ARRAY];
};

struct reflog_expire_options {
	struct reflog_expire_entry_option *entries, **entries_tail;
	int stalefix;
	int explicit_expiry;
	timestamp_t default_expire_total;
	timestamp_t expire_total;
	timestamp_t default_expire_unreachable;
	timestamp_t expire_unreachable;
	int recno;
};
#define REFLOG_EXPIRE_OPTIONS_INIT(now) { \
	.default_expire_total = now - 30 * 24 * 3600, \
	.default_expire_unreachable = now - 90 * 24 * 3600, \
}

/*
 * Parse the reflog expire configuration. This should be used with
 * `repo_config()`.
 */
int reflog_expire_config(const char *var, const char *value,
			 const struct config_context *ctx, void *cb);

/*
 * Adapt the options so that they apply to the given refname. This applies any
 * per-reference reflog expiry configuration that may exist to the options.
 */
void reflog_expire_options_set_refname(struct reflog_expire_options *cb,
				       const char *refname);

struct expire_reflog_policy_cb {
	enum {
		UE_NORMAL,
		UE_ALWAYS,
		UE_HEAD
	} unreachable_expire_kind;
	struct commit_list *mark_list;
	unsigned long mark_limit;
	struct reflog_expire_options opts;
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
