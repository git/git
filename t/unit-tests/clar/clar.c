/*
 * Copyright (c) Vicent Marti. All rights reserved.
 *
 * This file is part of clar, distributed under the ISC license.
 * For full terms see the included COPYING file.
 */

#define _BSD_SOURCE
#define _DARWIN_C_SOURCE
#define _DEFAULT_SOURCE

#include <errno.h>
#include <setjmp.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdarg.h>
#include <wchar.h>
#include <time.h>
#include <inttypes.h>

/* required for sandboxing */
#include <sys/types.h>
#include <sys/stat.h>

#if defined(__UCLIBC__) && ! defined(__UCLIBC_HAS_WCHAR__)
	/*
	 * uClibc can optionally be built without wchar support, in which case
	 * the installed <wchar.h> is a stub that only defines the `whar_t`
	 * type but none of the functions typically declared by it.
	 */
#else
#	define CLAR_HAVE_WCHAR
#endif

#ifdef _WIN32
#	define WIN32_LEAN_AND_MEAN
#	include <windows.h>
#	include <io.h>
#	include <direct.h>

#	define _MAIN_CC __cdecl

#	ifndef stat
#		define stat(path, st) _stat(path, st)
		typedef struct _stat STAT_T;
#	else
		typedef struct stat STAT_T;
#	endif
#	ifndef mkdir
#		define mkdir(path, mode) _mkdir(path)
#	endif
#	ifndef chdir
#		define chdir(path) _chdir(path)
#	endif
#	ifndef access
#		define access(path, mode) _access(path, mode)
#	endif
#	ifndef strdup
#		define strdup(str) _strdup(str)
#	endif
#	ifndef strcasecmp
#		define strcasecmp(a,b) _stricmp(a,b)
#	endif

#	ifndef __MINGW32__
#		pragma comment(lib, "shell32")
#		ifndef strncpy
#			define strncpy(to, from, to_size) strncpy_s(to, to_size, from, _TRUNCATE)
#		endif
#		ifndef W_OK
#			define W_OK 02
#		endif
#		ifndef S_ISDIR
#			define S_ISDIR(x) ((x & _S_IFDIR) != 0)
#		endif
#		define p_snprintf(buf,sz,fmt,...) _snprintf_s(buf,sz,_TRUNCATE,fmt,__VA_ARGS__)
#	else
#		define p_snprintf snprintf
#	endif

#	define localtime_r(timer, buf) (localtime_s(buf, timer) == 0 ? buf : NULL)
#else
#	include <sys/wait.h> /* waitpid(2) */
#	include <unistd.h>
#	define _MAIN_CC
#	define p_snprintf snprintf
	typedef struct stat STAT_T;
#endif

#define MAX(x, y) (((x) > (y)) ? (x) : (y))

#include "clar.h"

static void fs_rm(const char *_source);
static void fs_copy(const char *_source, const char *dest);

#ifdef CLAR_FIXTURE_PATH
static const char *
fixture_path(const char *base, const char *fixture_name);
#endif

struct clar_error {
	const char *file;
	const char *function;
	uintmax_t line_number;
	const char *error_msg;
	char *description;

	struct clar_error *next;
};

struct clar_explicit {
	size_t suite_idx;
	const char *filter;

	struct clar_explicit *next;
};

struct clar_report {
	const char *test;
	int test_number;
	const char *suite;

	enum cl_test_status status;
	time_t start;
	double elapsed;

	struct clar_error *errors;
	struct clar_error *last_error;

	struct clar_report *next;
};

struct clar_summary {
	const char *filename;
	FILE *fp;
};

static struct {
	enum cl_test_status test_status;

	const char *active_test;
	const char *active_suite;

	int total_skipped;
	int total_errors;

	int tests_ran;
	int suites_ran;

	enum cl_output_format output_format;

	int exit_on_error;
	int verbosity;

	int write_summary;
	char *summary_filename;
	struct clar_summary *summary;

	struct clar_explicit *explicit;
	struct clar_explicit *last_explicit;

	struct clar_report *reports;
	struct clar_report *last_report;

	const char *invoke_file;
	const char *invoke_func;
	size_t invoke_line;

	void (*local_cleanup)(void *);
	void *local_cleanup_payload;

	jmp_buf trampoline;
	int trampoline_enabled;

	cl_trace_cb *pfn_trace_cb;
	void *trace_payload;

} _clar;

