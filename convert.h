/*
 * Copyright (c) 2011, Google Inc.
 */
#ifndef CONVERT_H
#define CONVERT_H

#include "string-list.h"

struct index_state;

#define CONV_EOL_RNDTRP_DIE   (1<<0) /* Die if CRLF to LF to CRLF is different */
#define CONV_EOL_RNDTRP_WARN  (1<<1) /* Warn if CRLF to LF to CRLF is different */
#define CONV_EOL_RENORMALIZE  (1<<2) /* Convert CRLF to LF */
#define CONV_EOL_KEEP_CRLF    (1<<3) /* Keep CRLF line endings as is */
#define CONV_WRITE_OBJECT     (1<<4) /* Content is written to the index */

extern int global_conv_flags_eol;

enum auto_crlf {
	AUTO_CRLF_FALSE = 0,
	AUTO_CRLF_TRUE = 1,
	AUTO_CRLF_INPUT = -1
};

extern enum auto_crlf auto_crlf;

enum eol {
	EOL_UNSET,
	EOL_CRLF,
	EOL_LF,
#ifdef NATIVE_CRLF
	EOL_NATIVE = EOL_CRLF
#else
	EOL_NATIVE = EOL_LF
#endif
};

enum ce_delay_state {
	CE_NO_DELAY = 0,
	CE_CAN_DELAY = 1,
	CE_RETRY = 2
};

struct delayed_checkout {
	/*
	 * State of the currently processed cache entry. If the state is
	 * CE_CAN_DELAY, then the filter can delay the current cache entry.
	 * If the state is CE_RETRY, then this signals the filter that the
	 * cache entry was requested before.
	 */
	enum ce_delay_state state;
	/* List of filter drivers that signaled delayed blobs. */
	struct string_list filters;
	/* List of delayed blobs identified by their path. */
	struct string_list paths;
};

extern enum eol core_eol;
extern char *check_roundtrip_encoding;
extern const char *get_cached_convert_stats_ascii(const struct index_state *istate,
						  const char *path);
extern const char *get_wt_convert_stats_ascii(const char *path);
extern const char *get_convert_attr_ascii(const char *path);

/* returns 1 if *dst was used */
extern int convert_to_git(const struct index_state *istate,
			  const char *path, const char *src, size_t len,
			  struct strbuf *dst, int conv_flags);
extern int convert_to_working_tree(const char *path, const char *src,
				   size_t len, struct strbuf *dst);
extern int async_convert_to_working_tree(const char *path, const char *src,
					 size_t len, struct strbuf *dst,
					 void *dco);
extern int async_query_available_blobs(const char *cmd, struct string_list *available_paths);
extern int renormalize_buffer(const struct index_state *istate,
			      const char *path, const char *src, size_t len,
			      struct strbuf *dst);
static inline int would_convert_to_git(const struct index_state *istate,
				       const char *path)
{
	return convert_to_git(istate, path, NULL, 0, NULL, 0);
}
/* Precondition: would_convert_to_git_filter_fd(path) == true */
extern void convert_to_git_filter_fd(const struct index_state *istate,
				     const char *path, int fd,
				     struct strbuf *dst,
				     int conv_flags);
extern int would_convert_to_git_filter_fd(const char *path);

/*****************************************************************
 *
 * Streaming conversion support
 *
 *****************************************************************/

struct stream_filter; /* opaque */

extern struct stream_filter *get_stream_filter(const char *path, const struct object_id *);
extern void free_stream_filter(struct stream_filter *);
extern int is_null_stream_filter(struct stream_filter *);

/*
 * Use as much input up to *isize_p and fill output up to *osize_p;
 * update isize_p and osize_p to indicate how much buffer space was
 * consumed and filled. Return 0 on success, non-zero on error.
 *
 * Some filters may need to buffer the input and look-ahead inside it
 * to decide what to output, and they may consume more than zero bytes
 * of input and still not produce any output. After feeding all the
 * input, pass NULL as input and keep calling this function, to let
 * such filters know there is no more input coming and it is time for
 * them to produce the remaining output based on the buffered input.
 */
extern int stream_filter(struct stream_filter *,
			 const char *input, size_t *isize_p,
			 char *output, size_t *osize_p);

#endif /* CONVERT_H */
