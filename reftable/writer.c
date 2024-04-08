/*
Copyright 2020 Google LLC

Use of this source code is governed by a BSD-style
license that can be found in the LICENSE file or at
https://developers.google.com/open-source/licenses/bsd
*/

#include "writer.h"

#include "system.h"

#include "block.h"
#include "constants.h"
#include "record.h"
#include "tree.h"
#include "reftable-error.h"

/* finishes a block, and writes it to storage */
static int writer_flush_block(struct reftable_writer *w);

/* deallocates memory related to the index */
static void writer_clear_index(struct reftable_writer *w);

/* finishes writing a 'r' (refs) or 'g' (reflogs) section */
static int writer_finish_public_section(struct reftable_writer *w);

static struct reftable_block_stats *
writer_reftable_block_stats(struct reftable_writer *w, uint8_t typ)
{
	switch (typ) {
	case 'r':
		return &w->stats.ref_stats;
	case 'o':
		return &w->stats.obj_stats;
	case 'i':
		return &w->stats.idx_stats;
	case 'g':
		return &w->stats.log_stats;
	}
	abort();
	return NULL;
}

/* write data, queuing the padding for the next write. Returns negative for
 * error. */
static int padded_write(struct reftable_writer *w, uint8_t *data, size_t len,
			int padding)
{
	int n = 0;
	if (w->pending_padding > 0) {
		uint8_t *zeroed = reftable_calloc(w->pending_padding, sizeof(*zeroed));
		int n = w->write(w->write_arg, zeroed, w->pending_padding);
		if (n < 0)
			return n;

		w->pending_padding = 0;
		reftable_free(zeroed);
	}

	w->pending_padding = padding;
	n = w->write(w->write_arg, data, len);
	if (n < 0)
		return n;
	n += padding;
	return 0;
}

static void options_set_defaults(struct reftable_write_options *opts)
{
	if (opts->restart_interval == 0) {
		opts->restart_interval = 16;
	}

	if (opts->hash_id == 0) {
		opts->hash_id = GIT_SHA1_FORMAT_ID;
	}
	if (opts->block_size == 0) {
		opts->block_size = DEFAULT_BLOCK_SIZE;
	}
}

static int writer_version(struct reftable_writer *w)
{
	return (w->opts.hash_id == 0 || w->opts.hash_id == GIT_SHA1_FORMAT_ID) ?
			     1 :
			     2;
}

static int writer_write_header(struct reftable_writer *w, uint8_t *dest)
{
	memcpy(dest, "REFT", 4);

	dest[4] = writer_version(w);

	put_be24(dest + 5, w->opts.block_size);
	put_be64(dest + 8, w->min_update_index);
	put_be64(dest + 16, w->max_update_index);
	if (writer_version(w) == 2) {
		put_be32(dest + 24, w->opts.hash_id);
	}
	return header_size(writer_version(w));
}

static void writer_reinit_block_writer(struct reftable_writer *w, uint8_t typ)
{
	int block_start = 0;
	if (w->next == 0) {
		block_start = header_size(writer_version(w));
	}

	strbuf_reset(&w->last_key);
	block_writer_init(&w->block_writer_data, typ, w->block,
			  w->opts.block_size, block_start,
			  hash_size(w->opts.hash_id));
	w->block_writer = &w->block_writer_data;
	w->block_writer->restart_interval = w->opts.restart_interval;
}

static struct strbuf reftable_empty_strbuf = STRBUF_INIT;

struct reftable_writer *
reftable_new_writer(ssize_t (*writer_func)(void *, const void *, size_t),
		    int (*flush_func)(void *),
		    void *writer_arg, struct reftable_write_options *opts)
{
	struct reftable_writer *wp = reftable_calloc(1, sizeof(*wp));
	strbuf_init(&wp->block_writer_data.last_key, 0);
	options_set_defaults(opts);
	if (opts->block_size >= (1 << 24)) {
		/* TODO - error return? */
		abort();
	}
	wp->last_key = reftable_empty_strbuf;
	REFTABLE_CALLOC_ARRAY(wp->block, opts->block_size);
	wp->write = writer_func;
	wp->write_arg = writer_arg;
	wp->opts = *opts;
	wp->flush = flush_func;
	writer_reinit_block_writer(wp, BLOCK_TYPE_REF);

	return wp;
}

void reftable_writer_set_limits(struct reftable_writer *w, uint64_t min,
				uint64_t max)
{
	w->min_update_index = min;
	w->max_update_index = max;
}