struct clar_func {
	const char *name;
	void (*ptr)(void);
};

struct clar_suite {
	const char *name;
	struct clar_func initialize;
	struct clar_func cleanup;
	const struct clar_func *tests;
	size_t test_count;
	int enabled;
};

/* From clar_print_*.c */
static void clar_print_init(int test_count, int suite_count);
static void clar_print_shutdown(int test_count, int suite_count, int error_count);
static void clar_print_error(int num, const struct clar_report *report, const struct clar_error *error);
static void clar_print_ontest(const char *suite_name, const char *test_name, int test_number, enum cl_test_status failed);
static void clar_print_onsuite(const char *suite_name, int suite_index);
static void clar_print_onabortv(const char *msg, va_list argp);
static void clar_print_onabort(const char *msg, ...);

/* From clar_sandbox.c */
static void clar_tempdir_init(void);
static void clar_tempdir_shutdown(void);
static int clar_sandbox_create(const char *suite_name, const char *test_name);
static int clar_sandbox_cleanup(void);

/* From summary.h */
static struct clar_summary *clar_summary_init(const char *filename);
static int clar_summary_shutdown(struct clar_summary *fp);

/* Load the declarations for the test suite */
#include "clar.suite"


#define CL_TRACE(ev)													\
	do {																\
		if (_clar.pfn_trace_cb)											\
			_clar.pfn_trace_cb(ev,										\
							   _clar.active_suite,						\
							   _clar.active_test,						\
							   _clar.trace_payload);					\
	} while (0)

static void clar_abort(const char *msg, ...)
{
	va_list argp;
	va_start(argp, msg);
	clar_print_onabortv(msg, argp);
	va_end(argp);
	exit(-1);
}

void cl_trace_register(cl_trace_cb *cb, void *payload)
{
	_clar.pfn_trace_cb = cb;
	_clar.trace_payload = payload;
}


/* Core test functions */
static void
clar_report_errors(struct clar_report *report)
{
	struct clar_error *error;
	int i = 1;

	for (error = report->errors; error; error = error->next)
		clar_print_error(i++, _clar.last_report, error);
}

static void
clar_report_all(void)
{
	struct clar_report *report;
	struct clar_error *error;
	int i = 1;

	for (report = _clar.reports; report; report = report->next) {
		if (report->status != CL_TEST_FAILURE)
			continue;

		for (error = report->errors; error; error = error->next)
			clar_print_error(i++, report, error);
	}
}

#ifdef WIN32
# define clar_time DWORD

static void clar_time_now(clar_time *out)
{
	*out = GetTickCount();
}

static double clar_time_diff(clar_time *start, clar_time *end)
{
	return ((double)*end - (double)*start) / 1000;
}
#else
# include <sys/time.h>

# define clar_time struct timeval

static void clar_time_now(clar_time *out)
{
	gettimeofday(out, NULL);
}

static double clar_time_diff(clar_time *start, clar_time *end)
{
	return ((double)end->tv_sec + (double)end->tv_usec / 1.0E6) -
	       ((double)start->tv_sec + (double)start->tv_usec / 1.0E6);
}
#endif

static void
clar_run_test(
	const struct clar_suite *suite,
	const struct clar_func *test,
	const struct clar_func *initialize,
	const struct clar_func *cleanup)
{
	clar_time start, end;

	_clar.trampoline_enabled = 1;

	CL_TRACE(CL_TRACE__TEST__BEGIN);

	clar_sandbox_create(suite->name, test->name);

	_clar.last_report->start = time(NULL);
	clar_time_now(&start);

	if (setjmp(_clar.trampoline) == 0) {
		if (initialize->ptr != NULL)
			initialize->ptr();

		CL_TRACE(CL_TRACE__TEST__RUN_BEGIN);
		test->ptr();
		CL_TRACE(CL_TRACE__TEST__RUN_END);
	}

	clar_time_now(&end);

	_clar.trampoline_enabled = 0;

	if (_clar.last_report->status == CL_TEST_NOTRUN)
		_clar.last_report->status = CL_TEST_OK;

	_clar.last_report->elapsed = clar_time_diff(&start, &end);

	if (_clar.local_cleanup != NULL)
		_clar.local_cleanup(_clar.local_cleanup_payload);

	clar__clear_invokepoint();

	if (cleanup->ptr != NULL)
		cleanup->ptr();

	clar_sandbox_cleanup();

	CL_TRACE(CL_TRACE__TEST__END);

	_clar.tests_ran++;

	/* remove any local-set cleanup methods */
	_clar.local_cleanup = NULL;
	_clar.local_cleanup_payload = NULL;

	clar_print_ontest(suite->name, test->name, _clar.tests_ran, _clar.last_report->status);
}

