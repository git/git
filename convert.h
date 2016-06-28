/*
 * Copyright (c) 2011, Google Inc.
 */
#ifndef CONVERT_H
#define CONVERT_H

enum safe_crlf {
	SAFE_CRLF_FALSE = 0,
	SAFE_CRLF_FAIL = 1,
	SAFE_CRLF_WARN = 2,
	SAFE_CRLF_RENORMALIZE = 3
};

extern enum safe_crlf safe_crlf;

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

extern enum eol core_eol;
extern const char *get_cached_convert_stats_ascii(const char *path);
extern const char *get_wt_convert_stats_ascii(const char *path);
extern const char *get_convert_attr_ascii(const char *path);

/* returns 1 if *dst was used */
extern int convert_to_git(const char *path, const char *src, size_t len,
			  struct strbuf *dst, enum safe_crlf checksafe);
extern int convert_to_working_tree(const char *path, const char *src,
				   size_t len, struct strbuf *dst);
extern int renormalize_buffer(const char *path, const char *src, size_t len,
			      struct strbuf *dst);
static inline int would_convert_to_git(const char *path)
{
	return convert_to_git(path, NULL, 0, NULL, 0);
}
/* Precondition: would_convert_to_git_filter_fd(path) == true */
extern void convert_to_git_filter_fd(const char *path, int fd,
				     struct strbuf *dst,
				     enum safe_crlf checksafe);
extern int would_convert_to_git_filter_fd(const char *path);

/*****************************************************************
 *
 * Streaming conversion support
 *
 *****************************************************************/

struct stream_filter; /* opaque */

extern struct stream_filter *get_stream_filter(const char *path, const unsigned char *);
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
