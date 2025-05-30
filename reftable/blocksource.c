/*
 * Copyright 2020 Google LLC
 *
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file or at
 * https://developers.google.com/open-source/licenses/bsd
 */

#include "system.h"

#include "basics.h"
#include "blocksource.h"
#include "reftable-blocksource.h"
#include "reftable-error.h"

void block_source_release_data(struct reftable_block_data *data)
{
	struct reftable_block_source source = data->source;
	if (data && source.ops)
		source.ops->release_data(source.arg, data);
	data->data = NULL;
	data->len = 0;
	data->source.ops = NULL;
	data->source.arg = NULL;
}

void block_source_close(struct reftable_block_source *source)
{
	if (!source->ops) {
		return;
	}

	source->ops->close(source->arg);
	source->ops = NULL;
}

ssize_t block_source_read_data(struct reftable_block_source *source,
			       struct reftable_block_data *dest, uint64_t off,
			       uint32_t size)
{
	ssize_t result = source->ops->read_data(source->arg, dest, off, size);
	dest->source = *source;
	return result;
}

uint64_t block_source_size(struct reftable_block_source *source)
{
	return source->ops->size(source->arg);
}

static void reftable_buf_release_data(void *b REFTABLE_UNUSED, struct reftable_block_data *dest)
{
	if (dest->len)
		memset(dest->data, 0xff, dest->len);
	reftable_free(dest->data);
}

static void reftable_buf_close(void *b REFTABLE_UNUSED)
{
}

static ssize_t reftable_buf_read_data(void *v, struct reftable_block_data *dest,
				      uint64_t off, uint32_t size)
{
	struct reftable_buf *b = v;
	assert(off + size <= b->len);
	REFTABLE_CALLOC_ARRAY(dest->data, size);
	if (!dest->data)
		return -1;
	memcpy(dest->data, b->buf + off, size);
	dest->len = size;
	return size;
}

static uint64_t reftable_buf_size(void *b)
{
	return ((struct reftable_buf *)b)->len;
}

static struct reftable_block_source_vtable reftable_buf_vtable = {
	.size = &reftable_buf_size,
	.read_data = &reftable_buf_read_data,
	.release_data = &reftable_buf_release_data,
	.close = &reftable_buf_close,
};

void block_source_from_buf(struct reftable_block_source *bs,
			   struct reftable_buf *buf)
{
	assert(!bs->ops);
	bs->ops = &reftable_buf_vtable;
	bs->arg = buf;
}

struct file_block_source {
	uint64_t size;
	unsigned char *data;
};

static uint64_t file_size(void *b)
{
	return ((struct file_block_source *)b)->size;
}

static void file_release_data(void *b REFTABLE_UNUSED, struct reftable_block_data *dest REFTABLE_UNUSED)
{
}

static void file_close(void *v)
{
	struct file_block_source *b = v;
	munmap(b->data, b->size);
	reftable_free(b);
}

static ssize_t file_read_data(void *v, struct reftable_block_data *dest, uint64_t off,
			      uint32_t size)
{
	struct file_block_source *b = v;
	assert(off + size <= b->size);
	dest->data = b->data + off;
	dest->len = size;
	return size;
}

static struct reftable_block_source_vtable file_vtable = {
	.size = &file_size,
	.read_data = &file_read_data,
	.release_data = &file_release_data,
	.close = &file_close,
};

int reftable_block_source_from_file(struct reftable_block_source *bs,
				    const char *name)
{
	struct file_block_source *p = NULL;
	struct stat st;
	int fd, err;

	fd = open(name, O_RDONLY);
	if (fd < 0) {
		if (errno == ENOENT)
			return REFTABLE_NOT_EXIST_ERROR;
		err = -1;
		goto out;
	}

	if (fstat(fd, &st) < 0) {
		err = REFTABLE_IO_ERROR;
		goto out;
	}

	REFTABLE_CALLOC_ARRAY(p, 1);
	if (!p) {
		err = REFTABLE_OUT_OF_MEMORY_ERROR;
		goto out;
	}

	p->size = st.st_size;
	p->data = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
	if (p->data == MAP_FAILED) {
		err = REFTABLE_IO_ERROR;
		p->data = NULL;
		goto out;
	}

	assert(!bs->ops);
	bs->ops = &file_vtable;
	bs->arg = p;

	err = 0;

out:
	if (fd >= 0)
		close(fd);
	if (err < 0)
		reftable_free(p);
	return err;
}