static void
clar_run_suite(const struct clar_suite *suite, const char *filter)
{
	const struct clar_func *test = suite->tests;
	size_t i, matchlen = 0;
	struct clar_report *report;
	int exact = 0;

	if (!suite->enabled)
		return;

	if (_clar.exit_on_error && _clar.total_errors)
		return;

	clar_print_onsuite(suite->name, ++_clar.suites_ran);

	_clar.active_suite = suite->name;
	_clar.active_test = NULL;
	CL_TRACE(CL_TRACE__SUITE_BEGIN);

	if (filter) {
		size_t suitelen = strlen(suite->name);
		matchlen = strlen(filter);
		if (matchlen <= suitelen) {
			filter = NULL;
		} else {
			filter += suitelen;
			while (*filter == ':')
				++filter;
			matchlen = strlen(filter);

			if (matchlen && filter[matchlen - 1] == '$') {
				exact = 1;
				matchlen--;
			}
		}
	}

	for (i = 0; i < suite->test_count; ++i) {
		if (filter && strncmp(test[i].name, filter, matchlen))
			continue;

		if (exact && strlen(test[i].name) != matchlen)
			continue;

		_clar.active_test = test[i].name;

		if ((report = calloc(1, sizeof(*report))) == NULL)
			clar_abort("Failed to allocate report.\n");
		report->suite = _clar.active_suite;
		report->test = _clar.active_test;
		report->test_number = _clar.tests_ran;
		report->status = CL_TEST_NOTRUN;

		if (_clar.reports == NULL)
			_clar.reports = report;

		if (_clar.last_report != NULL)
			_clar.last_report->next = report;

		_clar.last_report = report;

		clar_run_test(suite, &test[i], &suite->initialize, &suite->cleanup);

		if (_clar.exit_on_error && _clar.total_errors)
			return;
	}

	_clar.active_test = NULL;
	CL_TRACE(CL_TRACE__SUITE_END);
}

static void
clar_usage(const char *arg)
{
	printf("Usage: %s [options]\n\n", arg);
	printf("Options:\n");
	printf("  -sname        Run only the suite with `name` (can go to individual test name)\n");
	printf("  -iname        Include the suite with `name`\n");
	printf("  -xname        Exclude the suite with `name`\n");
	printf("  -v            Increase verbosity (show suite names)\n");
	printf("  -q            Decrease verbosity, inverse to -v\n");
	printf("  -Q            Quit as soon as a test fails\n");
	printf("  -t            Display results in tap format\n");
	printf("  -l            Print suite names\n");
	printf("  -r[filename]  Write summary file (to the optional filename)\n");
	exit(1);
}

