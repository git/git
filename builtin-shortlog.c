#include "builtin.h"
#include "cache.h"
#include "commit.h"
#include "diff.h"
#include "path-list.h"
#include "revision.h"
#include "utf8.h"
#include "mailmap.h"

static const char shortlog_usage[] =
"git-shortlog [-n] [-s] [-e] [<commit-id>... ]";

static char *common_repo_prefix;
static int email;

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

static void insert_one_record(struct path_list *list,
			      const char *author,
			      const char *oneline)
{
	const char *dot3 = common_repo_prefix;
	char *buffer, *p;
	struct path_list_item *item;
	struct path_list *onelines;
	char namebuf[1024];
	size_t len;
	const char *eol;
	const char *boemail, *eoemail;

	boemail = strchr(author, '<');
	if (!boemail)
		return;
	eoemail = strchr(boemail, '>');
	if (!eoemail)
		return;
	if (!map_email(&mailmap, boemail+1, namebuf, sizeof(namebuf))) {
		while (author < boemail && isspace(*author))
			author++;
		for (len = 0;
		     len < sizeof(namebuf) - 1 && author + len < boemail;
		     len++)
			namebuf[len] = author[len];
		while (0 < len && isspace(namebuf[len-1]))
			len--;
		namebuf[len] = '\0';
	}
	else
		len = strlen(namebuf);

	if (email) {
		size_t room = sizeof(namebuf) - len - 1;
		int maillen = eoemail - boemail + 1;
		snprintf(namebuf + len, room, " %.*s", maillen, boemail);
	}

	buffer = xstrdup(namebuf);
	item = path_list_insert(buffer, list);
	if (item->util == NULL)
		item->util = xcalloc(1, sizeof(struct path_list));
	else
		free(buffer);

	eol = strchr(oneline, '\n');
	if (!eol)
		eol = oneline + strlen(oneline);
	while (*oneline && isspace(*oneline) && *oneline != '\n')
		oneline++;
	if (!prefixcmp(oneline, "[PATCH")) {
		char *eob = strchr(oneline, ']');
		if (eob && (!eol || eob < eol))
			oneline = eob + 1;
	}
	while (*oneline && isspace(*oneline) && *oneline != '\n')
		oneline++;
	len = eol - oneline;
	while (len && isspace(oneline[len-1]))
		len--;
	buffer = xmemdupz(oneline, len);

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
	char author[1024], oneline[1024];

	while (fgets(author, sizeof(author), stdin) != NULL) {
		if (!(author[0] == 'A' || author[0] == 'a') ||
		    prefixcmp(author + 1, "uthor: "))
			continue;
		while (fgets(oneline, sizeof(oneline), stdin) &&
		       oneline[0] != '\n')
			; /* discard headers */
		while (fgets(oneline, sizeof(oneline), stdin) &&
		       oneline[0] == '\n')
			; /* discard blanks */
		insert_one_record(list, author + 8, oneline);
	}
}

static void get_from_rev(struct rev_info *rev, struct path_list *list)
{
	struct commit *commit;

	if (prepare_revision_walk(rev))
		die("revision walk setup failed");
	while ((commit = get_revision(rev)) != NULL) {
		const char *author = NULL, *buffer;

		buffer = commit->buffer;
		while (*buffer && *buffer != '\n') {
			const char *eol = strchr(buffer, '\n');

			if (eol == NULL)
				eol = buffer + strlen(buffer);
			else
				eol++;

			if (!prefixcmp(buffer, "author "))
				author = buffer + 7;
			buffer = eol;
		}
		if (!author)
			die("Missing author: %s",
			    sha1_to_hex(commit->object.sha1));
		if (*buffer)
			buffer++;
		insert_one_record(list, author, !*buffer ? "<none>" : buffer);
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
		else if (!strcmp(argv[1], "-e") ||
			 !strcmp(argv[1], "--email"))
			email = 1;
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

	read_mailmap(&mailmap, ".mailmap", &common_repo_prefix);

	/* assume HEAD if from a tty */
	if (!rev.pending.nr && isatty(0))
		add_head_to_pending(&rev);
	if (rev.pending.nr == 0) {
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
			printf("%6d\t%s\n", onelines->nr, list.items[i].path);
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
