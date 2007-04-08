#include "builtin.h"
#include "cache.h"
#include "commit.h"
#include "diff.h"
#include "path-list.h"
#include "revision.h"
#include "utf8.h"

static const char shortlog_usage[] =
"git-shortlog [-n] [-s] [<commit-id>... ]";

static char *common_repo_prefix;

static int compare_by_number(const void *a1, const void *a2)
{
	const struct path_list_item *i1 = a1, *i2 = a2;
	const struct path_list *l1 = i1->util, *l2 = i2->util;

	if (l1->nr < l2->nr)
		return 1;
	else if (l1->nr == l2->nr)
		return 0;
	else
		return -1;
}

static struct path_list mailmap = {NULL, 0, 0, 0};

static int read_mailmap(const char *filename)
{
	char buffer[1024];
	FILE *f = fopen(filename, "r");

	if (f == NULL)
		return 1;
	while (fgets(buffer, sizeof(buffer), f) != NULL) {
		char *end_of_name, *left_bracket, *right_bracket;
		char *name, *email;
		int i;
		if (buffer[0] == '#') {
			static const char abbrev[] = "# repo-abbrev:";
			int abblen = sizeof(abbrev) - 1;
			int len = strlen(buffer);

			if (len && buffer[len - 1] == '\n')
				buffer[--len] = 0;
			if (!strncmp(buffer, abbrev, abblen)) {
				char *cp;

				if (common_repo_prefix)
					free(common_repo_prefix);
				common_repo_prefix = xmalloc(len);

				for (cp = buffer + abblen; isspace(*cp); cp++)
					; /* nothing */
				strcpy(common_repo_prefix, cp);
			}
			continue;
		}
		if ((left_bracket = strchr(buffer, '<')) == NULL)
			continue;
		if ((right_bracket = strchr(left_bracket + 1, '>')) == NULL)
			continue;
		if (right_bracket == left_bracket + 1)
			continue;
		for (end_of_name = left_bracket; end_of_name != buffer
				&& isspace(end_of_name[-1]); end_of_name--)
			/* keep on looking */
		if (end_of_name == buffer)
			continue;
		name = xmalloc(end_of_name - buffer + 1);
		strlcpy(name, buffer, end_of_name - buffer + 1);
		email = xmalloc(right_bracket - left_bracket);
		for (i = 0; i < right_bracket - left_bracket - 1; i++)
			email[i] = tolower(left_bracket[i + 1]);
		email[right_bracket - left_bracket - 1] = '\0';
		path_list_insert(email, &mailmap)->util = name;
	}
	fclose(f);
	return 0;
}

static int map_email(char *email, char *name, int maxlen)
{
	char *p;
	struct path_list_item *item;

	/* autocomplete common developers */
	p = strchr(email, '>');
	if (!p)
		return 0;

	*p = '\0';
	/* downcase the email address */
	for (p = email; *p; p++)
		*p = tolower(*p);
	item = path_list_lookup(email, &mailmap);
	if (item != NULL) {
		const char *realname = (const char *)item->util;
		strncpy(name, realname, maxlen);
		return 1;
	}
	return 0;
}

