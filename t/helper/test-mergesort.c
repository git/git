#include "test-tool.h"
#include "cache.h"
#include "mergesort.h"

struct line {
	char *text;
	struct line *next;
};

static void *get_next(const void *a)
{
	return ((const struct line *)a)->next;
}

static void set_next(void *a, void *b)
{
	((struct line *)a)->next = b;
}

static int compare_strings(const void *a, const void *b)
{
	const struct line *x = a, *y = b;
	return strcmp(x->text, y->text);
}

int cmd__mergesort(int argc, const char **argv)
{
	struct line *line, *p = NULL, *lines = NULL;
	struct strbuf sb = STRBUF_INIT;

	while (!strbuf_getline(&sb, stdin)) {
		line = xmalloc(sizeof(struct line));
		line->text = strbuf_detach(&sb, NULL);
		if (p) {
			line->next = p->next;
			p->next = line;
		} else {
			line->next = NULL;
			lines = line;
		}
		p = line;
	}

	lines = llist_mergesort(lines, get_next, set_next, compare_strings);

	while (lines) {
		puts(lines->text);
		lines = lines->next;
	}
	return 0;
}
