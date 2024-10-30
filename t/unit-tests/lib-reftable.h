#ifndef LIB_REFTABLE_H
#define LIB_REFTABLE_H

#include "git-compat-util.h"
#include "reftable/reftable-writer.h"

struct reftable_buf;

void t_reftable_set_hash(uint8_t *p, int i, uint32_t id);

struct reftable_writer *t_reftable_strbuf_writer(struct reftable_buf *buf,
						 struct reftable_write_options *opts);

void t_reftable_write_to_buf(struct reftable_buf *buf,
			     struct reftable_ref_record *refs,
			     size_t nrecords,
			     struct reftable_log_record *logs,
			     size_t nlogs,
			     struct reftable_write_options *opts);

#endif
