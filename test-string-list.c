#include "cache.h"
#include "string-list.h"

void write_list(const struct string_list *list)
{
	int i;
	for (i = 0; i < list->nr; i++)
		printf("[%d]: \"%s\"\n", i, list->items[i].string);
}

int main(int argc, char **argv)
{
	if (argc == 5 && !strcmp(argv[1], "split")) {
		struct string_list list = STRING_LIST_INIT_DUP;
		int i;
		const char *s = argv[2];
		int delim = *argv[3];
		int maxsplit = atoi(argv[4]);

		i = string_list_split(&list, s, delim, maxsplit);
		printf("%d\n", i);
		write_list(&list);
		string_list_clear(&list, 0);
		return 0;
	}

	if (argc == 5 && !strcmp(argv[1], "split_in_place")) {
		struct string_list list = STRING_LIST_INIT_NODUP;
		int i;
		char *s = xstrdup(argv[2]);
		int delim = *argv[3];
		int maxsplit = atoi(argv[4]);

		i = string_list_split_in_place(&list, s, delim, maxsplit);
		printf("%d\n", i);
		write_list(&list);
		string_list_clear(&list, 0);
		free(s);
		return 0;
	}

	fprintf(stderr, "%s: unknown function name: %s\n", argv[0],
		argv[1] ? argv[1] : "(there was none)");
	return 1;
}
