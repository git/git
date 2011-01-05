#ifndef FAST_EXPORT_H_
#define FAST_EXPORT_H_

#include "line_buffer.h"

void fast_export_init(int fd);
void fast_export_deinit(void);
void fast_export_reset(void);

void fast_export_delete(uint32_t depth, uint32_t *path);
void fast_export_modify(uint32_t depth, uint32_t *path, uint32_t mode,
			uint32_t mark);
void fast_export_begin_commit(uint32_t revision);
void fast_export_commit(uint32_t revision, uint32_t author, char *log,
			uint32_t uuid, uint32_t url, unsigned long timestamp);
void fast_export_blob(uint32_t mode, uint32_t mark, uint32_t len,
		      struct line_buffer *input);

#endif
