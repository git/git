#ifndef FAST_EXPORT_H_
#define FAST_EXPORT_H_

struct strbuf;
struct line_buffer;

void fast_export_init(int fd);
void fast_export_deinit(void);
void fast_export_reset(void);

void fast_export_delete(uint32_t depth, const uint32_t *path);
void fast_export_modify(uint32_t depth, const uint32_t *path,
			uint32_t mode, const char *dataref);
void fast_export_begin_commit(uint32_t revision, const char *author, char *log,
			const char *uuid, const char *url,
			unsigned long timestamp);
void fast_export_end_commit(uint32_t revision);
void fast_export_data(uint32_t mode, uint32_t len, struct line_buffer *input);

/* If there is no such file at that rev, returns -1, errno == ENOENT. */
int fast_export_ls_rev(uint32_t rev, uint32_t depth, const uint32_t *path,
			uint32_t *mode_out, struct strbuf *dataref_out);
int fast_export_ls(uint32_t depth, const uint32_t *path,
			uint32_t *mode_out, struct strbuf *dataref_out);

#endif
