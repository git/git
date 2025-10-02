#include "test-tool.h"
#include "strbuf.h"
#include "string-list.h"

int cmd__string_list(int argc, const char **argv)
{
	if (argc == 2 && !strcmp(argv[1], "sort")) {
		struct string_list list = STRING_LIST_INIT_NODUP;
		struct strbuf sb = STRBUF_INIT;
		struct string_list_item *item;

		strbuf_read(&sb, 0, 0);

		/*
		 * Split by newline, but don't create a string_list item
		 * for the empty string after the last separator.
		 */
		if (sb.len && sb.buf[sb.len - 1] == '\n')
			strbuf_setlen(&sb, sb.len - 1);
		string_list_split_in_place(&list, sb.buf, "\n", -1);

		string_list_sort(&list);

		for_each_string_list_item(item, &list)
			puts(item->string);

		string_list_clear(&list, 0);
		strbuf_release(&sb);
		return 0;
	}

	fprintf(stderr, "%s: unknown function name: %s\n", argv[0],
		argv[1] ? argv[1] : "(there was none)");
	return 1;
}
