#include "test-tool.h"
#include "cache.h"
#include "mergesort.h"

static uint32_t minstd_rand(uint32_t *state)
{
	*state = (uint64_t)*state * 48271 % 2147483647;
	return *state;
}

struct line {
	char *text;
	struct line *next;
};

DEFINE_LIST_SORT(static, sort_lines, struct line, next);

static int compare_strings(const struct line *x, const struct line *y)
{
	return strcmp(x->text, y->text);
}

static int sort_stdin(void)
{
	struct line *lines;
	struct line **tail = &lines;
	struct strbuf sb = STRBUF_INIT;
	struct mem_pool lines_pool;
	char *p;

	strbuf_read(&sb, 0, 0);

	/*
	 * Split by newline, but don't create an item
	 * for the empty string after the last separator.
	 */
	if (sb.len && sb.buf[sb.len - 1] == '\n')
		strbuf_setlen(&sb, sb.len - 1);

	mem_pool_init(&lines_pool, 0);
	p = sb.buf;
	for (;;) {
		char *eol = strchr(p, '\n');
		struct line *line = mem_pool_alloc(&lines_pool, sizeof(*line));
		line->text = p;
		*tail = line;
		tail = &line->next;
		if (!eol)
			break;
		*eol = '\0';
		p = eol + 1;
	}
	*tail = NULL;

	sort_lines(&lines, compare_strings);

	while (lines) {
		puts(lines->text);
		lines = lines->next;
	}
	return 0;
}

static void dist_sawtooth(int *arr, int n, int m)
{
	int i;
	for (i = 0; i < n; i++)
		arr[i] = i % m;
}

static void dist_rand(int *arr, int n, int m)
{
	int i;
	uint32_t seed = 1;
	for (i = 0; i < n; i++)
		arr[i] = minstd_rand(&seed) % m;
}

static void dist_stagger(int *arr, int n, int m)
{
	int i;
	for (i = 0; i < n; i++)
		arr[i] = (i * m + i) % n;
}

static void dist_plateau(int *arr, int n, int m)
{
	int i;
	for (i = 0; i < n; i++)
		arr[i] = (i < m) ? i : m;
}

static void dist_shuffle(int *arr, int n, int m)
{
	int i, j, k;
	uint32_t seed = 1;
	for (i = j = 0, k = 1; i < n; i++)
		arr[i] = minstd_rand(&seed) % m ? (j += 2) : (k += 2);
}

#define DIST(name) { #name, dist_##name }

static struct dist {
	const char *name;
	void (*fn)(int *arr, int n, int m);
} dist[] = {
	DIST(sawtooth),
	DIST(rand),
	DIST(stagger),
	DIST(plateau),
	DIST(shuffle),
};

static const struct dist *get_dist_by_name(const char *name)
{
	int i;
	for (i = 0; i < ARRAY_SIZE(dist); i++) {
	       if (!strcmp(dist[i].name, name))
		       return &dist[i];
	}
	return NULL;
}

static void mode_copy(int *arr, int n)
{
	/* nothing */
}

static void mode_reverse(int *arr, int n)
{
	int i, j;
	for (i = 0, j = n - 1; i < j; i++, j--)
		SWAP(arr[i], arr[j]);
}

static void mode_reverse_1st_half(int *arr, int n)
{
	mode_reverse(arr, n / 2);
}

static void mode_reverse_2nd_half(int *arr, int n)
{
	int half = n / 2;
	mode_reverse(arr + half, n - half);
}

static int compare_ints(const void *av, const void *bv)
{
	const int *ap = av, *bp = bv;
	int a = *ap, b = *bp;
	return (a > b) - (a < b);
}

static void mode_sort(int *arr, int n)
{
	QSORT(arr, n, compare_ints);
}

static void mode_dither(int *arr, int n)
{
	int i;
	for (i = 0; i < n; i++)
		arr[i] += i % 5;
}

static void unriffle(int *arr, int n, int *tmp)
{
	int i, j;
	COPY_ARRAY(tmp, arr, n);
	for (i = j = 0; i < n; i += 2)
		arr[j++] = tmp[i];
	for (i = 1; i < n; i += 2)
		arr[j++] = tmp[i];
}

static void unriffle_recursively(int *arr, int n, int *tmp)
{
	if (n > 1) {
		int half = n / 2;
		unriffle(arr, n, tmp);
		unriffle_recursively(arr, half, tmp);
		unriffle_recursively(arr + half, n - half, tmp);
	}
}

static void mode_unriffle(int *arr, int n)
{
	int *tmp;
	ALLOC_ARRAY(tmp, n);
	unriffle_recursively(arr, n, tmp);
	free(tmp);
}

static unsigned int prev_pow2(unsigned int n)
{
	unsigned int pow2 = 1;
	while (pow2 * 2 < n)
		pow2 *= 2;
	return pow2;
}

static void unriffle_recursively_skewed(int *arr, int n, int *tmp)
{
	if (n > 1) {
		int pow2 = prev_pow2(n);
		int rest = n - pow2;
		unriffle(arr + pow2 - rest, rest * 2, tmp);
		unriffle_recursively_skewed(arr, pow2, tmp);
		unriffle_recursively_skewed(arr + pow2, rest, tmp);
	}
}

static void mode_unriffle_skewed(int *arr, int n)
{
	int *tmp;
	ALLOC_ARRAY(tmp, n);
	unriffle_recursively_skewed(arr, n, tmp);
	free(tmp);
}

#define MODE(name) { #name, mode_##name }