static void writer_release(struct reftable_writer *w)
{
	if (w) {
		reftable_free(w->block);
		w->block = NULL;
		block_writer_release(&w->block_writer_data);
		w->block_writer = NULL;
		writer_clear_index(w);
		strbuf_release(&w->last_key);
	}
}

void reftable_writer_free(struct reftable_writer *w)
{
	writer_release(w);
	reftable_free(w);
}

struct obj_index_tree_node {
	struct strbuf hash;
	uint64_t *offsets;
	size_t offset_len;
	size_t offset_cap;
};

#define OBJ_INDEX_TREE_NODE_INIT    \
	{                           \
		.hash = STRBUF_INIT \
	}

static int obj_index_tree_node_compare(const void *a, const void *b)
{
	return strbuf_cmp(&((const struct obj_index_tree_node *)a)->hash,
			  &((const struct obj_index_tree_node *)b)->hash);
}

static void writer_index_hash(struct reftable_writer *w, struct strbuf *hash)
{
	uint64_t off = w->next;

	struct obj_index_tree_node want = { .hash = *hash };

	struct tree_node *node = tree_search(&want, &w->obj_index_tree,
					     &obj_index_tree_node_compare, 0);
	struct obj_index_tree_node *key = NULL;
	if (!node) {
		struct obj_index_tree_node empty = OBJ_INDEX_TREE_NODE_INIT;
		key = reftable_malloc(sizeof(struct obj_index_tree_node));
		*key = empty;

		strbuf_reset(&key->hash);
		strbuf_addbuf(&key->hash, hash);
		tree_search((void *)key, &w->obj_index_tree,
			    &obj_index_tree_node_compare, 1);
	} else {
		key = node->key;
	}

	if (key->offset_len > 0 && key->offsets[key->offset_len - 1] == off) {
		return;
	}

	REFTABLE_ALLOC_GROW(key->offsets, key->offset_len + 1, key->offset_cap);
	key->offsets[key->offset_len++] = off;
}

static int writer_add_record(struct reftable_writer *w,
			     struct reftable_record *rec)
{
	struct strbuf key = STRBUF_INIT;
	int err;

	reftable_record_key(rec, &key);
	if (strbuf_cmp(&w->last_key, &key) >= 0) {
		err = REFTABLE_API_ERROR;
		goto done;
	}

	strbuf_reset(&w->last_key);
	strbuf_addbuf(&w->last_key, &key);
	if (!w->block_writer)
		writer_reinit_block_writer(w, reftable_record_type(rec));

	if (block_writer_type(w->block_writer) != reftable_record_type(rec))
		BUG("record of type %d added to writer of type %d",
		    reftable_record_type(rec), block_writer_type(w->block_writer));

	/*
	 * Try to add the record to the writer. If this succeeds then we're
	 * done. Otherwise the block writer may have hit the block size limit
	 * and needs to be flushed.
	 */
	if (!block_writer_add(w->block_writer, rec)) {
		err = 0;
		goto done;
	}

	/*
	 * The current block is full, so we need to flush and reinitialize the
	 * writer to start writing the next block.
	 */
	err = writer_flush_block(w);
	if (err < 0)
		goto done;
	writer_reinit_block_writer(w, reftable_record_type(rec));

	/*
	 * Try to add the record to the writer again. If this still fails then
	 * the record does not fit into the block size.
	 *
	 * TODO: it would be great to have `block_writer_add()` return proper
	 *       error codes so that we don't have to second-guess the failure
	 *       mode here.
	 */
	err = block_writer_add(w->block_writer, rec);
	if (err) {
		err = REFTABLE_ENTRY_TOO_BIG_ERROR;
		goto done;
	}

done:
	strbuf_release(&key);
	return err;
}

int reftable_writer_add_ref(struct reftable_writer *w,
			    struct reftable_ref_record *ref)
{
	struct reftable_record rec = {
		.type = BLOCK_TYPE_REF,
		.u = {
			.ref = *ref
		},
	};
	int err = 0;

	if (!ref->refname)
		return REFTABLE_API_ERROR;
	if (ref->update_index < w->min_update_index ||
	    ref->update_index > w->max_update_index)
		return REFTABLE_API_ERROR;

	rec.u.ref.update_index -= w->min_update_index;

	err = writer_add_record(w, &rec);
	if (err < 0)
		return err;

