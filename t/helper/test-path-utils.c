#include "test-tool.h"
#include "cache.h"
#include "string-list.h"
#include "utf8.h"

/*
 * A "string_list_each_func_t" function that normalizes an entry from
 * GIT_CEILING_DIRECTORIES.  If the path is unusable for some reason,
 * die with an explanation.
 */
static int normalize_ceiling_entry(struct string_list_item *item, void *unused)
{
	char *ceil = item->string;

	if (!*ceil)
		die("Empty path is not supported");
	if (!is_absolute_path(ceil))
		die("Path \"%s\" is not absolute", ceil);
	if (normalize_path_copy(ceil, ceil) < 0)
		die("Path \"%s\" could not be normalized", ceil);
	return 1;
}

static void normalize_argv_string(const char **var, const char *input)
{
	if (!strcmp(input, "<null>"))
		*var = NULL;
	else if (!strcmp(input, "<empty>"))
		*var = "";
	else
		*var = input;

	if (*var && (**var == '<' || **var == '('))
		die("Bad value: %s\n", input);
}

struct test_data {
	const char *from;  /* input:  transform from this ... */
	const char *to;    /* output: ... to this.            */
	const char *alternative; /* output: ... or this.      */
};

/*
 * Compatibility wrappers for OpenBSD, whose basename(3) and dirname(3)
 * have const parameters.
 */
static char *posix_basename(char *path)
{
	return basename(path);
}

static char *posix_dirname(char *path)
{
	return dirname(path);
}

static int test_function(struct test_data *data, char *(*func)(char *input),
	const char *funcname)
{
	int failed = 0, i;
	char buffer[1024];
	char *to;

	for (i = 0; data[i].to; i++) {
		if (!data[i].from)
			to = func(NULL);
		else {
			xsnprintf(buffer, sizeof(buffer), "%s", data[i].from);
			to = func(buffer);
		}
		if (!strcmp(to, data[i].to))
			continue;
		if (!data[i].alternative)
			error("FAIL: %s(%s) => '%s' != '%s'\n",
				funcname, data[i].from, to, data[i].to);
		else if (!strcmp(to, data[i].alternative))
			continue;
		else
			error("FAIL: %s(%s) => '%s' != '%s', '%s'\n",
				funcname, data[i].from, to, data[i].to,
				data[i].alternative);
		failed = 1;
	}
	return failed;
}

static struct test_data basename_data[] = {
	/* --- POSIX type paths --- */
	{ NULL,              "."    },
	{ "",                "."    },
	{ ".",               "."    },
	{ "..",              ".."   },
	{ "/",               "/"    },
	{ "//",              "/", "//" },
	{ "///",             "/", "//" },
	{ "////",            "/", "//" },
	{ "usr",             "usr"  },
	{ "/usr",            "usr"  },
	{ "/usr/",           "usr"  },
	{ "/usr//",          "usr"  },
	{ "/usr/lib",        "lib"  },
	{ "usr/lib",         "lib"  },
	{ "usr/lib///",      "lib"  },

#if defined(__MINGW32__) || defined(_MSC_VER)
	/* --- win32 type paths --- */
	{ "\\usr",           "usr"  },
	{ "\\usr\\",         "usr"  },
	{ "\\usr\\\\",       "usr"  },
	{ "\\usr\\lib",      "lib"  },
	{ "usr\\lib",        "lib"  },
	{ "usr\\lib\\\\\\",  "lib"  },
	{ "C:/usr",          "usr"  },
	{ "C:/usr",          "usr"  },
	{ "C:/usr/",         "usr"  },
	{ "C:/usr//",        "usr"  },
	{ "C:/usr/lib",      "lib"  },
	{ "C:usr/lib",       "lib"  },
	{ "C:usr/lib///",    "lib"  },
	{ "C:",              "."    },
	{ "C:a",             "a"    },
	{ "C:/",             "/"    },
	{ "C:///",           "/"    },
	{ "\\",              "\\", "/" },
	{ "\\\\",            "\\", "/" },
	{ "\\\\\\",          "\\", "/" },
#endif
	{ NULL,              NULL   }
};

