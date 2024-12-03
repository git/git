/*
Copyright 2020 Google LLC

Use of this source code is governed by a BSD-style
license that can be found in the LICENSE file or at
https://developers.google.com/open-source/licenses/bsd
*/

#include "system.h"

#include "basics.h"
#include "blocksource.h"
#include "reftable-blocksource.h"
#include "reftable-error.h"

static void reftable_buf_return_block(void *b UNUSED, struct reftable_block *dest)
{
	if (dest->len)
		memset(dest->data, 0xff, dest->len);
	reftable_free(dest->data);
}

static void reftable_buf_close(void *b UNUSED)
{
}

static int reftable_buf_read_block(void *v, struct reftable_block *dest,
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
	.read_block = &reftable_buf_read_block,
	.return_block = &reftable_buf_return_block,
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

static void file_return_block(void *b UNUSED, struct reftable_block *dest UNUSED)
{
}

static void file_close(void *v)
{
	struct file_block_source *b = v;
	munmap(b->data, b->size);
	reftable_free(b);
}

static int file_read_block(void *v, struct reftable_block *dest, uint64_t off,
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
	.read_block = &file_read_block,
	.return_block = &file_return_block,
	.close = &file_close,
};

int reftable_block_source_from_file(struct reftable_block_source *bs,
				    const char *name)
{
	struct file_block_source *p;
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
	p->data = xmmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);

	assert(!bs->ops);
	bs->ops = &file_vtable;
	bs->arg = p;

	err = 0;

out:
	if (fd >= 0)
		close(fd);
	if (err < 0)
		reftable_free(p);
	return 0;
}
