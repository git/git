#include "cache.h"
#include "commit.h"
#include "epoch.h"

#define SEEN		(1u << 0)
#define INTERESTING	(1u << 1)
#define COUNTED		(1u << 2)

static const char rev_list_usage[] =
	"usage: git-rev-list [OPTION] commit-id <commit-id>\n"
		      "  --max-count=nr\n"
		      "  --max-age=epoch\n"
		      "  --min-age=epoch\n"
		      "  --header\n"
		      "  --pretty\n"
		      "  --merge-order [ --show-breaks ]";

static int bisect_list = 0;
static int verbose_header = 0;
static int show_parents = 0;
static int hdr_termination = 0;
static const char *prefix = "";
static unsigned long max_age = -1;
static unsigned long min_age = -1;
static int max_count = -1;
static enum cmit_fmt commit_format = CMIT_FMT_RAW;
static int merge_order = 0;
static int show_breaks = 0;

static void show_commit(struct commit *commit)
{
	if (show_breaks) {
		prefix = "| ";
		if (commit->object.flags & DISCONTINUITY) {
			prefix = "^ ";     
		} else if (commit->object.flags & BOUNDARY) {
			prefix = "= ";
		} 
        }        		
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
		static char pretty_header[16384];
		pretty_print_commit(commit_format, commit->buffer, ~0, pretty_header, sizeof(pretty_header));
		printf("%s%c", pretty_header, hdr_termination);
	}	
}

static int filter_commit(struct commit * commit)
{
	if (commit->object.flags & UNINTERESTING)
		return CONTINUE;
	if (min_age != -1 && (commit->date > min_age))
		return CONTINUE;
	if (max_age != -1 && (commit->date < max_age))
		return STOP;
	if (max_count != -1 && !max_count--)
		return STOP;

	return DO;
}

static int process_commit(struct commit * commit)
{
	int action=filter_commit(commit);

	if (action == STOP) {
		return STOP;
	}

	if (action == CONTINUE) {
		return CONTINUE;
	}

	show_commit(commit);

	return CONTINUE;
}

static void show_commit_list(struct commit_list *list)
{
	while (list) {
		struct commit *commit = pop_most_recent_commit(&list, SEEN);

		if (process_commit(commit) == STOP)
			break;
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

/*
 * This is a truly stupid algorithm, but it's only
 * used for bisection, and we just don't care enough.
 *
 * We care just barely enough to avoid recursing for
 * non-merge entries.
 */
static int count_distance(struct commit_list *entry)
{
	int nr = 0;

	while (entry) {
		struct commit *commit = entry->item;
		struct commit_list *p;

		if (commit->object.flags & (UNINTERESTING | COUNTED))
			break;
		nr++;
		commit->object.flags |= COUNTED;
		p = commit->parents;
		entry = p;
		if (p) {
			p = p->next;
			while (p) {
				nr += count_distance(p);
				p = p->next;
			}
		}
	}
	return nr;
}

static int clear_distance(struct commit_list *list)
{
	while (list) {
		struct commit *commit = list->item;
		commit->object.flags &= ~COUNTED;
		list = list->next;
	}
}

static struct commit_list *find_bisection(struct commit_list *list)
{
	int nr, closest;
	struct commit_list *p, *best;

	nr = 0;
	p = list;
	while (p) {
		nr++;
		p = p->next;
	}
	closest = 0;
	best = list;

	p = list;
	while (p) {
		int distance = count_distance(p);
		clear_distance(list);
		if (nr - distance < distance)
			distance = nr - distance;
		if (distance > closest) {
			best = p;
			closest = distance;
		}
		p = p->next;
	}
	if (best)
		best->next = NULL;
	return best;
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
	if (bisect_list)
		newlist = find_bisection(newlist);
	return newlist;
}

static enum cmit_fmt get_commit_format(const char *arg)
{
	if (!*arg)
		return CMIT_FMT_DEFAULT;
	if (!strcmp(arg, "=raw"))
		return CMIT_FMT_RAW;
	if (!strcmp(arg, "=medium"))
		return CMIT_FMT_MEDIUM;
	if (!strcmp(arg, "=short"))
		return CMIT_FMT_SHORT;
	usage(rev_list_usage);	
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
		if (!strncmp(arg, "--pretty", 8)) {
			commit_format = get_commit_format(arg+8);
			verbose_header = 1;
			hdr_termination = '\n';
			prefix = "commit ";
			continue;
		}
		if (!strcmp(arg, "--parents")) {
			show_parents = 1;
			continue;
		}
		if (!strcmp(arg, "--bisect")) {
			bisect_list = 1;
			continue;
		}
		if (!strncmp(arg, "--merge-order", 13)) {
		        merge_order = 1;
			continue;
		}
		if (!strncmp(arg, "--show-breaks", 13)) {
			show_breaks = 1;
			continue;
		}

		flags = 0;
		if (*arg == '^') {
			flags = UNINTERESTING;
			arg++;
			limited = 1;
		}
		if (get_sha1(arg, sha1) || (show_breaks && !merge_order))
			usage(rev_list_usage);
		commit = lookup_commit_reference(sha1);
		if (!commit || parse_commit(commit) < 0)
			die("bad commit object %s", arg);
		commit->object.flags |= flags;
		commit_list_insert(commit, &list);
	}

	if (!list)
		usage(rev_list_usage);

	if (!merge_order) {		
	        if (limited)
			list = limit_list(list);
		show_commit_list(list);
	} else {
		if (sort_list_in_merge_order(list, &process_commit)) {
			  die("merge order sort failed\n");
		}
	}

	return 0;
}
