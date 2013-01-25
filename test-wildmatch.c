#ifdef USE_WILDMATCH
#undef USE_WILDMATCH  /* We need real fnmatch implementation here */
#endif
#include "cache.h"
#include "wildmatch.h"

static int perf(int ac, char **av)
{
	struct timeval tv1, tv2;
	struct stat st;
	int fd, i, n, flags1 = 0, flags2 = 0;
	char *buffer, *p;
	uint32_t usec1, usec2;
	const char *lang;
	const char *file = av[0];
	const char *pattern = av[1];

	lang = getenv("LANG");
	if (lang && strcmp(lang, "C"))
		die("Please test it on C locale.");

	if ((fd = open(file, O_RDONLY)) == -1 || fstat(fd, &st))
		die_errno("file open");

	buffer = xmalloc(st.st_size + 2);
	if (read(fd, buffer, st.st_size) != st.st_size)
		die_errno("read");

	buffer[st.st_size] = '\0';
	buffer[st.st_size + 1] = '\0';
	for (i = 0; i < st.st_size; i++)
		if (buffer[i] == '\n')
			buffer[i] = '\0';

	n = atoi(av[2]);
	if (av[3] && !strcmp(av[3], "pathname")) {
		flags1 = WM_PATHNAME;
		flags2 = FNM_PATHNAME;
	}

	gettimeofday(&tv1, NULL);
	for (i = 0; i < n; i++) {
		for (p = buffer; *p; p += strlen(p) + 1)
			wildmatch(pattern, p, flags1, NULL);
	}
	gettimeofday(&tv2, NULL);

	usec1 = (uint32_t)tv2.tv_sec * 1000000 + tv2.tv_usec;
	usec1 -= (uint32_t)tv1.tv_sec * 1000000 + tv1.tv_usec;
	printf("wildmatch %ds %dus\n",
	       (int)(usec1 / 1000000),
	       (int)(usec1 % 1000000));

	gettimeofday(&tv1, NULL);
	for (i = 0; i < n; i++) {
		for (p = buffer; *p; p += strlen(p) + 1)
			fnmatch(pattern, p, flags2);
	}
	gettimeofday(&tv2, NULL);

	usec2 = (uint32_t)tv2.tv_sec * 1000000 + tv2.tv_usec;
	usec2 -= (uint32_t)tv1.tv_sec * 1000000 + tv1.tv_usec;
	if (usec2 > usec1)
		printf("fnmatch   %ds %dus or %.2f%% slower\n",
		       (int)((usec2 - usec1) / 1000000),
		       (int)((usec2 - usec1) % 1000000),
		       (float)(usec2 - usec1) / usec1 * 100);
	else
		printf("fnmatch   %ds %dus or %.2f%% faster\n",
		       (int)((usec1 - usec2) / 1000000),
		       (int)((usec1 - usec2) % 1000000),
		       (float)(usec1 - usec2) / usec1 * 100);
	return 0;
}

int main(int argc, char **argv)
{
	int i;

	if (!strcmp(argv[1], "perf"))
		return perf(argc - 2, argv + 2);

	for (i = 2; i < argc; i++) {
		if (argv[i][0] == '/')
			die("Forward slash is not allowed at the beginning of the\n"
			    "pattern because Windows does not like it. Use `XXX/' instead.");
		else if (!strncmp(argv[i], "XXX/", 4))
			argv[i] += 3;
	}
	if (!strcmp(argv[1], "wildmatch"))
		return !!wildmatch(argv[3], argv[2], WM_PATHNAME, NULL);
	else if (!strcmp(argv[1], "iwildmatch"))
		return !!wildmatch(argv[3], argv[2], WM_PATHNAME | WM_CASEFOLD, NULL);
	else if (!strcmp(argv[1], "pathmatch"))
		return !!wildmatch(argv[3], argv[2], 0, NULL);
	else if (!strcmp(argv[1], "fnmatch"))
		return !!fnmatch(argv[3], argv[2], FNM_PATHNAME);
	else
		return 1;
}