static void insert_author_oneline(struct path_list *list,
		const char *author, int authorlen,
		const char *oneline, int onelinelen)
{
	const char *dot3 = common_repo_prefix;
	char *buffer, *p;
	struct path_list_item *item;
	struct path_list *onelines;

	while (authorlen > 0 && isspace(author[authorlen - 1]))
		authorlen--;

	buffer = xmalloc(authorlen + 1);
	memcpy(buffer, author, authorlen);
	buffer[authorlen] = '\0';

	item = path_list_insert(buffer, list);
	if (item->util == NULL)
		item->util = xcalloc(1, sizeof(struct path_list));
	else
		free(buffer);

	if (!prefixcmp(oneline, "[PATCH")) {
		char *eob = strchr(oneline, ']');

		if (eob) {
			while (isspace(eob[1]) && eob[1] != '\n')
				eob++;
			if (eob - oneline < onelinelen) {
				onelinelen -= eob - oneline;
				oneline = eob;
			}
		}
	}

	while (onelinelen > 0 && isspace(oneline[0])) {
		oneline++;
		onelinelen--;
	}

	while (onelinelen > 0 && isspace(oneline[onelinelen - 1]))
		onelinelen--;

	buffer = xmalloc(onelinelen + 1);
	memcpy(buffer, oneline, onelinelen);
	buffer[onelinelen] = '\0';

	if (dot3) {
		int dot3len = strlen(dot3);
		if (dot3len > 5) {
			while ((p = strstr(buffer, dot3)) != NULL) {
				int taillen = strlen(p) - dot3len;
				memcpy(p, "/.../", 5);
				memmove(p + 5, p + dot3len, taillen + 1);
			}
		}
	}

	onelines = item->util;
	if (onelines->nr >= onelines->alloc) {
		onelines->alloc = alloc_nr(onelines->nr);
		onelines->items = xrealloc(onelines->items,
				onelines->alloc
				* sizeof(struct path_list_item));
	}

	onelines->items[onelines->nr].util = NULL;
	onelines->items[onelines->nr++].path = buffer;
}

static void read_from_stdin(struct path_list *list)
{
	char buffer[1024];

	while (fgets(buffer, sizeof(buffer), stdin) != NULL) {
		char *bob;
		if ((buffer[0] == 'A' || buffer[0] == 'a') &&
				!prefixcmp(buffer + 1, "uthor: ") &&
				(bob = strchr(buffer + 7, '<')) != NULL) {
			char buffer2[1024], offset = 0;

			if (map_email(bob + 1, buffer, sizeof(buffer)))
				bob = buffer + strlen(buffer);
			else {
				offset = 8;
				while (buffer + offset < bob &&
				       isspace(bob[-1]))
					bob--;
			}

			while (fgets(buffer2, sizeof(buffer2), stdin) &&
					buffer2[0] != '\n')
				; /* chomp input */
			if (fgets(buffer2, sizeof(buffer2), stdin)) {
				int l2 = strlen(buffer2);
				int i;
				for (i = 0; i < l2; i++)
					if (!isspace(buffer2[i]))
						break;
				insert_author_oneline(list,
						buffer + offset,
						bob - buffer - offset,
						buffer2 + i, l2 - i);
			}
		}
	}
}

static void get_from_rev(struct rev_info *rev, struct path_list *list)
{
	char scratch[1024];
	struct commit *commit;

	prepare_revision_walk(rev);
	while ((commit = get_revision(rev)) != NULL) {
		const char *author = NULL, *oneline, *buffer;
		int authorlen = authorlen, onelinelen;

		/* get author and oneline */
		for (buffer = commit->buffer; buffer && *buffer != '\0' &&
				*buffer != '\n'; ) {
			const char *eol = strchr(buffer, '\n');

			if (eol == NULL)
				eol = buffer + strlen(buffer);
			else
				eol++;

			if (!prefixcmp(buffer, "author ")) {
				char *bracket = strchr(buffer, '<');

				if (bracket == NULL || bracket > eol)
					die("Invalid commit buffer: %s",
					    sha1_to_hex(commit->object.sha1));

				if (map_email(bracket + 1, scratch,
							sizeof(scratch))) {
					author = scratch;
					authorlen = strlen(scratch);
				} else {
					if (bracket[-1] == ' ')
						bracket--;

					author = buffer + 7;
					authorlen = bracket - buffer - 7;
				}
			}
			buffer = eol;
		}

		if (author == NULL)
			die ("Missing author: %s",
					sha1_to_hex(commit->object.sha1));

		if (buffer == NULL || *buffer == '\0') {
			oneline = "<none>";
			onelinelen = sizeof(oneline) + 1;
		} else {
			char *eol;

			oneline = buffer + 1;
			eol = strchr(oneline, '\n');
			if (eol == NULL)
				onelinelen = strlen(oneline);
			else
				onelinelen = eol - oneline;
		}

		insert_author_oneline(list,
				author, authorlen, oneline, onelinelen);
	}

}

