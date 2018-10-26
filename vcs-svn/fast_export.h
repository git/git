#ifndef FAST_EXPORT_H
#define FAST_EXPORT_H

struct strbuf;
struct line_buffer;

void fast_export_init(int fd);
void fast_export_deinit(void);

void fast_export_delete(const char *path);
void fast_export_modify(const char *path, uint32_t mode, const char *dataref);
void fast_export_note(const char *committish, const char *dataref);
void fast_export_begin_note(uint32_t revision, const char *author,
		const char *log, timestamp_t timestamp, const char *note_ref);
void fast_export_begin_commit(uint32_t revision, const char *author,
			const struct strbuf *log, const char *uuid,const char *url,
			timestamp_t timestamp, const char *local_ref);
void fast_export_end_commit(uint32_t revision);
void fast_export_data(uint32_t mode, off_t len, struct line_buffer *input);
void fast_export_buf_to_data(const struct strbuf *data);
void fast_export_blob_delta(uint32_t mode,
			uint32_t old_mode, const char *old_data,
			off_t len, struct line_buffer *input);

/* If there is no such file at that rev, returns -1, errno == ENOENT. */
int fast_export_ls_rev(uint32_t rev, const char *path,
			uint32_t *mode_out, struct strbuf *dataref_out);
int fast_export_ls(const char *path,
			uint32_t *mode_out, struct strbuf *dataref_out);

void fast_export_copy(uint32_t revision, const char *src, const char *dst);
const char *fast_export_read_path(const char *path, uint32_t *mode_out);

#endif
