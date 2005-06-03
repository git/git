#ifndef DELTA_H
#define DELTA_H

/* handling of delta buffers */
extern void *diff_delta(void *from_buf, unsigned long from_size,
			void *to_buf, unsigned long to_size,
		        unsigned long *delta_size);
extern void *patch_delta(void *src_buf, unsigned long src_size,
			 void *delta_buf, unsigned long delta_size,
			 unsigned long *dst_size);

/* handling of delta objects */
struct delta;
struct object_list;
extern struct delta *lookup_delta(const unsigned char *sha1);
extern int parse_delta_buffer(struct delta *item, void *buffer, unsigned long size);
extern int parse_delta(struct delta *item, unsigned char sha1);
extern int process_deltas(void *src, unsigned long src_size,
			  const char *src_type, struct object_list *delta);

#endif