static struct test_data dirname_data[] = {
	/* --- POSIX type paths --- */
	{ NULL,              "."      },
	{ "",                "."      },
	{ ".",               "."      },
	{ "..",              "."      },
	{ "/",               "/"      },
	{ "//",              "/", "//" },
	{ "///",             "/", "//" },
	{ "////",            "/", "//" },
	{ "usr",             "."      },
	{ "/usr",            "/"      },
	{ "/usr/",           "/"      },
	{ "/usr//",          "/"      },
	{ "/usr/lib",        "/usr"   },
	{ "usr/lib",         "usr"    },
	{ "usr/lib///",      "usr"    },

#if defined(__MINGW32__) || defined(_MSC_VER)
	/* --- win32 type paths --- */
	{ "\\",              "\\"     },
	{ "\\\\",            "\\\\"   },
	{ "\\usr",           "\\"     },
	{ "\\usr\\",         "\\"     },
	{ "\\usr\\\\",       "\\"     },
	{ "\\usr\\lib",      "\\usr"  },
	{ "usr\\lib",        "usr"    },
	{ "usr\\lib\\\\\\",  "usr"    },
	{ "C:a",             "C:."    },
	{ "C:/",             "C:/"    },
	{ "C:///",           "C:/"    },
	{ "C:/usr",          "C:/"    },
	{ "C:/usr/",         "C:/"    },
	{ "C:/usr//",        "C:/"    },
	{ "C:/usr/lib",      "C:/usr" },
	{ "C:usr/lib",       "C:usr"  },
	{ "C:usr/lib///",    "C:usr"  },
	{ "\\\\\\",          "\\"     },
	{ "\\\\\\\\",        "\\"     },
	{ "C:",              "C:.", "." },
#endif
	{ NULL,              NULL     }
};

static int is_dotgitmodules(const char *path)
{
	return is_hfs_dotgitmodules(path) || is_ntfs_dotgitmodules(path);
}

static int cmp_by_st_size(const void *a, const void *b)
{
	intptr_t x = (intptr_t)((struct string_list_item *)a)->util;
	intptr_t y = (intptr_t)((struct string_list_item *)b)->util;

	return x > y ? -1 : (x < y ? +1 : 0);
}

/*
 * A very simple, reproducible pseudo-random generator. Copied from
 * `test-genrandom.c`.
 */
static uint64_t my_random_value = 1234;

static uint64_t my_random(void)
{
	my_random_value = my_random_value * 1103515245 + 12345;
	return my_random_value;
}

/*
 * A fast approximation of the square root, without requiring math.h.
 *
 * It uses Newton's method to approximate the solution of 0 = x^2 - value.
 */
static double my_sqrt(double value)
{
	const double epsilon = 1e-6;
	double x = value;

	if (value == 0)
		return 0;

	for (;;) {
		double delta = (value / x - x) / 2;
		if (delta < epsilon && delta > -epsilon)
			return x + delta;
		x += delta;
	}
}

