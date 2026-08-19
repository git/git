#ifndef ODB_TRANSACTION_H
#define ODB_TRANSACTION_H

#include "gettext.h"
#include "odb.h"

/*
 * A transaction may be started for an object database prior to writing new
 * objects via odb_transaction_begin(). These objects are not committed until
 * odb_transaction_commit() is invoked. Only a single transaction may be pending
 * at a time.
 *
 * Each ODB source is expected to implement its own transaction handling.
 */
struct odb_transaction {
	/* The ODB source the transaction is opened against. */
	struct odb_source *source;

	/*
	 * The ODB source specific callback invoked to commit a transaction.
	 * Returns 0 on success, a negative error code otherwise.
	 */
	int (*commit)(struct odb_transaction *transaction);

	/*
	 * Optional ODB source specific callback invoked when the transaction
	 * needs to perform any deferred cleanup after objects have been
	 * committed. Returns 0 on success, a negative error code otherwise.
	 */
	int (*finalize)(struct odb_transaction *transaction);

	/*
	 * This callback is expected to write the given object stream into
	 * the ODB transaction. Note that for now, only blobs support streaming.
	 *
	 * The resulting object ID shall be written into the out pointer. The
	 * callback is expected to return 0 on success, a negative error code
	 * otherwise.
	 */
	int (*write_object_stream)(struct odb_transaction *transaction,
				   struct odb_write_stream *stream, size_t len,
				   struct object_id *oid);

	/*
	 * This callback is expected to populate the provided strvec with the
	 * environment variables that a child process should inherit so that its
	 * object writes participate in the transaction. Returns 0 on success, a
	 * negative error code otherwise.
	 */
	int (*env)(struct odb_transaction *transaction, struct strvec *env);
};

/* Flags used to configure an ODB transaction. */
enum odb_transaction_flags {
	/* Configures the transaction for use with git-receive-pack(1). */
	ODB_TRANSACTION_RECEIVE = (1 << 0),
};

/*
 * Starts an ODB transaction and returns it via `out`. Subsequent objects are
 * written to the transaction and not committed until odb_transaction_commit()
 * is invoked on the transaction. Returns 0 on success and a negative value on
 * error. Note that it is considered an error to start a new transaction if the
 * ODB already has an inflight transaction pending.
 */
int odb_transaction_begin(struct object_database *odb,
			  struct odb_transaction **out,
			  enum odb_transaction_flags flags);

static inline void odb_transaction_begin_or_die(struct object_database *odb,
						struct odb_transaction **out,
						enum odb_transaction_flags flags)
{
	if (odb_transaction_begin(odb, out, flags))
		die(_("failed to start ODB transaction"));
}

/*
 * Commits an ODB transaction making the written objects visible. Returns 0 on
 * success, a negative error code otherwise. Note that, if the specified
 * transaction is NULL, the function is a no-op and no error is returned.
 */
int odb_transaction_commit(struct odb_transaction *transaction);

/*
 * Finalizes an ODB transaction, performing any deferred cleanup and freeing it.
 * Must be called for every successfully started transaction. Note that, if the
 * specified transaction is NULL, the function is a no-op. Returns 0 on success,
 * a negative error code otherwise.
 */
int odb_transaction_finalize(struct odb_transaction *transaction);

static inline void odb_transaction_commit_and_finalize_or_die(struct odb_transaction *transaction)
{
	if (odb_transaction_commit(transaction))
		die(_("failed to commit ODB transaction"));
	if (odb_transaction_finalize(transaction))
		die(_("failed to finalize ODB transaction"));
}

/*
 * Writes the object in the provided stream into the transaction. The resulting
 * object ID is written into the out pointer. Returns 0 on success, a negative
 * error code otherwise.
 */
int odb_transaction_write_object_stream(struct odb_transaction *transaction,
					struct odb_write_stream *stream,
					size_t len, struct object_id *oid);

/*
 * Populates the provided strvec with the environment variables that a child
 * process should inherit so that its object writes participate in the
 * transaction, suitable for using via child_process.env. Returns 0 on success,
 * a negative error code otherwise. Note that, if the specified transaction is
 * NULL, the function is a no-op and no error is returned.
 */
int odb_transaction_env(struct odb_transaction *transaction, struct strvec *env);

#endif
