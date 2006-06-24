#ifndef GIT_COMPAT_UTIL_H
#define GIT_COMPAT_UTIL_H

#ifndef FLEX_ARRAY
#if defined(__GNUC__) && (__GNUC__ < 3)
#define FLEX_ARRAY 0
#else
#define FLEX_ARRAY /* empty */
#endif
#endif

#define ARRAY_SIZE(x) (sizeof(x)/sizeof(x[0]))

#include <unistd.h>
#include <stdio.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <sys/param.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <dirent.h>

#ifdef __GNUC__
#define NORETURN __attribute__((__noreturn__))
#else
#define NORETURN
#ifndef __attribute__
#define __attribute__(x)
#endif
#endif

/* General helper functions */
extern void usage(const char *err) NORETURN;
extern void die(const char *err, ...) NORETURN __attribute__((format (printf, 1, 2)));
extern int error(const char *err, ...) __attribute__((format (printf, 1, 2)));

extern void set_usage_routine(void (*routine)(const char *err) NORETURN);
extern void set_die_routine(void (*routine)(const char *err, va_list params) NORETURN);
extern void set_error_routine(void (*routine)(const char *err, va_list params));

#ifdef NO_MMAP

#ifndef PROT_READ
#define PROT_READ 1
#define PROT_WRITE 2
#define MAP_PRIVATE 1
#define MAP_FAILED ((void*)-1)
#endif

#define mmap gitfakemmap
#define munmap gitfakemunmap
extern void *gitfakemmap(void *start, size_t length, int prot , int flags, int fd, off_t offset);
extern int gitfakemunmap(void *start, size_t length);

#else /* NO_MMAP */

#include <sys/mman.h>

#endif /* NO_MMAP */

#ifdef NO_SETENV
#define setenv gitsetenv
extern int gitsetenv(const char *, const char *, int);
#endif

#ifdef NO_UNSETENV
#define unsetenv gitunsetenv
extern void gitunsetenv(const char *);
#endif

#ifdef NO_STRCASESTR
#define strcasestr gitstrcasestr
extern char *gitstrcasestr(const char *haystack, const char *needle);
#endif

static inline void *xmalloc(size_t size)
{
	void *ret = malloc(size);
	if (!ret && !size)
		ret = malloc(1);
	if (!ret)
		die("Out of memory, malloc failed");
	return ret;
}

static inline void *xrealloc(void *ptr, size_t size)
{
	void *ret = realloc(ptr, size);
	if (!ret && !size)
		ret = realloc(ptr, 1);
	if (!ret)
		die("Out of memory, realloc failed");
	return ret;
}

static inline void *xcalloc(size_t nmemb, size_t size)
{
	void *ret = calloc(nmemb, size);
	if (!ret && (!nmemb || !size))
		ret = calloc(1, 1);
	if (!ret)
		die("Out of memory, calloc failed");
	return ret;
}

static inline ssize_t xread(int fd, void *buf, size_t len)
{
	ssize_t nr;
	while (1) {
		nr = read(fd, buf, len);
		if ((nr < 0) && (errno == EAGAIN || errno == EINTR))
			continue;
		return nr;
	}
}

static inline ssize_t xwrite(int fd, const void *buf, size_t len)
{
	ssize_t nr;
	while (1) {
		nr = write(fd, buf, len);
		if ((nr < 0) && (errno == EAGAIN || errno == EINTR))
			continue;
		return nr;
	}
}

/* Sane ctype - no locale, and works with signed chars */
#undef isspace
#undef isdigit
#undef isalpha
#undef isalnum
#undef tolower
#undef toupper
extern unsigned char sane_ctype[256];
#define GIT_SPACE 0x01
#define GIT_DIGIT 0x02
#define GIT_ALPHA 0x04
#define sane_istest(x,mask) ((sane_ctype[(unsigned char)(x)] & (mask)) != 0)
#define isspace(x) sane_istest(x,GIT_SPACE)
#define isdigit(x) sane_istest(x,GIT_DIGIT)
#define isalpha(x) sane_istest(x,GIT_ALPHA)
#define isalnum(x) sane_istest(x,GIT_ALPHA | GIT_DIGIT)
#define tolower(x) sane_case((unsigned char)(x), 0x20)
#define toupper(x) sane_case((unsigned char)(x), 0)

static inline int sane_case(int x, int high)
{
	if (sane_istest(x, GIT_ALPHA))
		x = (x & ~0x20) | high;
	return x;
}

#ifndef MAXPATHLEN
#define MAXPATHLEN 256
#endif
#endif