	if (!w->opts.skip_index_objects && reftable_ref_record_val1(ref)) {
		struct strbuf h = STRBUF_INIT;
		strbuf_add(&h, (char *)reftable_ref_record_val1(ref),
			   hash_size(w->opts.hash_id));
		writer_index_hash(w, &h);
		strbuf_release(&h);
	}

	if (!w->opts.skip_index_objects && reftable_ref_record_val2(ref)) {
		struct strbuf h = STRBUF_INIT;
		strbuf_add(&h, reftable_ref_record_val2(ref),
			   hash_size(w->opts.hash_id));
		writer_index_hash(w, &h);
		strbuf_release(&h);
	}
	return 0;
}

int reftable_writer_add_refs(struct reftable_writer *w,
			     struct reftable_ref_record *refs, int n)
{
	int err = 0;
	int i = 0;
	QSORT(refs, n, reftable_ref_record_compare_name);
	for (i = 0; err == 0 && i < n; i++) {
		err = reftable_writer_add_ref(w, &refs[i]);
	}
	return err;
}

static int reftable_writer_add_log_verbatim(struct reftable_writer *w,
					    struct reftable_log_record *log)
{
	struct reftable_record rec = {
		.type = BLOCK_TYPE_LOG,
		.u = {
			.log = *log,
		},
	};
	if (w->block_writer &&
	    block_writer_type(w->block_writer) == BLOCK_TYPE_REF) {
		int err = writer_finish_public_section(w);
		if (err < 0)
			return err;
	}

	w->next -= w->pending_padding;
	w->pending_padding = 0;
	return writer_add_record(w, &rec);
}

int reftable_writer_add_log(struct reftable_writer *w,
			    struct reftable_log_record *log)
{
	char *input_log_message = NULL;
	struct strbuf cleaned_message = STRBUF_INIT;
	int err = 0;

	if (log->value_type == REFTABLE_LOG_DELETION)
		return reftable_writer_add_log_verbatim(w, log);

	if (!log->refname)
		return REFTABLE_API_ERROR;

	input_log_message = log->value.update.message;
	if (!w->opts.exact_log_message && log->value.update.message) {
		strbuf_addstr(&cleaned_message, log->value.update.message);
		while (cleaned_message.len &&
		       cleaned_message.buf[cleaned_message.len - 1] == '\n')
			strbuf_setlen(&cleaned_message,
				      cleaned_message.len - 1);
		if (strchr(cleaned_message.buf, '\n')) {
			/* multiple lines not allowed. */
			err = REFTABLE_API_ERROR;
			goto done;
		}
		strbuf_addstr(&cleaned_message, "\n");
		log->value.update.message = cleaned_message.buf;
	}

	err = reftable_writer_add_log_verbatim(w, log);
	log->value.update.message = input_log_message;
done:
	strbuf_release(&cleaned_message);
	return err;
}

int reftable_writer_add_logs(struct reftable_writer *w,
			     struct reftable_log_record *logs, int n)
{
	int err = 0;
	int i = 0;
	QSORT(logs, n, reftable_log_record_compare_key);

	for (i = 0; err == 0 && i < n; i++) {
		err = reftable_writer_add_log(w, &logs[i]);
	}
	return err;
}

static int writer_finish_section(struct reftable_writer *w)
{
	struct reftable_block_stats *bstats = NULL;
	uint8_t typ = block_writer_type(w->block_writer);
	uint64_t index_start = 0;
	int max_level = 0;
	size_t threshold = w->opts.unpadded ? 1 : 3;
	int before_blocks = w->stats.idx_stats.blocks;
	int err;

	err = writer_flush_block(w);
	if (err < 0)
		return err;

	/*
	 * When the section we are about to index has a lot of blocks then the
	 * index itself may span across multiple blocks, as well. This would
	 * require a linear scan over index blocks only to find the desired
	 * indexed block, which is inefficient. Instead, we write a multi-level
	 * index where index records of level N+1 will refer to index blocks of
	 * level N. This isn't constant time, either, but at least logarithmic.
	 *
	 * This loop handles writing this multi-level index. Note that we write
	 * the lowest-level index pointing to the indexed blocks first. We then
	 * continue writing additional index levels until the current level has
	 * less blocks than the threshold so that the highest level will be at
	 * the end of the index section.
	 *
	 * Readers are thus required to start reading the index section from
	 * its end, which is why we set `index_start` to the beginning of the
	 * last index section.
	 */
	while (w->index_len > threshold) {
		struct reftable_index_record *idx = NULL;
		size_t i, idx_len;

		max_level++;
		index_start = w->next;
		writer_reinit_block_writer(w, BLOCK_TYPE_INDEX);

		idx = w->index;
		idx_len = w->index_len;

		w->index = NULL;
		w->index_len = 0;
		w->index_cap = 0;
		for (i = 0; i < idx_len; i++) {
			struct reftable_record rec = {
				.type = BLOCK_TYPE_INDEX,
				.u = {
					.idx = idx[i],
				},
			};

			err = writer_add_record(w, &rec);
			if (err < 0)
				return err;
		}

		err = writer_flush_block(w);
		if (err < 0)
			return err;

		for (i = 0; i < idx_len; i++)
			strbuf_release(&idx[i].last_key);
		reftable_free(idx);
	}

	/*
	 * The index may still contain a number of index blocks lower than the
	 * threshold. Clear it so that these entries don't leak into the next
	 * index section.
	 */
	writer_clear_index(w);

	bstats = writer_reftable_block_stats(w, typ);
	bstats->index_blocks = w->stats.idx_stats.blocks - before_blocks;
	bstats->index_offset = index_start;
	bstats->max_index_level = max_level;

	/* Reinit lastKey, as the next section can start with any key. */
	strbuf_reset(&w->last_key);

	return 0;
}

