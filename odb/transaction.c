#include "git-compat-util.h"
#include "object-file.h"
#include "odb/transaction.h"

struct odb_transaction *odb_transaction_begin(struct object_database *odb)
{
	if (odb->transaction)
		return NULL;

	odb->transaction = odb_transaction_files_begin(odb->sources);

	return odb->transaction;
}

void odb_transaction_commit(struct odb_transaction *transaction)
{
	if (!transaction)
		return;

	/*
	 * Ensure the transaction ending matches the pending transaction.
	 */
	ASSERT(transaction == transaction->source->odb->transaction);

	transaction->commit(transaction);
	transaction->source->odb->transaction = NULL;
	free(transaction);
}
