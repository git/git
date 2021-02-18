#ifndef CHUNK_FORMAT_H
#define CHUNK_FORMAT_H

#include "git-compat-util.h"

struct hashfile;
struct chunkfile;

#define CHUNK_TOC_ENTRY_SIZE (sizeof(uint32_t) + sizeof(uint64_t))

struct chunkfile *init_chunkfile(struct hashfile *f);
void free_chunkfile(struct chunkfile *cf);
int get_num_chunks(struct chunkfile *cf);
typedef int (*chunk_write_fn)(struct hashfile *f, void *data);
void add_chunk(struct chunkfile *cf,
	       uint32_t id,
	       size_t size,
	       chunk_write_fn fn);
int write_chunkfile(struct chunkfile *cf, void *data);

#endif