static int protect_ntfs_hfs_benchmark(int argc, const char **argv)
{
	size_t i, j, nr, min_len = 3, max_len = 20;
	char **names;
	int repetitions = 15, file_mode = 0100644;
	uint64_t begin, end;
	double m[3][2], v[3][2];
	uint64_t cumul;
	double cumul2;

	if (argc > 1 && !strcmp(argv[1], "--with-symlink-mode")) {
		file_mode = 0120000;
		argc--;
		argv++;
	}

	nr = argc > 1 ? strtoul(argv[1], NULL, 0) : 1000000;
	ALLOC_ARRAY(names, nr);

	if (argc > 2) {
		min_len = strtoul(argv[2], NULL, 0);
		if (argc > 3)
			max_len = strtoul(argv[3], NULL, 0);
		if (min_len > max_len)
			die("min_len > max_len");
	}

	for (i = 0; i < nr; i++) {
		size_t len = min_len + (my_random() % (max_len + 1 - min_len));

		names[i] = xmallocz(len);
		while (len > 0)
			names[i][--len] = (char)(' ' + (my_random() % ('\x7f' - ' ')));
	}

	for (protect_ntfs = 0; protect_ntfs < 2; protect_ntfs++)
		for (protect_hfs = 0; protect_hfs < 2; protect_hfs++) {
			cumul = 0;
			cumul2 = 0;
			for (i = 0; i < repetitions; i++) {
				begin = getnanotime();
				for (j = 0; j < nr; j++)
					verify_path(names[j], file_mode);
				end = getnanotime();
				printf("protect_ntfs = %d, protect_hfs = %d: %lfms\n", protect_ntfs, protect_hfs, (end-begin) / (double)1e6);
				cumul += end - begin;
				cumul2 += (end - begin) * (end - begin);
			}
			m[protect_ntfs][protect_hfs] = cumul / (double)repetitions;
			v[protect_ntfs][protect_hfs] = my_sqrt(cumul2 / (double)repetitions - m[protect_ntfs][protect_hfs] * m[protect_ntfs][protect_hfs]);
			printf("mean: %lfms, stddev: %lfms\n", m[protect_ntfs][protect_hfs] / (double)1e6, v[protect_ntfs][protect_hfs] / (double)1e6);
		}

	for (protect_ntfs = 0; protect_ntfs < 2; protect_ntfs++)
		for (protect_hfs = 0; protect_hfs < 2; protect_hfs++)
			printf("ntfs=%d/hfs=%d: %lf%% slower\n", protect_ntfs, protect_hfs, (m[protect_ntfs][protect_hfs] - m[0][0]) * 100 / m[0][0]);

	return 0;
}

