#include "cache.h"
#include "commit.h"

#define SEEN		(1u << 0)
#define INTERESTING	(1u << 1)
#define UNINTERESTING	(1u << 2)

static const char rev_list_usage[] =
	"usage: git-rev-list [OPTION] commit-id <commit-id>\n"
		      "  --max-count=nr\n"
		      "  --max-age=epoch\n"
		      "  --min-age=epoch\n"
		      "  --header\n"
		      "  --pretty";

static int verbose_header = 0;
static int show_parents = 0;
static int pretty_print = 0;
static int hdr_termination = 0;
static const char *prefix = "";
static unsigned long max_age = -1;
static unsigned long min_age = -1;
static int max_count = -1;

static void show_commit(struct commit *commit)
{
	printf("%s%s", prefix, sha1_to_hex(commit->object.sha1));
	if (show_parents) {
		struct commit_list *parents = commit->parents;
		while (parents) {
			printf(" %s", sha1_to_hex(parents->item->object.sha1));
			parents = parents->next;
		}
	}
	putchar('\n');
	if (verbose_header) {
		const char *buf = commit->buffer;
		if (pretty_print) {
			static char pretty_header[16384];
			pretty_print_commit(commit->buffer, ~0, pretty_header, sizeof(pretty_header));
			buf = pretty_header;
		}
		printf("%s%c", buf, hdr_termination);
	}
}

static void show_commit_list(struct commit_list *list)
{
	while (list) {
		struct commit *commit = pop_most_recent_commit(&list, SEEN);

		if (commit->object.flags & UNINTERESTING)
			continue;
		if (min_age != -1 && (commit->date > min_age))
			continue;
		if (max_age != -1 && (commit->date < max_age))
			break;
		if (max_count != -1 && !max_count--)
			break;
		show_commit(commit);
	}
}

static void mark_parents_uninteresting(struct commit *commit)
{
	struct commit_list *parents = commit->parents;

	while (parents) {
		struct commit *commit = parents->item;
		commit->object.flags |= UNINTERESTING;
		parents = parents->next;
	}
}

static int everybody_uninteresting(struct commit_list *list)
{
	while (list) {
		struct commit *commit = list->item;
		list = list->next;
		if (commit->object.flags & UNINTERESTING)
			continue;
		return 0;
	}
	return 1;
}

struct commit_list *limit_list(struct commit_list *list)
{
	struct commit_list *newlist = NULL;
	struct commit_list **p = &newlist;
	do {
		struct commit *commit = pop_most_recent_commit(&list, SEEN);
		struct object *obj = &commit->object;

		if (obj->flags & UNINTERESTING) {
			mark_parents_uninteresting(commit);
			if (everybody_uninteresting(list))
				break;
			continue;
		}
		p = &commit_list_insert(commit, p)->next;
	} while (list);
	return newlist;
}

int main(int argc, char **argv)
{
	struct commit_list *list = NULL;
	int i, limited = 0;

	for (i = 1 ; i < argc; i++) {
		int flags;
		char *arg = argv[i];
		unsigned char sha1[20];
		struct commit *commit;

		if (!strncmp(arg, "--max-count=", 12)) {
			max_count = atoi(arg + 12);
			continue;
		}
		if (!strncmp(arg, "--max-age=", 10)) {
			max_age = atoi(arg + 10);
			continue;
		}
		if (!strncmp(arg, "--min-age=", 10)) {
			min_age = atoi(arg + 10);
			continue;
		}
		if (!strcmp(arg, "--header")) {
			verbose_header = 1;
			continue;
		}
		if (!strcmp(arg, "--pretty")) {
			verbose_header = 1;
			pretty_print = 1;
			hdr_termination = '\n';
			prefix = "commit ";
			continue;
		}
		if (!strcmp(arg, "--parents")) {
			show_parents = 1;
			continue;
		}

		flags = 0;
		if (*arg == '^') {
			flags = UNINTERESTING;
			arg++;
			limited = 1;
		}
		if (get_sha1(arg, sha1))
			usage(rev_list_usage);
		commit = lookup_commit_reference(sha1);
		if (!commit || parse_commit(commit) < 0)
			die("bad commit object %s", arg);
		commit->object.flags |= flags;
		commit_list_insert(commit, &list);
	}

	if (!list)
		usage(rev_list_usage);

	if (limited)
		list = limit_list(list);

	show_commit_list(list);
	return 0;
}
