extern void *diff_delta(void *from_buf, unsigned long from_size,
			void *to_buf, unsigned long to_size,
		        unsigned long *delta_size);
extern void *patch_delta(void *src_buf, unsigned long src_size,
			 void *delta_buf, unsigned long delta_size,
			 unsigned long *dst_size);
