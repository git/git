#ifndef COMPAT_DARWIN_H
#define COMPAT_DARWIN_H

int darwin_regexec_buf(const regex_t *preg, const char *buf, size_t size,
		       size_t nmatch, regmatch_t pmatch[], int eflags);
#define regexec_buf darwin_regexec_buf

#endif
