#include "cache.h"
#include "refs.h"
#include "tag.h"
#include "commit.h"
#include "tree.h"
#include "blob.h"
#include "tree-walk.h"
#include "diff.h"
#include "revision.h"
#include "list-objects.h"
#include "builtin.h"
#include "log-tree.h"
#include "graph.h"

/* bits #0-15 in revision.h */

#define COUNTED		(1u<<16)

static const char rev_list_usage[] =
"git rev-list [OPTION] <commit-id>... [ -- paths... ]\n"
"  limiting output:\n"
"    --max-count=nr\n"
"    --max-age=epoch\n"
"    --min-age=epoch\n"
"    --sparse\n"
"    --no-merges\n"
"    --remove-empty\n"
"    --all\n"
"    --branches\n"
"    --tags\n"
"    --remotes\n"
"    --stdin\n"
"    --quiet\n"
"  ordering output:\n"
"    --topo-order\n"
"    --date-order\n"
"    --reverse\n"
"  formatting output:\n"
"    --parents\n"
"    --children\n"
"    --objects | --objects-edge\n"
"    --unpacked\n"
"    --header | --pretty\n"
"    --abbrev=nr | --no-abbrev\n"
"    --abbrev-commit\n"
"    --left-right\n"
"  special purpose:\n"
"    --bisect\n"
"    --bisect-vars\n"
"    --bisect-all"
;

static struct rev_info revs;

static int bisect_list;
static int show_timestamp;
static int hdr_termination;
static const char *header_prefix;

static void finish_commit(struct commit *commit);
static void show_commit(struct commit *commit)
{
	graph_show_commit(revs.graph);

	if (show_timestamp)
		printf("%lu ", commit->date);
	if (header_prefix)
		fputs(header_prefix, stdout);

	if (!revs.graph) {
		if (commit->object.flags & BOUNDARY)
			putchar('-');
		else if (commit->object.flags & UNINTERESTING)
			putchar('^');
		else if (revs.left_right) {
			if (commit->object.flags & SYMMETRIC_LEFT)
				putchar('<');
			else
				putchar('>');
		}
	}
	if (revs.abbrev_commit && revs.abbrev)
		fputs(find_unique_abbrev(commit->object.sha1, revs.abbrev),
		      stdout);
	else
		fputs(sha1_to_hex(commit->object.sha1), stdout);
	if (revs.print_parents) {
		struct commit_list *parents = commit->parents;
		while (parents) {
			printf(" %s", sha1_to_hex(parents->item->object.sha1));
			parents = parents->next;
		}
	}
	if (revs.children.name) {
		struct commit_list *children;

		children = lookup_decoration(&revs.children, &commit->object);
		while (children) {
			printf(" %s", sha1_to_hex(children->item->object.sha1));
			children = children->next;
		}
	}
	show_decorations(commit);
	if (revs.commit_format == CMIT_FMT_ONELINE)
		putchar(' ');
	else
		putchar('\n');

	if (revs.verbose_header && commit->buffer) {
		struct strbuf buf;
		strbuf_init(&buf, 0);
		pretty_print_commit(revs.commit_format, commit,
				    &buf, revs.abbrev, NULL, NULL,
				    revs.date_mode, 0);
		if (revs.graph) {
			if (buf.len) {
				if (revs.commit_format != CMIT_FMT_ONELINE)
					graph_show_oneline(revs.graph);

				graph_show_commit_msg(revs.graph, &buf);

				/*
				 * Add a newline after the commit message.
				 *
				 * Usually, this newline produces a blank
				 * padding line between entries, in which case
				 * we need to add graph padding on this line.
				 *
				 * However, the commit message may not end in a
				 * newline.  In this case the newline simply
				 * ends the last line of the commit message,
				 * and we don't need any graph output.  (This
				 * always happens with CMIT_FMT_ONELINE, and it
				 * happens with CMIT_FMT_USERFORMAT when the
				 * format doesn't explicitly end in a newline.)
				 */
				if (buf.len && buf.buf[buf.len - 1] == '\n')
					graph_show_padding(revs.graph);
				putchar('\n');
			} else {
				/*
				 * If the message buffer is empty, just show
				 * the rest of the graph output for this
				 * commit.
				 */
				if (graph_show_remainder(revs.graph))
					putchar('\n');
			}
		} else {
			if (buf.len)
				printf("%s%c", buf.buf, hdr_termination);
		}
		strbuf_release(&buf);
	} else {
		if (graph_show_remainder(revs.graph))
			putchar('\n');
	}
	maybe_flush_or_die(stdout, "stdout");
	finish_commit(commit);
}

