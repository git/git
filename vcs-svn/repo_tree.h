#ifndef REPO_TREE_H_
#define REPO_TREE_H_

#include "git-compat-util.h"

#define REPO_MODE_DIR 0040000
#define REPO_MODE_BLB 0100644
#define REPO_MODE_EXE 0100755
#define REPO_MODE_LNK 0120000

#define REPO_MAX_PATH_LEN 4096
#define REPO_MAX_PATH_DEPTH 1000

uint32_t next_blob_mark(void);
void repo_copy(uint32_t revision, const uint32_t *src, const uint32_t *dst);
void repo_add(uint32_t *path, uint32_t mode, uint32_t blob_mark);
const char *repo_read_path(const uint32_t *path);
uint32_t repo_read_mode(const uint32_t *path);
void repo_delete(uint32_t *path);
void repo_commit(uint32_t revision, const char *author,
		char *log, const char *uuid, const char *url,
		long unsigned timestamp);
void repo_diff(uint32_t r1, uint32_t r2);
void repo_init(void);
void repo_reset(void);

#endif