struct common_prefix_arg {
	struct strbuf *last;
	int max;
};

static void update_common(void *void_arg, void *key)
{
	struct common_prefix_arg *arg = void_arg;
	struct obj_index_tree_node *entry = key;
	if (arg->last) {
		int n = common_prefix_size(&entry->hash, arg->last);
		if (n > arg->max) {
			arg->max = n;
		}
	}
	arg->last = &entry->hash;
}

struct write_record_arg {
	struct reftable_writer *w;
	int err;
};

static void write_object_record(void *void_arg, void *key)
{
	struct write_record_arg *arg = void_arg;
	struct obj_index_tree_node *entry = key;
	struct reftable_record
		rec = { .type = BLOCK_TYPE_OBJ,
			.u.obj = {
				.hash_prefix = (uint8_t *)entry->hash.buf,
				.hash_prefix_len = arg->w->stats.object_id_len,
				.offsets = entry->offsets,
				.offset_len = entry->offset_len,
			} };
	if (arg->err < 0)
		goto done;

	arg->err = block_writer_add(arg->w->block_writer, &rec);
	if (arg->err == 0)
		goto done;

	arg->err = writer_flush_block(arg->w);
	if (arg->err < 0)
		goto done;

	writer_reinit_block_writer(arg->w, BLOCK_TYPE_OBJ);
	arg->err = block_writer_add(arg->w->block_writer, &rec);
	if (arg->err == 0)
		goto done;

	rec.u.obj.offset_len = 0;
	arg->err = block_writer_add(arg->w->block_writer, &rec);

	/* Should be able to write into a fresh block. */
	assert(arg->err == 0);

done:;
}

static void object_record_free(void *void_arg, void *key)
{
	struct obj_index_tree_node *entry = key;

	FREE_AND_NULL(entry->offsets);
	strbuf_release(&entry->hash);
	reftable_free(entry);
}

static int writer_dump_object_index(struct reftable_writer *w)
{
	struct write_record_arg closure = { .w = w };
	struct common_prefix_arg common = {
		.max = 1,		/* obj_id_len should be >= 2. */
	};
	if (w->obj_index_tree) {
		infix_walk(w->obj_index_tree, &update_common, &common);
	}
	w->stats.object_id_len = common.max + 1;

	writer_reinit_block_writer(w, BLOCK_TYPE_OBJ);

	if (w->obj_index_tree) {
		infix_walk(w->obj_index_tree, &write_object_record, &closure);
	}

	if (closure.err < 0)
		return closure.err;
	return writer_finish_section(w);
}

static int writer_finish_public_section(struct reftable_writer *w)
{
	uint8_t typ = 0;
	int err = 0;

	if (!w->block_writer)
		return 0;

	typ = block_writer_type(w->block_writer);
	err = writer_finish_section(w);
	if (err < 0)
		return err;
	if (typ == BLOCK_TYPE_REF && !w->opts.skip_index_objects &&
	    w->stats.ref_stats.index_blocks > 0) {
		err = writer_dump_object_index(w);
		if (err < 0)
			return err;
	}

	if (w->obj_index_tree) {
		infix_walk(w->obj_index_tree, &object_record_free, NULL);
		tree_free(w->obj_index_tree);
		w->obj_index_tree = NULL;
	}

	w->block_writer = NULL;
	return 0;
}