static int parse_uint(char const **arg, int comma)
{
	unsigned long ul;
	int ret;
	char *endp;

	ul = strtoul(*arg, &endp, 10);
	if (endp != *arg && *endp && *endp != comma)
		return -1;
	ret = (int) ul;
	if (ret != ul)
		return -1;
	*arg = endp;
	if (**arg)
		(*arg)++;
	return ret;
}

static const char wrap_arg_usage[] = "-w[<width>[,<indent1>[,<indent2>]]]";
#define DEFAULT_WRAPLEN 76
#define DEFAULT_INDENT1 6
#define DEFAULT_INDENT2 9

static void parse_wrap_args(const char *arg, int *in1, int *in2, int *wrap)
{
	arg += 2; /* skip -w */

	*wrap = parse_uint(&arg, ',');
	if (*wrap < 0)
		die(wrap_arg_usage);
	*in1 = parse_uint(&arg, ',');
	if (*in1 < 0)
		die(wrap_arg_usage);
	*in2 = parse_uint(&arg, '\0');
	if (*in2 < 0)
		die(wrap_arg_usage);

	if (!*wrap)
		*wrap = DEFAULT_WRAPLEN;
	if (!*in1)
		*in1 = DEFAULT_INDENT1;
	if (!*in2)
		*in2 = DEFAULT_INDENT2;
	if (*wrap &&
	    ((*in1 && *wrap <= *in1) ||
	     (*in2 && *wrap <= *in2)))
		die(wrap_arg_usage);
}

int cmd_shortlog(int argc, const char **argv, const char *prefix)
{
	struct rev_info rev;
	struct path_list list = { NULL, 0, 0, 1 };
	int i, j, sort_by_number = 0, summary = 0;
	int wrap_lines = 0;
	int wrap = DEFAULT_WRAPLEN;
	int in1 = DEFAULT_INDENT1;
	int in2 = DEFAULT_INDENT2;

	/* since -n is a shadowed rev argument, parse our args first */
	while (argc > 1) {
		if (!strcmp(argv[1], "-n") || !strcmp(argv[1], "--numbered"))
			sort_by_number = 1;
		else if (!strcmp(argv[1], "-s") ||
				!strcmp(argv[1], "--summary"))
			summary = 1;
		else if (!prefixcmp(argv[1], "-w")) {
			wrap_lines = 1;
			parse_wrap_args(argv[1], &in1, &in2, &wrap);
		}
		else if (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help"))
			usage(shortlog_usage);
		else
			break;
		argv++;
		argc--;
	}
	init_revisions(&rev, prefix);
	argc = setup_revisions(argc, argv, &rev, NULL);
	if (argc > 1)
		die ("unrecognized argument: %s", argv[1]);

	if (!access(".mailmap", R_OK))
		read_mailmap(".mailmap");

	if (rev.pending.nr == 0) {
		if (isatty(0))
			fprintf(stderr, "(reading log to summarize from standard input)\n");
		read_from_stdin(&list);
	}
	else
		get_from_rev(&rev, &list);

	if (sort_by_number)
		qsort(list.items, list.nr, sizeof(struct path_list_item),
			compare_by_number);

	for (i = 0; i < list.nr; i++) {
		struct path_list *onelines = list.items[i].util;

		if (summary) {
			printf("%s: %d\n", list.items[i].path, onelines->nr);
		} else {
			printf("%s (%d):\n", list.items[i].path, onelines->nr);
			for (j = onelines->nr - 1; j >= 0; j--) {
				const char *msg = onelines->items[j].path;

				if (wrap_lines) {
					int col = print_wrapped_text(msg, in1, in2, wrap);
					if (col != wrap)
						putchar('\n');
				}
				else
					printf("      %s\n", msg);
			}
			putchar('\n');
		}

		onelines->strdup_paths = 1;
		path_list_clear(onelines, 1);
		free(onelines);
		list.items[i].util = NULL;
	}

	list.strdup_paths = 1;
	path_list_clear(&list, 1);
	mailmap.strdup_paths = 1;
	path_list_clear(&mailmap, 1);

	return 0;
}

