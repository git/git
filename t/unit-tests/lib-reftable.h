#include "git-compat-util.h"
#include "clar/clar.h"
#include "clar-decls.h"
#include "git-compat-util.h"
#include "reftable/reftable-writer.h"
#include "strbuf.h"

struct reftable_buf;

void cl_reftable_set_hash(uint8_t *p, int i, enum reftable_hash id);

struct reftable_writer *cl_reftable_strbuf_writer(struct reftable_buf *buf,
						 struct reftable_write_options *opts);

void cl_reftable_write_to_buf(struct reftable_buf *buf,
			     struct reftable_ref_record *refs,
			     size_t nrecords,
			     struct reftable_log_record *logs,
			     size_t nlogs,
			     struct reftable_write_options *opts);