static void finish_commit(struct commit *commit)
{
	if (commit->parents) {
		free_commit_list(commit->parents);
		commit->parents = NULL;
	}
	free(commit->buffer);
	commit->buffer = NULL;
}

static void finish_object(struct object_array_entry *p)
{
	if (p->item->type == OBJ_BLOB && !has_sha1_file(p->item->sha1))
		die("missing blob object '%s'", sha1_to_hex(p->item->sha1));
}

static void show_object(struct object_array_entry *p)
{
	/* An object with name "foo\n0000000..." can be used to
	 * confuse downstream git-pack-objects very badly.
	 */
	const char *ep = strchr(p->name, '\n');

	finish_object(p);
	if (ep) {
		printf("%s %.*s\n", sha1_to_hex(p->item->sha1),
		       (int) (ep - p->name),
		       p->name);
	}
	else
		printf("%s %s\n", sha1_to_hex(p->item->sha1), p->name);
}

static void show_edge(struct commit *commit)
{
	printf("-%s\n", sha1_to_hex(commit->object.sha1));
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
		if (!(commit->object.flags & TREESAME))
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

static void clear_distance(struct commit_list *list)
{
	while (list) {
		struct commit *commit = list->item;
		commit->object.flags &= ~COUNTED;
		list = list->next;
	}
}

#define DEBUG_BISECT 0

static inline int weight(struct commit_list *elem)
{
	return *((int*)(elem->item->util));
}

static inline void weight_set(struct commit_list *elem, int weight)
{
	*((int*)(elem->item->util)) = weight;
}

static int count_interesting_parents(struct commit *commit)
{
	struct commit_list *p;
	int count;

	for (count = 0, p = commit->parents; p; p = p->next) {
		if (p->item->object.flags & UNINTERESTING)
			continue;
		count++;
	}
	return count;
}

static inline int halfway(struct commit_list *p, int nr)
{
	/*
	 * Don't short-cut something we are not going to return!
	 */
	if (p->item->object.flags & TREESAME)
		return 0;
	if (DEBUG_BISECT)
		return 0;
	/*
	 * 2 and 3 are halfway of 5.
	 * 3 is halfway of 6 but 2 and 4 are not.
	 */
	switch (2 * weight(p) - nr) {
	case -1: case 0: case 1:
		return 1;
	default:
		return 0;
	}
}

#if !DEBUG_BISECT
#define show_list(a,b,c,d) do { ; } while (0)
#else
static void show_list(const char *debug, int counted, int nr,
		      struct commit_list *list)
{
	struct commit_list *p;

	fprintf(stderr, "%s (%d/%d)\n", debug, counted, nr);

	for (p = list; p; p = p->next) {
		struct commit_list *pp;
		struct commit *commit = p->item;
		unsigned flags = commit->object.flags;
		enum object_type type;
		unsigned long size;
		char *buf = read_sha1_file(commit->object.sha1, &type, &size);
		char *ep, *sp;

		fprintf(stderr, "%c%c%c ",
			(flags & TREESAME) ? ' ' : 'T',
			(flags & UNINTERESTING) ? 'U' : ' ',
			(flags & COUNTED) ? 'C' : ' ');
		if (commit->util)
			fprintf(stderr, "%3d", weight(p));
		else
			fprintf(stderr, "---");
		fprintf(stderr, " %.*s", 8, sha1_to_hex(commit->object.sha1));
		for (pp = commit->parents; pp; pp = pp->next)
			fprintf(stderr, " %.*s", 8,
				sha1_to_hex(pp->item->object.sha1));

		sp = strstr(buf, "\n\n");
		if (sp) {
			sp += 2;
			for (ep = sp; *ep && *ep != '\n'; ep++)
				;
			fprintf(stderr, " %.*s", (int)(ep - sp), sp);
		}
		fprintf(stderr, "\n");
	}
}
#endif /* DEBUG_BISECT */

static struct commit_list *best_bisection(struct commit_list *list, int nr)
{
	struct commit_list *p, *best;
	int best_distance = -1;

	best = list;
	for (p = list; p; p = p->next) {
		int distance;
		unsigned flags = p->item->object.flags;

		if (flags & TREESAME)
			continue;
		distance = weight(p);
		if (nr - distance < distance)
			distance = nr - distance;
		if (distance > best_distance) {
			best = p;
			best_distance = distance;
		}
	}

	return best;
}

struct commit_dist {
	struct commit *commit;
	int distance;
};

static int compare_commit_dist(const void *a_, const void *b_)
{
	struct commit_dist *a, *b;

	a = (struct commit_dist *)a_;
	b = (struct commit_dist *)b_;
	if (a->distance != b->distance)
		return b->distance - a->distance; /* desc sort */
	return hashcmp(a->commit->object.sha1, b->commit->object.sha1);
}

static struct commit_list *best_bisection_sorted(struct commit_list *list, int nr)
{
	struct commit_list *p;
	struct commit_dist *array = xcalloc(nr, sizeof(*array));
	int cnt, i;

	for (p = list, cnt = 0; p; p = p->next) {
		int distance;
		unsigned flags = p->item->object.flags;

		if (flags & TREESAME)
			continue;
		distance = weight(p);
		if (nr - distance < distance)
			distance = nr - distance;
		array[cnt].commit = p->item;
		array[cnt].distance = distance;
		cnt++;
	}
	qsort(array, cnt, sizeof(*array), compare_commit_dist);
	for (p = list, i = 0; i < cnt; i++) {
		struct name_decoration *r = xmalloc(sizeof(*r) + 100);
		struct object *obj = &(array[i].commit->object);

		sprintf(r->name, "dist=%d", array[i].distance);
		r->next = add_decoration(&name_decoration, obj, r);
		p->item = array[i].commit;
		p = p->next;
	}
	if (p)
		p->next = NULL;
	free(array);
	return list;
}

/*
 * zero or positive weight is the number of interesting commits it can
 * reach, including itself.  Especially, weight = 0 means it does not
 * reach any tree-changing commits (e.g. just above uninteresting one
 * but traversal is with pathspec).
 *
 * weight = -1 means it has one parent and its distance is yet to
 * be computed.
 *
 * weight = -2 means it has more than one parent and its distance is
 * unknown.  After running count_distance() first, they will get zero
 * or positive distance.
 */
static struct commit_list *do_find_bisection(struct commit_list *list,
					     int nr, int *weights,
					     int find_all)
{
	int n, counted;
	struct commit_list *p;

	counted = 0;

	for (n = 0, p = list; p; p = p->next) {
		struct commit *commit = p->item;
		unsigned flags = commit->object.flags;

		p->item->util = &weights[n++];
		switch (count_interesting_parents(commit)) {
		case 0:
			if (!(flags & TREESAME)) {
				weight_set(p, 1);
				counted++;
				show_list("bisection 2 count one",
					  counted, nr, list);
			}
			/*
			 * otherwise, it is known not to reach any
			 * tree-changing commit and gets weight 0.
			 */
			break;
		case 1:
			weight_set(p, -1);
			break;
		default:
			weight_set(p, -2);
			break;
		}
	}

	show_list("bisection 2 initialize", counted, nr, list);

	/*
	 * If you have only one parent in the resulting set
	 * then you can reach one commit more than that parent
	 * can reach.  So we do not have to run the expensive
	 * count_distance() for single strand of pearls.
	 *
	 * However, if you have more than one parents, you cannot
	 * just add their distance and one for yourself, since
	 * they usually reach the same ancestor and you would
	 * end up counting them twice that way.
	 *
	 * So we will first count distance of merges the usual
	 * way, and then fill the blanks using cheaper algorithm.
	 */
	for (p = list; p; p = p->next) {
		if (p->item->object.flags & UNINTERESTING)
			continue;
		if (weight(p) != -2)
			continue;
		weight_set(p, count_distance(p));
		clear_distance(list);

		/* Does it happen to be at exactly half-way? */
		if (!find_all && halfway(p, nr))
			return p;
		counted++;
	}

	show_list("bisection 2 count_distance", counted, nr, list);

	while (counted < nr) {
		for (p = list; p; p = p->next) {
			struct commit_list *q;
			unsigned flags = p->item->object.flags;

			if (0 <= weight(p))
				continue;
			for (q = p->item->parents; q; q = q->next) {
				if (q->item->object.flags & UNINTERESTING)
					continue;
				if (0 <= weight(q))
					break;
			}
			if (!q)
				continue;

			/*
			 * weight for p is unknown but q is known.
			 * add one for p itself if p is to be counted,
			 * otherwise inherit it from q directly.
			 */
			if (!(flags & TREESAME)) {
				weight_set(p, weight(q)+1);
				counted++;
				show_list("bisection 2 count one",
					  counted, nr, list);
			}
			else
				weight_set(p, weight(q));

			/* Does it happen to be at exactly half-way? */
			if (!find_all && halfway(p, nr))
				return p;
		}
	}

	show_list("bisection 2 counted all", counted, nr, list);

	if (!find_all)
		return best_bisection(list, nr);
	else
		return best_bisection_sorted(list, nr);
}

static struct commit_list *find_bisection(struct commit_list *list,
					  int *reaches, int *all,
					  int find_all)
{
	int nr, on_list;
	struct commit_list *p, *best, *next, *last;
	int *weights;

	show_list("bisection 2 entry", 0, 0, list);

	/*
	 * Count the number of total and tree-changing items on the
	 * list, while reversing the list.
	 */
	for (nr = on_list = 0, last = NULL, p = list;
	     p;
	     p = next) {
		unsigned flags = p->item->object.flags;

		next = p->next;
		if (flags & UNINTERESTING)
			continue;
		p->next = last;
		last = p;
		if (!(flags & TREESAME))
			nr++;
		on_list++;
	}
	list = last;
	show_list("bisection 2 sorted", 0, nr, list);

	*all = nr;
	weights = xcalloc(on_list, sizeof(*weights));

	/* Do the real work of finding bisection commit. */
	best = do_find_bisection(list, nr, weights, find_all);
	if (best) {
		if (!find_all)
			best->next = NULL;
		*reaches = weight(best);
	}
	free(weights);
	return best;
}

int cmd_rev_list(int argc, const char **argv, const char *prefix)
{
	struct commit_list *list;
	int i;
	int read_from_stdin = 0;
	int bisect_show_vars = 0;
	int bisect_find_all = 0;
	int quiet = 0;

	git_config(git_default_config, NULL);
	init_revisions(&revs, prefix);
	revs.abbrev = 0;
	revs.commit_format = CMIT_FMT_UNSPECIFIED;
	argc = setup_revisions(argc, argv, &revs, NULL);

	quiet = DIFF_OPT_TST(&revs.diffopt, QUIET);
	for (i = 1 ; i < argc; i++) {
		const char *arg = argv[i];

		if (!strcmp(arg, "--header")) {
			revs.verbose_header = 1;
			continue;
		}
		if (!strcmp(arg, "--timestamp")) {
			show_timestamp = 1;
			continue;
		}
		if (!strcmp(arg, "--bisect")) {
			bisect_list = 1;
			continue;
		}
		if (!strcmp(arg, "--bisect-all")) {
			bisect_list = 1;
			bisect_find_all = 1;
			continue;
		}
		if (!strcmp(arg, "--bisect-vars")) {
			bisect_list = 1;
			bisect_show_vars = 1;
			continue;
		}
		if (!strcmp(arg, "--stdin")) {
			if (read_from_stdin++)
				die("--stdin given twice?");
			read_revisions_from_stdin(&revs);
			continue;
		}
		usage(rev_list_usage);

	}
	if (revs.commit_format != CMIT_FMT_UNSPECIFIED) {
		/* The command line has a --pretty  */
		hdr_termination = '\n';
		if (revs.commit_format == CMIT_FMT_ONELINE)
			header_prefix = "";
		else
			header_prefix = "commit ";
	}
	else if (revs.verbose_header)
		/* Only --header was specified */
		revs.commit_format = CMIT_FMT_RAW;

	list = revs.commits;

	if ((!list &&
	     (!(revs.tag_objects||revs.tree_objects||revs.blob_objects) &&
	      !revs.pending.nr)) ||
	    revs.diff)
		usage(rev_list_usage);

	save_commit_buffer = revs.verbose_header || revs.grep_filter;
	if (bisect_list)
		revs.limited = 1;

	if (prepare_revision_walk(&revs))
		die("revision walk setup failed");
	if (revs.tree_objects)
		mark_edges_uninteresting(revs.commits, &revs, show_edge);

	if (bisect_list) {
		int reaches = reaches, all = all;

		revs.commits = find_bisection(revs.commits, &reaches, &all,
					      bisect_find_all);
		if (bisect_show_vars) {
			int cnt;
			char hex[41];
			if (!revs.commits)
				return 1;
			/*
			 * revs.commits can reach "reaches" commits among
			 * "all" commits.  If it is good, then there are
			 * (all-reaches) commits left to be bisected.
			 * On the other hand, if it is bad, then the set
			 * to bisect is "reaches".
			 * A bisect set of size N has (N-1) commits further
			 * to test, as we already know one bad one.
			 */
			cnt = all - reaches;
			if (cnt < reaches)
				cnt = reaches;
			strcpy(hex, sha1_to_hex(revs.commits->item->object.sha1));

			if (bisect_find_all) {
				traverse_commit_list(&revs, show_commit, show_object);
				printf("------\n");
			}

			printf("bisect_rev=%s\n"
			       "bisect_nr=%d\n"
			       "bisect_good=%d\n"
			       "bisect_bad=%d\n"
			       "bisect_all=%d\n",
			       hex,
			       cnt - 1,
			       all - reaches - 1,
			       reaches - 1,
			       all);
			return 0;
		}
	}

	traverse_commit_list(&revs,
		quiet ? finish_commit : show_commit,
		quiet ? finish_object : show_object);

	return 0;
}