static void
clar_parse_args(int argc, char **argv)
{
	int i;

	for (i = 1; i < argc; ++i) {
		char *argument = argv[i];

		if (argument[0] != '-' || argument[1] == '\0')
			clar_usage(argv[0]);

		switch (argument[1]) {
		case 's':
		case 'i':
		case 'x': { /* given suite name */
			int offset = (argument[2] == '=') ? 3 : 2, found = 0;
			char action = argument[1];
			size_t j, arglen, suitelen, cmplen;

			argument += offset;
			arglen = strlen(argument);

			if (arglen == 0) {
				if (i + 1 == argc)
					clar_usage(argv[0]);

				argument = argv[++i];
				arglen = strlen(argument);
			}

			for (j = 0; j < _clar_suite_count; ++j) {
				suitelen = strlen(_clar_suites[j].name);
				cmplen = (arglen < suitelen) ? arglen : suitelen;

				if (strncmp(argument, _clar_suites[j].name, cmplen) == 0) {
					int exact = (arglen >= suitelen);

					/* Do we have a real suite prefix separated by a
					 * trailing '::' or just a matching substring? */
					if (arglen > suitelen && (argument[suitelen] != ':'
						    || argument[suitelen + 1] != ':'))
					    continue;

					++found;

					switch (action) {
					case 's': {
						struct clar_explicit *explicit;

						if ((explicit = calloc(1, sizeof(*explicit))) == NULL)
							clar_abort("Failed to allocate explicit test.\n");

						explicit->suite_idx = j;
						explicit->filter = argument;

						if (_clar.explicit == NULL)
							_clar.explicit = explicit;

						if (_clar.last_explicit != NULL)
							_clar.last_explicit->next = explicit;

						_clar_suites[j].enabled = 1;
						_clar.last_explicit = explicit;
						break;
					}
					case 'i': _clar_suites[j].enabled = 1; break;
					case 'x': _clar_suites[j].enabled = 0; break;
					}

					if (exact)
						break;
				}
			}

			if (!found)
				clar_abort("No suite matching '%s' found.\n", argument);

			break;
		}

		case 'q':
			if (argument[2] != '\0')
				clar_usage(argv[0]);

			_clar.verbosity--;
			break;

		case 'Q':
			if (argument[2] != '\0')
				clar_usage(argv[0]);

			_clar.exit_on_error = 1;
			break;

		case 't':
			if (argument[2] != '\0')
				clar_usage(argv[0]);

			_clar.output_format = CL_OUTPUT_TAP;
			break;

		case 'l': {
			size_t j;

			if (argument[2] != '\0')
				clar_usage(argv[0]);

			printf("Test suites (use -s<name> to run just one):\n");
			for (j = 0; j < _clar_suite_count; ++j)
				printf(" %3d: %s\n", (int)j, _clar_suites[j].name);

			exit(0);
		}

		case 'v':
			if (argument[2] != '\0')
				clar_usage(argv[0]);

			_clar.verbosity++;
			break;

		case 'r':
			_clar.write_summary = 1;
			free(_clar.summary_filename);

			if (*(argument + 2)) {
				if ((_clar.summary_filename = strdup(argument + 2)) == NULL)
					clar_abort("Failed to allocate summary filename.\n");
			} else {
				_clar.summary_filename = NULL;
			}

			break;

		default:
			clar_usage(argv[0]);
		}
	}
}

void
clar_test_init(int argc, char **argv)
{
	const char *summary_env;

	if (argc > 1)
		clar_parse_args(argc, argv);

	clar_print_init((int)_clar_callback_count, (int)_clar_suite_count);

	if (!_clar.summary_filename &&
	    (summary_env = getenv("CLAR_SUMMARY")) != NULL) {
		_clar.write_summary = 1;
		if ((_clar.summary_filename = strdup(summary_env)) == NULL)
			clar_abort("Failed to allocate summary filename.\n");
	}

	if (_clar.write_summary && !_clar.summary_filename)
		if ((_clar.summary_filename = strdup("summary.xml")) == NULL)
			clar_abort("Failed to allocate summary filename.\n");

	if (_clar.write_summary)
	    _clar.summary = clar_summary_init(_clar.summary_filename);

	clar_tempdir_init();
}

int
clar_test_run(void)
{
	size_t i;
	struct clar_explicit *explicit;

	if (_clar.explicit) {
		for (explicit = _clar.explicit; explicit; explicit = explicit->next)
			clar_run_suite(&_clar_suites[explicit->suite_idx], explicit->filter);
	} else {
		for (i = 0; i < _clar_suite_count; ++i)
			clar_run_suite(&_clar_suites[i], NULL);
	}

	return _clar.total_errors;
}

void
clar_test_shutdown(void)
{
	struct clar_explicit *explicit, *explicit_next;
	struct clar_report *report, *report_next;

	clar_print_shutdown(
		_clar.tests_ran,
		(int)_clar_suite_count,
		_clar.total_errors
	);

	clar_tempdir_shutdown();

	if (_clar.write_summary && clar_summary_shutdown(_clar.summary) < 0)
		clar_abort("Failed to write the summary file '%s: %s.\n",
			   _clar.summary_filename, strerror(errno));

	for (explicit = _clar.explicit; explicit; explicit = explicit_next) {
		explicit_next = explicit->next;
		free(explicit);
	}

	for (report = _clar.reports; report; report = report_next) {
		struct clar_error *error, *error_next;

		for (error = report->errors; error; error = error_next) {
			free(error->description);
			error_next = error->next;
			free(error);
		}

		report_next = report->next;
		free(report);
	}

	free(_clar.summary_filename);
}

