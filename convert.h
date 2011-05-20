/*
 * Copyright (c) 2011, Google Inc.
 */
#ifndef CONVERT_H
#define CONVERT_H

enum safe_crlf {
	SAFE_CRLF_FALSE = 0,
	SAFE_CRLF_FAIL = 1,
	SAFE_CRLF_WARN = 2
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

/* returns 1 if *dst was used */
extern int convert_to_git(const char *path, const char *src, size_t len,
			  struct strbuf *dst, enum safe_crlf checksafe);
extern int convert_to_working_tree(const char *path, const char *src,
				   size_t len, struct strbuf *dst);
extern int renormalize_buffer(const char *path, const char *src, size_t len,
			      struct strbuf *dst);

/*****************************************************************
 *
 * Streaming converison support
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
 */
extern int stream_filter(struct stream_filter *,
			 const char *input, size_t *isize_p,
			 char *output, size_t *osize_p);

#endif /* CONVERT_H */
