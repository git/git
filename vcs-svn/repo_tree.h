#ifndef REPO_TREE_H_
#define REPO_TREE_H_

struct strbuf;

#define REPO_MODE_DIR 0040000
#define REPO_MODE_BLB 0100644
#define REPO_MODE_EXE 0100755
#define REPO_MODE_LNK 0120000

uint32_t next_blob_mark(void);
void repo_copy(uint32_t revision, const char *src, const char *dst);
const char *repo_read_path(const char *path, uint32_t *mode_out);
void repo_delete(const char *path);

#endif