int cmd__path_utils(int argc, const char **argv)
{
	if (argc == 3 && !strcmp(argv[1], "normalize_path_copy")) {
		char *buf = xmallocz(strlen(argv[2]));
		int rv = normalize_path_copy(buf, argv[2]);
		if (rv)
			buf = "++failed++";
		puts(buf);
		return 0;
	}

	if (argc >= 2 && !strcmp(argv[1], "real_path")) {
		while (argc > 2) {
			puts(real_path(argv[2]));
			argc--;
			argv++;
		}
		return 0;
	}

	if (argc >= 2 && !strcmp(argv[1], "absolute_path")) {
		while (argc > 2) {
			puts(absolute_path(argv[2]));
			argc--;
			argv++;
		}
		return 0;
	}

	if (argc == 4 && !strcmp(argv[1], "longest_ancestor_length")) {
		int len;
		struct string_list ceiling_dirs = STRING_LIST_INIT_DUP;
		char *path = xstrdup(argv[2]);

		/*
		 * We have to normalize the arguments because under
		 * Windows, bash mangles arguments that look like
		 * absolute POSIX paths or colon-separate lists of
		 * absolute POSIX paths into DOS paths (e.g.,
		 * "/foo:/foo/bar" might be converted to
		 * "D:\Src\msysgit\foo;D:\Src\msysgit\foo\bar"),
		 * whereas longest_ancestor_length() requires paths
		 * that use forward slashes.
		 */
		if (normalize_path_copy(path, path))
			die("Path \"%s\" could not be normalized", argv[2]);
		string_list_split(&ceiling_dirs, argv[3], PATH_SEP, -1);
		filter_string_list(&ceiling_dirs, 0,
				   normalize_ceiling_entry, NULL);
		len = longest_ancestor_length(path, &ceiling_dirs);
		string_list_clear(&ceiling_dirs, 0);
		free(path);
		printf("%d\n", len);
		return 0;
	}

	if (argc >= 4 && !strcmp(argv[1], "prefix_path")) {
		const char *prefix = argv[2];
		int prefix_len = strlen(prefix);
		int nongit_ok;
		setup_git_directory_gently(&nongit_ok);
		while (argc > 3) {
			puts(prefix_path(prefix, prefix_len, argv[3]));
			argc--;
			argv++;
		}
		return 0;
	}

	if (argc == 4 && !strcmp(argv[1], "strip_path_suffix")) {
		char *prefix = strip_path_suffix(argv[2], argv[3]);
		printf("%s\n", prefix ? prefix : "(null)");
		return 0;
	}

	if (argc == 3 && !strcmp(argv[1], "print_path")) {
		puts(argv[2]);
		return 0;
	}

	if (argc == 4 && !strcmp(argv[1], "relative_path")) {
		struct strbuf sb = STRBUF_INIT;
		const char *in, *prefix, *rel;
		normalize_argv_string(&in, argv[2]);
		normalize_argv_string(&prefix, argv[3]);
		rel = relative_path(in, prefix, &sb);
		if (!rel)
			puts("(null)");
		else
			puts(strlen(rel) > 0 ? rel : "(empty)");
		strbuf_release(&sb);
		return 0;
	}

	if (argc == 2 && !strcmp(argv[1], "basename"))
		return test_function(basename_data, posix_basename, argv[1]);

	if (argc == 2 && !strcmp(argv[1], "dirname"))
		return test_function(dirname_data, posix_dirname, argv[1]);

	if (argc > 2 && !strcmp(argv[1], "is_dotgitmodules")) {
		int res = 0, expect = 1, i;
		for (i = 2; i < argc; i++)
			if (!strcmp("--not", argv[i]))
				expect = !expect;
			else if (expect != is_dotgitmodules(argv[i]))
				res = error("'%s' is %s.gitmodules", argv[i],
					    expect ? "not " : "");
			else
				fprintf(stderr, "ok: '%s' is %s.gitmodules\n",
					argv[i], expect ? "" : "not ");
		return !!res;
	}

	if (argc > 2 && !strcmp(argv[1], "file-size")) {
		int res = 0, i;
		struct stat st;

		for (i = 2; i < argc; i++)
			if (stat(argv[i], &st))
				res = error_errno("Cannot stat '%s'", argv[i]);
			else
				printf("%"PRIuMAX"\n", (uintmax_t)st.st_size);
		return !!res;
	}

	if (argc == 4 && !strcmp(argv[1], "skip-n-bytes")) {
		int fd = open(argv[2], O_RDONLY), offset = atoi(argv[3]);
		char buffer[65536];

		if (fd < 0)
			die_errno("could not open '%s'", argv[2]);
		if (lseek(fd, offset, SEEK_SET) < 0)
			die_errno("could not skip %d bytes", offset);
		for (;;) {
			ssize_t count = read(fd, buffer, sizeof(buffer));
			if (count < 0)
				die_errno("could not read '%s'", argv[2]);
			if (!count)
				break;
			if (write(1, buffer, count) < 0)
				die_errno("could not write to stdout");
		}
		close(fd);
		return 0;
	}

	if (argc > 5 && !strcmp(argv[1], "slice-tests")) {
		int res = 0;
		long offset, stride, i;
		struct string_list list = STRING_LIST_INIT_NODUP;
		struct stat st;

		offset = strtol(argv[2], NULL, 10);
		stride = strtol(argv[3], NULL, 10);
		if (stride < 1)
			stride = 1;
		for (i = 4; i < argc; i++)
			if (stat(argv[i], &st))
				res = error_errno("Cannot stat '%s'", argv[i]);
			else
				string_list_append(&list, argv[i])->util =
					(void *)(intptr_t)st.st_size;
		QSORT(list.items, list.nr, cmp_by_st_size);
		for (i = offset; i < list.nr; i+= stride)
			printf("%s\n", list.items[i].string);

		return !!res;
	}

	if (argc > 1 && !strcmp(argv[1], "protect_ntfs_hfs"))
		return !!protect_ntfs_hfs_benchmark(argc - 1, argv + 1);

	if (argc > 1 && !strcmp(argv[1], "is_valid_path")) {
		int res = 0, expect = 1, i;

		for (i = 2; i < argc; i++)
			if (!strcmp("--not", argv[i]))
				expect = 0;
			else if (expect != is_valid_path(argv[i]))
				res = error("'%s' is%s a valid path",
					    argv[i], expect ? " not" : "");
			else
				fprintf(stderr,
					"'%s' is%s a valid path\n",
					argv[i], expect ? "" : " not");

		return !!res;
	}

	fprintf(stderr, "%s: unknown function name: %s\n", argv[0],
		argv[1] ? argv[1] : "(there was none)");
	return 1;
}