static struct mode {
	const char *name;
	void (*fn)(int *arr, int n);
} mode[] = {
	MODE(copy),
	MODE(reverse),
	MODE(reverse_1st_half),
	MODE(reverse_2nd_half),
	MODE(sort),
	MODE(dither),
	MODE(unriffle),
	MODE(unriffle_skewed),
};

static const struct mode *get_mode_by_name(const char *name)
{
	int i;
	for (i = 0; i < ARRAY_SIZE(mode); i++) {
	       if (!strcmp(mode[i].name, name))
		       return &mode[i];
	}
	return NULL;
}

static int generate(int argc, const char **argv)
{
	const struct dist *dist = NULL;
	const struct mode *mode = NULL;
	int i, n, m, *arr;

	if (argc != 4)
		return 1;

	dist = get_dist_by_name(argv[0]);
	mode = get_mode_by_name(argv[1]);
	n = strtol(argv[2], NULL, 10);
	m = strtol(argv[3], NULL, 10);
	if (!dist || !mode)
		return 1;

	ALLOC_ARRAY(arr, n);
	dist->fn(arr, n, m);
	mode->fn(arr, n);
	for (i = 0; i < n; i++)
		printf("%08x\n", arr[i]);
	free(arr);
	return 0;
}

static struct stats {
	int get_next, set_next, compare;
} stats;

struct number {
	int value, rank;
	struct number *next;
};

DEFINE_LIST_SORT_DEBUG(static, sort_numbers, struct number, next,
		       stats.get_next++, stats.set_next++);

static int compare_numbers(const struct number *an, const struct number *bn)
{
	int a = an->value, b = bn->value;
	stats.compare++;
	return (a > b) - (a < b);
}

static void clear_numbers(struct number *list)
{
	while (list) {
		struct number *next = list->next;
		free(list);
		list = next;
	}
}

static int test(const struct dist *dist, const struct mode *mode, int n, int m)
{
	int *arr;
	size_t i;
	struct number *curr, *list, **tail;
	int is_sorted = 1;
	int is_stable = 1;
	const char *verdict;
	int result = -1;

	ALLOC_ARRAY(arr, n);
	dist->fn(arr, n, m);
	mode->fn(arr, n);
	for (i = 0, tail = &list; i < n; i++) {
		curr = xmalloc(sizeof(*curr));
		curr->value = arr[i];
		curr->rank = i;
		*tail = curr;
		tail = &curr->next;
	}
	*tail = NULL;

	stats.get_next = stats.set_next = stats.compare = 0;
	sort_numbers(&list, compare_numbers);

	QSORT(arr, n, compare_ints);
	for (i = 0, curr = list; i < n && curr; i++, curr = curr->next) {
		if (arr[i] != curr->value)
			is_sorted = 0;
		if (curr->next && curr->value == curr->next->value &&
		    curr->rank >= curr->next->rank)
			is_stable = 0;
	}
	if (i < n) {
		verdict = "too short";
	} else if (curr) {
		verdict = "too long";
	} else if (!is_sorted) {
		verdict = "not sorted";
	} else if (!is_stable) {
		verdict = "unstable";
	} else {
		verdict = "OK";
		result = 0;
	}

	printf("%-9s %-16s %8d %8d %8d %8d %8d %s\n",
	       dist->name, mode->name, n, m, stats.get_next, stats.set_next,
	       stats.compare, verdict);

	clear_numbers(list);
	free(arr);

	return result;
}

/*
 * A version of the qsort certification program from "Engineering a Sort
 * Function" by Bentley and McIlroy, Software—Practice and Experience,
 * Volume 23, Issue 11, 1249–1265 (November 1993).
 */
static int run_tests(int argc, const char **argv)
{
	const char *argv_default[] = { "100", "1023", "1024", "1025" };
	if (!argc)
		return run_tests(ARRAY_SIZE(argv_default), argv_default);
	printf("%-9s %-16s %8s %8s %8s %8s %8s %s\n",
	       "distribut", "mode", "n", "m", "get_next", "set_next",
	       "compare", "verdict");
	while (argc--) {
		int i, j, m, n = strtol(*argv++, NULL, 10);
		for (i = 0; i < ARRAY_SIZE(dist); i++) {
			for (j = 0; j < ARRAY_SIZE(mode); j++) {
				for (m = 1; m < 2 * n; m *= 2) {
					if (test(&dist[i], &mode[j], n, m))
						return 1;
				}
			}
		}
	}
	return 0;
}

int cmd__mergesort(int argc, const char **argv)
{
	int i;
	const char *sep;

	if (argc == 6 && !strcmp(argv[1], "generate"))
		return generate(argc - 2, argv + 2);
	if (argc == 2 && !strcmp(argv[1], "sort"))
		return sort_stdin();
	if (argc > 1 && !strcmp(argv[1], "test"))
		return run_tests(argc - 2, argv + 2);
	fprintf(stderr, "usage: test-tool mergesort generate <distribution> <mode> <n> <m>\n");
	fprintf(stderr, "   or: test-tool mergesort sort\n");
	fprintf(stderr, "   or: test-tool mergesort test [<n>...]\n");
	fprintf(stderr, "\n");
	for (i = 0, sep = "distributions: "; i < ARRAY_SIZE(dist); i++, sep = ", ")
		fprintf(stderr, "%s%s", sep, dist[i].name);
	fprintf(stderr, "\n");
	for (i = 0, sep = "modes: "; i < ARRAY_SIZE(mode); i++, sep = ", ")
		fprintf(stderr, "%s%s", sep, mode[i].name);
	fprintf(stderr, "\n");
	return 129;
}
