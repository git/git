#ifndef RESET_H
#define RESET_H

#include "hash.h"
#include "repository.h"

#define GIT_REFLOG_ACTION_ENVIRONMENT "GIT_REFLOG_ACTION"

#define RESET_HEAD_DETACH (1<<0)
#define RESET_HEAD_HARD (1<<1)
#define RESET_HEAD_RUN_POST_CHECKOUT_HOOK (1<<2)
#define RESET_HEAD_REFS_ONLY (1<<3)
#define RESET_ORIG_HEAD (1<<4)

int reset_head(struct repository *r, struct object_id *oid, const char *action,
	       const char *switch_to_branch, unsigned flags,
	       const char *reflog_orig_head, const char *reflog_head,
	       const char *default_reflog_action);

#endif
