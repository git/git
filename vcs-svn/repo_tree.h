#ifndef REPO_TREE_H_
#define REPO_TREE_H_

void svn_repo_copy(uint32_t revision, const char *src, const char *dst);
const char *svn_repo_read_path(const char *path, uint32_t *mode_out);

#endif