int
clar_test(int argc, char **argv)
{
	int errors;

	clar_test_init(argc, argv);
	errors = clar_test_run();
	clar_test_shutdown();

	return errors;
}

static void abort_test(void)
{
	if (!_clar.trampoline_enabled) {
		clar_print_onabort(
				"Fatal error: a cleanup method raised an exception.\n");
		clar_report_errors(_clar.last_report);
		exit(1);
	}

	CL_TRACE(CL_TRACE__TEST__LONGJMP);
	longjmp(_clar.trampoline, -1);
}

void clar__skip(void)
{
	_clar.last_report->status = CL_TEST_SKIP;
	_clar.total_skipped++;
	abort_test();
}

void clar__fail(
	const char *file,
	const char *function,
	size_t line,
	const char *error_msg,
	const char *description,
	int should_abort)
{
	struct clar_error *error;

	if ((error = calloc(1, sizeof(*error))) == NULL)
		clar_abort("Failed to allocate error.\n");

	if (_clar.last_report->errors == NULL)
		_clar.last_report->errors = error;

	if (_clar.last_report->last_error != NULL)
		_clar.last_report->last_error->next = error;

	_clar.last_report->last_error = error;

	error->file = _clar.invoke_file ? _clar.invoke_file : file;
	error->function = _clar.invoke_func ? _clar.invoke_func : function;
	error->line_number = _clar.invoke_line ? _clar.invoke_line : line;
	error->error_msg = error_msg;

	if (description != NULL &&
	    (error->description = strdup(description)) == NULL)
		clar_abort("Failed to allocate description.\n");

	_clar.total_errors++;
	_clar.last_report->status = CL_TEST_FAILURE;

	if (should_abort)
		abort_test();
}

void clar__assert(
	int condition,
	const char *file,
	const char *function,
	size_t line,
	const char *error_msg,
	const char *description,
	int should_abort)
{
	if (condition)
		return;

	clar__fail(file, function, line, error_msg, description, should_abort);
}

