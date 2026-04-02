#ifndef ODB_TRANSACTION_H
#define ODB_TRANSACTION_H

#include "odb.h"
#include "odb/source.h"

/*
 * A transaction may be started for an object database prior to writing new
 * objects via odb_transaction_begin(). These objects are not committed until
 * odb_transaction_commit() is invoked. Only a single transaction may be pending
 * at a time.
 *
 * Each ODB source is expected to implement its own transaction handling.
 */
struct odb_transaction;
typedef void (*odb_transaction_commit_fn)(struct odb_transaction *transaction);
struct odb_transaction {
	/* The ODB source the transaction is opened against. */
	struct odb_source *source;

	/* The ODB source specific callback invoked to commit a transaction. */
	odb_transaction_commit_fn commit;
};

/*
 * Starts an ODB transaction. Subsequent objects are written to the transaction
 * and not committed until odb_transaction_commit() is invoked on the
 * transaction. If the ODB already has a pending transaction, NULL is returned.
 */
struct odb_transaction *odb_transaction_begin(struct object_database *odb);

/*
 * Commits an ODB transaction making the written objects visible. If the
 * specified transaction is NULL, the function is a no-op.
 */
void odb_transaction_commit(struct odb_transaction *transaction);

#endif
