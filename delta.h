#ifndef DELTA_H
#define DELTA_H

/* opaque object for delta index */
struct delta_index;

/*
 * create_delta_index: compute index data from given buffer
 *
 * This returns a pointer to a struct delta_index that should be passed to
 * subsequent create_delta() calls, or to free_delta_index().  A NULL pointer
 * is returned on failure.  The given buffer must not be freed or altered
 * before free_delta_index() is called.  The returned pointer must be freed
 * using free_delta_index().
 */
struct delta_index *
create_delta_index(const void *buf, unsigned long bufsize);

/*
 * free_delta_index: free the index created by create_delta_index()
 *
 * Given pointer must be what create_delta_index() returned, or NULL.
 */
void free_delta_index(struct delta_index *index);

/*
 * sizeof_delta_index: returns memory usage of delta index
 *
 * Given pointer must be what create_delta_index() returned, or NULL.
 */
unsigned long sizeof_delta_index(struct delta_index *index);

/*
 * create_delta: create a delta from given index for the given buffer
 *
 * This function may be called multiple times with different buffers using
 * the same delta_index pointer.  If max_delta_size is non-zero and the
 * resulting delta is to be larger than max_delta_size then NULL is returned.
 * On success, a non-NULL pointer to the buffer with the delta data is
 * returned and *delta_size is updated with its size.  The returned buffer
 * must be freed by the caller.
 */
void *
create_delta(const struct delta_index *index,
	     const void *buf, unsigned long bufsize,
	     unsigned long *delta_size, unsigned long max_delta_size);

/*
 * diff_delta: create a delta from source buffer to target buffer
 *
 * If max_delta_size is non-zero and the resulting delta is to be larger
 * than max_delta_size then NULL is returned.  On success, a non-NULL
 * pointer to the buffer with the delta data is returned and *delta_size is
 * updated with its size.  The returned buffer must be freed by the caller.
 */
static inline void *
diff_delta(const void *src_buf, unsigned long src_bufsize,
	   const void *trg_buf, unsigned long trg_bufsize,
	   unsigned long *delta_size, unsigned long max_delta_size)
{
	struct delta_index *index = create_delta_index(src_buf, src_bufsize);
	if (index) {
		void *delta = create_delta(index, trg_buf, trg_bufsize,
					   delta_size, max_delta_size);
		free_delta_index(index);
		return delta;
	}
	return NULL;
}

/*
 * patch_delta: recreate target buffer given source buffer and delta data
 *
 * On success, a non-NULL pointer to the target buffer is returned and
 * *trg_bufsize is updated with its size.  On failure a NULL pointer is
 * returned.  The returned buffer must be freed by the caller.
 */
void *patch_delta(const void *src_buf, unsigned long src_size,
		  const void *delta_buf, unsigned long delta_size,
		  unsigned long *dst_size);

/* the smallest possible delta size is 4 bytes */
#define DELTA_SIZE_MIN	4

/*
 * This must be called twice on the delta data buffer, first to get the
 * expected source buffer size, and again to get the target buffer size.
 */
static inline unsigned long get_delta_hdr_size(const unsigned char **datap,
					       const unsigned char *top)
{
	const unsigned char *data = *datap;
	size_t cmd, size = 0;
	int i = 0;
	do {
		cmd = *data++;
		size |= st_left_shift(cmd & 0x7f, i);
		i += 7;
	} while (cmd & 0x80 && data < top);
	*datap = data;
	return cast_size_t_to_ulong(size);
}

#endif