void clar__assert_equal(
	const char *file,
	const char *function,
	size_t line,
	const char *err,
	int should_abort,
	const char *fmt,
	...)
{
	va_list args;
	char buf[4096];
	int is_equal = 1;

	va_start(args, fmt);

	if (!strcmp("%s", fmt)) {
		const char *s1 = va_arg(args, const char *);
		const char *s2 = va_arg(args, const char *);
		is_equal = (!s1 || !s2) ? (s1 == s2) : !strcmp(s1, s2);

		if (!is_equal) {
			if (s1 && s2) {
				int pos;
				for (pos = 0; s1[pos] == s2[pos] && s1[pos] && s2[pos]; ++pos)
					/* find differing byte offset */;
				p_snprintf(buf, sizeof(buf), "'%s' != '%s' (at byte %d)",
					s1, s2, pos);
			} else {
				const char *q1 = s1 ? "'" : "";
				const char *q2 = s2 ? "'" : "";
				s1 = s1 ? s1 : "NULL";
				s2 = s2 ? s2 : "NULL";
				p_snprintf(buf, sizeof(buf), "%s%s%s != %s%s%s",
					   q1, s1, q1, q2, s2, q2);
			}
		}
	}
	else if(!strcmp("%.*s", fmt)) {
		const char *s1 = va_arg(args, const char *);
		const char *s2 = va_arg(args, const char *);
		int len = va_arg(args, int);
		is_equal = (!s1 || !s2) ? (s1 == s2) : !strncmp(s1, s2, len);

		if (!is_equal) {
			if (s1 && s2) {
				int pos;
				for (pos = 0; pos < len && s1[pos] == s2[pos]; ++pos)
					/* find differing byte offset */;
				p_snprintf(buf, sizeof(buf), "'%.*s' != '%.*s' (at byte %d)",
					len, s1, len, s2, pos);
			} else {
				const char *q1 = s1 ? "'" : "";
				const char *q2 = s2 ? "'" : "";
				s1 = s1 ? s1 : "NULL";
				s2 = s2 ? s2 : "NULL";
				p_snprintf(buf, sizeof(buf), "%s%.*s%s != %s%.*s%s",
					   q1, len, s1, q1, q2, len, s2, q2);
			}
		}
	}
#ifdef CLAR_HAVE_WCHAR
	else if (!strcmp("%ls", fmt)) {
		const wchar_t *wcs1 = va_arg(args, const wchar_t *);
		const wchar_t *wcs2 = va_arg(args, const wchar_t *);
		is_equal = (!wcs1 || !wcs2) ? (wcs1 == wcs2) : !wcscmp(wcs1, wcs2);

		if (!is_equal) {
			if (wcs1 && wcs2) {
				int pos;
				for (pos = 0; wcs1[pos] == wcs2[pos] && wcs1[pos] && wcs2[pos]; ++pos)
					/* find differing byte offset */;
				p_snprintf(buf, sizeof(buf), "'%ls' != '%ls' (at byte %d)",
					wcs1, wcs2, pos);
			} else {
				const char *q1 = wcs1 ? "'" : "";
				const char *q2 = wcs2 ? "'" : "";
				wcs1 = wcs1 ? wcs1 : L"NULL";
				wcs2 = wcs2 ? wcs2 : L"NULL";
				p_snprintf(buf, sizeof(buf), "%s%ls%s != %s%ls%s",
					   q1, wcs1, q1, q2, wcs2, q2);
			}
		}
	}
	else if(!strcmp("%.*ls", fmt)) {
		const wchar_t *wcs1 = va_arg(args, const wchar_t *);
		const wchar_t *wcs2 = va_arg(args, const wchar_t *);
		int len = va_arg(args, int);
		is_equal = (!wcs1 || !wcs2) ? (wcs1 == wcs2) : !wcsncmp(wcs1, wcs2, len);

		if (!is_equal) {
			if (wcs1 && wcs2) {
				int pos;
				for (pos = 0; pos < len && wcs1[pos] == wcs2[pos]; ++pos)
					/* find differing byte offset */;
				p_snprintf(buf, sizeof(buf), "'%.*ls' != '%.*ls' (at byte %d)",
					len, wcs1, len, wcs2, pos);
			} else {
				const char *q1 = wcs1 ? "'" : "";
				const char *q2 = wcs2 ? "'" : "";
				wcs1 = wcs1 ? wcs1 : L"NULL";
				wcs2 = wcs2 ? wcs2 : L"NULL";
				p_snprintf(buf, sizeof(buf), "%s%.*ls%s != %s%.*ls%s",
					   q1, len, wcs1, q1, q2, len, wcs2, q2);
			}
		}
	}
#endif /* CLAR_HAVE_WCHAR */
	else if (!strcmp("%"PRIuMAX, fmt) || !strcmp("%"PRIxMAX, fmt)) {
		uintmax_t sz1 = va_arg(args, uintmax_t), sz2 = va_arg(args, uintmax_t);
		is_equal = (sz1 == sz2);
		if (!is_equal) {
			int offset = p_snprintf(buf, sizeof(buf), fmt, sz1);
			strncat(buf, " != ", sizeof(buf) - offset);
			p_snprintf(buf + offset + 4, sizeof(buf) - offset - 4, fmt, sz2);
		}
	}
	else if (!strcmp("%p", fmt)) {
		void *p1 = va_arg(args, void *), *p2 = va_arg(args, void *);
		is_equal = (p1 == p2);
		if (!is_equal)
			p_snprintf(buf, sizeof(buf), "%p != %p", p1, p2);
	}
	else {
		int i1 = va_arg(args, int), i2 = va_arg(args, int);
		is_equal = (i1 == i2);
		if (!is_equal) {
			int offset = p_snprintf(buf, sizeof(buf), fmt, i1);
			strncat(buf, " != ", sizeof(buf) - offset);
			p_snprintf(buf + offset + 4, sizeof(buf) - offset - 4, fmt, i2);
		}
	}

	va_end(args);

	if (!is_equal)
		clar__fail(file, function, line, err, buf, should_abort);
}

void cl_set_cleanup(void (*cleanup)(void *), void *opaque)
{
	_clar.local_cleanup = cleanup;
	_clar.local_cleanup_payload = opaque;
}

void clar__set_invokepoint(
	const char *file,
	const char *func,
	size_t line)
{
	_clar.invoke_file = file;
	_clar.invoke_func = func;
	_clar.invoke_line = line;
}

void clar__clear_invokepoint(void)
{
	_clar.invoke_file = NULL;
	_clar.invoke_func = NULL;
	_clar.invoke_line = 0;
}

#include "clar/sandbox.h"
#include "clar/fixtures.h"
#include "clar/fs.h"
#include "clar/print.h"
#include "clar/summary.h"
