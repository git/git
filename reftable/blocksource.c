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

static void strbuf_return_block(void *b, struct reftable_block *dest)
{
	if (dest->len)
		memset(dest->data, 0xff, dest->len);
	reftable_free(dest->data);
}

static void strbuf_close(void *b)
{
}

static int strbuf_read_block(void *v, struct reftable_block *dest, uint64_t off,
			     uint32_t size)
{
	struct strbuf *b = v;
	assert(off + size <= b->len);
	dest->data = reftable_calloc(size);
	memcpy(dest->data, b->buf + off, size);
	dest->len = size;
	return size;
}

static uint64_t strbuf_size(void *b)
{
	return ((struct strbuf *)b)->len;
}

static struct reftable_block_source_vtable strbuf_vtable = {
	.size = &strbuf_size,
	.read_block = &strbuf_read_block,
	.return_block = &strbuf_return_block,
	.close = &strbuf_close,
};

void block_source_from_strbuf(struct reftable_block_source *bs,
			      struct strbuf *buf)
{
	assert(!bs->ops);
	bs->ops = &strbuf_vtable;
	bs->arg = buf;
}

static void malloc_return_block(void *b, struct reftable_block *dest)
{
	if (dest->len)
		memset(dest->data, 0xff, dest->len);
	reftable_free(dest->data);
}

static struct reftable_block_source_vtable malloc_vtable = {
	.return_block = &malloc_return_block,
};

static struct reftable_block_source malloc_block_source_instance = {
	.ops = &malloc_vtable,
};

struct reftable_block_source malloc_block_source(void)
{
	return malloc_block_source_instance;
}

struct file_block_source {
	int fd;
	uint64_t size;
};

static uint64_t file_size(void *b)
{
	return ((struct file_block_source *)b)->size;
}

static void file_return_block(void *b, struct reftable_block *dest)
{
	if (dest->len)
		memset(dest->data, 0xff, dest->len);
	reftable_free(dest->data);
}

static void file_close(void *b)
{
	int fd = ((struct file_block_source *)b)->fd;
	if (fd > 0) {
		close(fd);
		((struct file_block_source *)b)->fd = 0;
	}

	reftable_free(b);
}

static int file_read_block(void *v, struct reftable_block *dest, uint64_t off,
			   uint32_t size)
{
	struct file_block_source *b = v;
	assert(off + size <= b->size);
	dest->data = reftable_malloc(size);
	if (pread(b->fd, dest->data, size, off) != size)
		return -1;
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
	struct stat st = { 0 };
	int err = 0;
	int fd = open(name, O_RDONLY);
	struct file_block_source *p = NULL;
	if (fd < 0) {
		if (errno == ENOENT) {
			return REFTABLE_NOT_EXIST_ERROR;
		}
		return -1;
	}

	err = fstat(fd, &st);
	if (err < 0) {
		close(fd);
		return REFTABLE_IO_ERROR;
	}

	p = reftable_calloc(sizeof(struct file_block_source));
	p->size = st.st_size;
	p->fd = fd;

	assert(!bs->ops);
	bs->ops = &file_vtable;
	bs->arg = p;
	return 0;
}
