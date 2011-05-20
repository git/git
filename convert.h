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
extern int can_bypass_conversion(const char *path);
#endif /* CONVERT_H */
