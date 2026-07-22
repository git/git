#ifndef REFTABLE_FSCK_H
#define REFTABLE_FSCK_H

#include "reftable-stack.h"

enum reftable_fsck_error {
	/* Invalid table name */
	REFTABLE_FSCK_ERROR_TABLE_NAME = 0,
	/* Used for bounds checking, must be last */
	REFTABLE_FSCK_MAX_VALUE,
};

/* Represents an individual error encountered during the FSCK checks. */
struct reftable_fsck_info {
	enum reftable_fsck_error error;
	const char *msg;
	const char *path;
};

typedef int reftable_fsck_report_fn(struct reftable_fsck_info *info,
				    void *cb_data);
typedef void reftable_fsck_verbose_fn(const char *msg, void *cb_data);

/*
 * Given a reftable stack, perform consistency checks on the stack.
 *
 * If an issue is encountered, the issue is reported to the callee via the
 * provided 'report_fn'. If the issue is non-recoverable the flow will not
 * continue. If it is recoverable, the flow will continue and further issues
 * will be reported as identified.
 *
 * The 'verbose_fn' will be invoked to provide verbose information about
 * the progress and state of the consistency checks.
 */
int reftable_fsck_check(struct reftable_stack *stack,
			reftable_fsck_report_fn report_fn,
			reftable_fsck_verbose_fn verbose_fn,
			void *cb_data);

#endif /* REFTABLE_FSCK_H */
