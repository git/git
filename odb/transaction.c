#include "git-compat-util.h"
#include "gettext.h"
#include "odb/source.h"
#include "odb/transaction.h"

int odb_transaction_begin(struct object_database *odb,
			  struct odb_transaction **out,
			  enum odb_transaction_flags flags)
{
	int ret;

	if (odb->transaction)
		return error(_("object database transaction already pending"));

	ret = odb_source_begin_transaction(odb->sources, out, flags);
	if (!ret)
		odb->transaction = *out;

	return ret;
}

int odb_transaction_commit(struct odb_transaction *transaction)
{
	int ret;

	if (!transaction)
		return 0;

	/*
	 * Ensure the transaction ending matches the pending transaction.
	 */
	ASSERT(transaction == transaction->source->odb->transaction);

	ret = transaction->commit(transaction);
	transaction->source->odb->transaction = NULL;

	return ret;
}

int odb_transaction_finalize(struct odb_transaction *transaction)
{
	int ret = 0;

	if (!transaction)
		return 0;

	if (transaction->finalize)
		ret = transaction->finalize(transaction);

	free(transaction);

	return ret;
}

int odb_transaction_write_object_stream(struct odb_transaction *transaction,
					struct odb_stream *stream,
					struct object_id *oid)
{
	return transaction->write_object_stream(transaction, stream, oid);
}

int odb_transaction_write_pack(struct odb_transaction *transaction, int pack_fd,
			       struct strbuf *err_msg,
			       const struct odb_transaction_write_pack_opts *opts)
{
	return transaction->write_pack(transaction, pack_fd, err_msg, opts);
}

int odb_transaction_env(struct odb_transaction *transaction, struct strvec *env)
{
	if (!transaction)
		return 0;

	return transaction->env(transaction, env);
}