int reftable_writer_close(struct reftable_writer *w)
{
	uint8_t footer[72];
	uint8_t *p = footer;
	int err = writer_finish_public_section(w);
	int empty_table = w->next == 0;
	if (err != 0)
		goto done;
	w->pending_padding = 0;
	if (empty_table) {
		/* Empty tables need a header anyway. */
		uint8_t header[28];
		int n = writer_write_header(w, header);
		err = padded_write(w, header, n, 0);
		if (err < 0)
			goto done;
	}

	p += writer_write_header(w, footer);
	put_be64(p, w->stats.ref_stats.index_offset);
	p += 8;
	put_be64(p, (w->stats.obj_stats.offset) << 5 | w->stats.object_id_len);
	p += 8;
	put_be64(p, w->stats.obj_stats.index_offset);
	p += 8;

	put_be64(p, w->stats.log_stats.offset);
	p += 8;
	put_be64(p, w->stats.log_stats.index_offset);
	p += 8;

	put_be32(p, crc32(0, footer, p - footer));
	p += 4;

	err = w->flush(w->write_arg);
	if (err < 0) {
		err = REFTABLE_IO_ERROR;
		goto done;
	}

	err = padded_write(w, footer, footer_size(writer_version(w)), 0);
	if (err < 0)
		goto done;

	if (empty_table) {
		err = REFTABLE_EMPTY_TABLE_ERROR;
		goto done;
	}

done:
	writer_release(w);
	return err;
}

static void writer_clear_index(struct reftable_writer *w)
{
	for (size_t i = 0; w->index && i < w->index_len; i++)
		strbuf_release(&w->index[i].last_key);
	FREE_AND_NULL(w->index);
	w->index_len = 0;
	w->index_cap = 0;
}

static int writer_flush_nonempty_block(struct reftable_writer *w)
{
	struct reftable_index_record index_record = {
		.last_key = STRBUF_INIT,
	};
	uint8_t typ = block_writer_type(w->block_writer);
	struct reftable_block_stats *bstats;
	int raw_bytes, padding = 0, err;
	uint64_t block_typ_off;

	/*
	 * Finish the current block. This will cause the block writer to emit
	 * restart points and potentially compress records in case we are
	 * writing a log block.
	 *
	 * Note that this is still happening in memory.
	 */
	raw_bytes = block_writer_finish(w->block_writer);
	if (raw_bytes < 0)
		return raw_bytes;

	/*
	 * By default, all records except for log records are padded to the
	 * block size.
	 */
	if (!w->opts.unpadded && typ != BLOCK_TYPE_LOG)
		padding = w->opts.block_size - raw_bytes;

	bstats = writer_reftable_block_stats(w, typ);
	block_typ_off = (bstats->blocks == 0) ? w->next : 0;
	if (block_typ_off > 0)
		bstats->offset = block_typ_off;
	bstats->entries += w->block_writer->entries;
	bstats->restarts += w->block_writer->restart_len;
	bstats->blocks++;
	w->stats.blocks++;

	/*
	 * If this is the first block we're writing to the table then we need
	 * to also write the reftable header.
	 */
	if (!w->next)
		writer_write_header(w, w->block);

	err = padded_write(w, w->block, raw_bytes, padding);
	if (err < 0)
		return err;

	/*
	 * Add an index record for every block that we're writing. If we end up
	 * having more than a threshold of index records we will end up writing
	 * an index section in `writer_finish_section()`. Each index record
	 * contains the last record key of the block it is indexing as well as
	 * the offset of that block.
	 *
	 * Note that this also applies when flushing index blocks, in which
	 * case we will end up with a multi-level index.
	 */
	REFTABLE_ALLOC_GROW(w->index, w->index_len + 1, w->index_cap);
	index_record.offset = w->next;
	strbuf_reset(&index_record.last_key);
	strbuf_addbuf(&index_record.last_key, &w->block_writer->last_key);
	w->index[w->index_len] = index_record;
	w->index_len++;

	w->next += padding + raw_bytes;
	w->block_writer = NULL;

	return 0;
}

static int writer_flush_block(struct reftable_writer *w)
{
	if (!w->block_writer)
		return 0;
	if (w->block_writer->entries == 0)
		return 0;
	return writer_flush_nonempty_block(w);
}

const struct reftable_stats *reftable_writer_stats(struct reftable_writer *w)
{
	return &w->stats;
}
