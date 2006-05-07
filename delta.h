#ifndef DELTA_H
#define DELTA_H

/* handling of delta buffers */
extern void *diff_delta(const void *from_buf, unsigned long from_size,
			const void *to_buf, unsigned long to_size,
		        unsigned long *delta_size, unsigned long max_size);
extern void *patch_delta(void *src_buf, unsigned long src_size,
			 const void *delta_buf, unsigned long delta_size,
			 unsigned long *dst_size);

/* the smallest possible delta size is 4 bytes */
#define DELTA_SIZE_MIN	4

/*
 * This must be called twice on the delta data buffer, first to get the
 * expected reference buffer size, and again to get the result buffer size.
 */
static inline unsigned long get_delta_hdr_size(const unsigned char **datap,
					       const unsigned char *top)
{
	const unsigned char *data = *datap;
	unsigned char cmd;
	unsigned long size = 0;
	int i = 0;
	do {
		cmd = *data++;
		size |= (cmd & ~0x80) << i;
		i += 7;
	} while (cmd & 0x80 && data < top);
	*datap = data;
	return size;
}

#endif
