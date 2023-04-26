#include "git-compat-util.h"
#include "config.h"
#include "commit.h"
#include "diff.h"
#include "environment.h"
#include "gettext.h"
#include "hex.h"
#include "revision.h"
#include "refs.h"
#include "list-objects.h"
#include "quote.h"
#include "hash-lookup.h"
#include "run-command.h"
#include "log-tree.h"
#include "bisect.h"
#include "oid-array.h"
#include "strvec.h"
#include "commit-slab.h"
#include "commit-reach.h"
#include "object-name.h"
#include "object-store.h"
#include "dir.h"

static struct oid_array good_revs;
static struct oid_array skipped_revs;

static struct object_id *current_bad_oid;

static const char *term_bad;
static const char *term_good;

/* Remember to update object flag allocation in object.h */
#define COUNTED		(1u<<16)

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

define_commit_slab(commit_weight, int *);
static struct commit_weight commit_weight;

#define DEBUG_BISECT 0

static inline int weight(struct commit_list *elem)
{
	return **commit_weight_at(&commit_weight, elem->item);
}

static inline void weight_set(struct commit_list *elem, int weight)
{
	**commit_weight_at(&commit_weight, elem->item) = weight;
}

static int count_interesting_parents(struct commit *commit, unsigned bisect_flags)
{
	struct commit_list *p;
	int count;

	for (count = 0, p = commit->parents; p; p = p->next) {
		if (!(p->item->object.flags & UNINTERESTING))
			count++;
		if (bisect_flags & FIND_BISECTION_FIRST_PARENT_ONLY)
			break;
	}
	return count;
}

static inline int approx_halfway(struct commit_list *p, int nr)
{
	int diff;

	/*
	 * Don't short-cut something we are not going to return!
	 */
	if (p->item->object.flags & TREESAME)
		return 0;
	if (DEBUG_BISECT)
		return 0;
	/*
	 * For small number of commits 2 and 3 are halfway of 5, and
	 * 3 is halfway of 6 but 2 and 4 are not.
	 */
	diff = 2 * weight(p) - nr;
	switch (diff) {
	case -1: case 0: case 1:
		return 1;
	default:
		/*
		 * For large number of commits we are not so strict, it's
		 * good enough if it's within ~0.1% of the halfway point,
		 * e.g. 5000 is exactly halfway of 10000, but we consider
		 * the values [4996, 5004] as halfway as well.
		 */
		if (abs(diff) < nr / 1024)
			return 1;
		return 0;
	}
}

static void show_list(const char *debug, int counted, int nr,
		      struct commit_list *list)
{
	struct commit_list *p;

	if (!DEBUG_BISECT)
		return;

	fprintf(stderr, "%s (%d/%d)\n", debug, counted, nr);

	for (p = list; p; p = p->next) {
		struct commit_list *pp;
		struct commit *commit = p->item;
		unsigned commit_flags = commit->object.flags;
		enum object_type type;
		unsigned long size;
		char *buf = repo_read_object_file(the_repository,
						  &commit->object.oid, &type,
						  &size);
		const char *subject_start;
		int subject_len;

		fprintf(stderr, "%c%c%c ",
			(commit_flags & TREESAME) ? ' ' : 'T',
			(commit_flags & UNINTERESTING) ? 'U' : ' ',
			(commit_flags & COUNTED) ? 'C' : ' ');
		if (*commit_weight_at(&commit_weight, p->item))
			fprintf(stderr, "%3d", weight(p));
		else
			fprintf(stderr, "---");
		fprintf(stderr, " %.*s", 8, oid_to_hex(&commit->object.oid));
		for (pp = commit->parents; pp; pp = pp->next)
			fprintf(stderr, " %.*s", 8,
				oid_to_hex(&pp->item->object.oid));

		subject_len = find_commit_subject(buf, &subject_start);
		if (subject_len)
			fprintf(stderr, " %.*s", subject_len, subject_start);
		fprintf(stderr, "\n");
	}
}

static struct commit_list *best_bisection(struct commit_list *list, int nr)
{
	struct commit_list *p, *best;
	int best_distance = -1;

	best = list;
	for (p = list; p; p = p->next) {
		int distance;
		unsigned commit_flags = p->item->object.flags;

		if (commit_flags & TREESAME)
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
	return oidcmp(&a->commit->object.oid, &b->commit->object.oid);
}

static struct commit_list *best_bisection_sorted(struct commit_list *list, int nr)
{
	struct commit_list *p;
	struct commit_dist *array = xcalloc(nr, sizeof(*array));
	struct strbuf buf = STRBUF_INIT;
	int cnt, i;

	for (p = list, cnt = 0; p; p = p->next) {
		int distance;
		unsigned commit_flags = p->item->object.flags;

		if (commit_flags & TREESAME)
			continue;
		distance = weight(p);
		if (nr - distance < distance)
			distance = nr - distance;
		array[cnt].commit = p->item;
		array[cnt].distance = distance;
		cnt++;
	}
	QSORT(array, cnt, compare_commit_dist);
	for (p = list, i = 0; i < cnt; i++) {
		struct object *obj = &(array[i].commit->object);

		strbuf_reset(&buf);
		strbuf_addf(&buf, "dist=%d", array[i].distance);
		add_name_decoration(DECORATION_NONE, buf.buf, obj);

		p->item = array[i].commit;
		if (i < cnt - 1)
			p = p->next;
	}
	if (p) {
		free_commit_list(p->next);
		p->next = NULL;
	}
	strbuf_release(&buf);
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
					     unsigned bisect_flags)
{
	int n, counted;
	struct commit_list *p;

	counted = 0;

	for (n = 0, p = list; p; p = p->next) {
		struct commit *commit = p->item;
		unsigned commit_flags = commit->object.flags;

		*commit_weight_at(&commit_weight, p->item) = &weights[n++];
		switch (count_interesting_parents(commit, bisect_flags)) {
		case 0:
			if (!(commit_flags & TREESAME)) {
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
		if (bisect_flags & FIND_BISECTION_FIRST_PARENT_ONLY)
			BUG("shouldn't be calling count-distance in fp mode");
		weight_set(p, count_distance(p));
		clear_distance(list);

		/* Does it happen to be at half-way? */
		if (!(bisect_flags & FIND_BISECTION_ALL) &&
		      approx_halfway(p, nr))
			return p;
		counted++;
	}

	show_list("bisection 2 count_distance", counted, nr, list);

	while (counted < nr) {
		for (p = list; p; p = p->next) {
			struct commit_list *q;
			unsigned commit_flags = p->item->object.flags;

			if (0 <= weight(p))
				continue;

			for (q = p->item->parents;
			     q;
			     q = bisect_flags & FIND_BISECTION_FIRST_PARENT_ONLY ? NULL : q->next) {
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
			if (!(commit_flags & TREESAME)) {
				weight_set(p, weight(q)+1);
				counted++;
				show_list("bisection 2 count one",
					  counted, nr, list);
			}
			else
				weight_set(p, weight(q));

			/* Does it happen to be at half-way? */
			if (!(bisect_flags & FIND_BISECTION_ALL) &&
			      approx_halfway(p, nr))
				return p;
		}
	}

	show_list("bisection 2 counted all", counted, nr, list);

	if (!(bisect_flags & FIND_BISECTION_ALL))
		return best_bisection(list, nr);
	else
		return best_bisection_sorted(list, nr);
}

void find_bisection(struct commit_list **commit_list, int *reaches,
		    int *all, unsigned bisect_flags)
{
	int nr, on_list;
	struct commit_list *list, *p, *best, *next, *last;
	int *weights;

	show_list("bisection 2 entry", 0, 0, *commit_list);
	init_commit_weight(&commit_weight);

	/*
	 * Count the number of total and tree-changing items on the
	 * list, while reversing the list.
	 */
	for (nr = on_list = 0, last = NULL, p = *commit_list;
	     p;
	     p = next) {
		unsigned commit_flags = p->item->object.flags;

		next = p->next;
		if (commit_flags & UNINTERESTING) {
			free(p);
			continue;
		}
		p->next = last;
		last = p;
		if (!(commit_flags & TREESAME))
			nr++;
		on_list++;
	}
	list = last;
	show_list("bisection 2 sorted", 0, nr, list);

	*all = nr;
	CALLOC_ARRAY(weights, on_list);

	/* Do the real work of finding bisection commit. */
	best = do_find_bisection(list, nr, weights, bisect_flags);
	if (best) {
		if (!(bisect_flags & FIND_BISECTION_ALL)) {
			list->item = best->item;
			free_commit_list(list->next);
			best = list;
			best->next = NULL;
		}
		*reaches = weight(best);
	}
	free(weights);
	*commit_list = best;
	clear_commit_weight(&commit_weight);
}

static int register_ref(const char *refname, const struct object_id *oid,
			int flags UNUSED, void *cb_data UNUSED)
{
	struct strbuf good_prefix = STRBUF_INIT;
	strbuf_addstr(&good_prefix, term_good);
	strbuf_addstr(&good_prefix, "-");

	if (!strcmp(refname, term_bad)) {
		current_bad_oid = xmalloc(sizeof(*current_bad_oid));
		oidcpy(current_bad_oid, oid);
	} else if (starts_with(refname, good_prefix.buf)) {
		oid_array_append(&good_revs, oid);
	} else if (starts_with(refname, "skip-")) {
		oid_array_append(&skipped_revs, oid);
	}

	strbuf_release(&good_prefix);

	return 0;
}

static int read_bisect_refs(void)
{
	return for_each_ref_in("refs/bisect/", register_ref, NULL);
}

static GIT_PATH_FUNC(git_path_bisect_names, "BISECT_NAMES")
static GIT_PATH_FUNC(git_path_bisect_expected_rev, "BISECT_EXPECTED_REV")
static GIT_PATH_FUNC(git_path_bisect_ancestors_ok, "BISECT_ANCESTORS_OK")
static GIT_PATH_FUNC(git_path_bisect_run, "BISECT_RUN")
static GIT_PATH_FUNC(git_path_bisect_start, "BISECT_START")
static GIT_PATH_FUNC(git_path_bisect_log, "BISECT_LOG")
static GIT_PATH_FUNC(git_path_bisect_terms, "BISECT_TERMS")
static GIT_PATH_FUNC(git_path_bisect_first_parent, "BISECT_FIRST_PARENT")

static void read_bisect_paths(struct strvec *array)
{
	struct strbuf str = STRBUF_INIT;
	const char *filename = git_path_bisect_names();
	FILE *fp = xfopen(filename, "r");

	while (strbuf_getline_lf(&str, fp) != EOF) {
		strbuf_trim(&str);
		if (sq_dequote_to_strvec(str.buf, array))
			die(_("Badly quoted content in file '%s': %s"),
			    filename, str.buf);
	}

	strbuf_release(&str);
	fclose(fp);
}

static char *join_oid_array_hex(struct oid_array *array, char delim)
{
	struct strbuf joined_hexs = STRBUF_INIT;
	int i;

	for (i = 0; i < array->nr; i++) {
		strbuf_addstr(&joined_hexs, oid_to_hex(array->oid + i));
		if (i + 1 < array->nr)
			strbuf_addch(&joined_hexs, delim);
	}

	return strbuf_detach(&joined_hexs, NULL);
}

/*
 * In this function, passing a not NULL skipped_first is very special.
 * It means that we want to know if the first commit in the list is
 * skipped because we will want to test a commit away from it if it is
 * indeed skipped.
 * So if the first commit is skipped, we cannot take the shortcut to
 * just "return list" when we find the first non skipped commit, we
 * have to return a fully filtered list.
 *
 * We use (*skipped_first == -1) to mean "it has been found that the
 * first commit is not skipped". In this case *skipped_first is set back
 * to 0 just before the function returns.
 */
struct commit_list *filter_skipped(struct commit_list *list,
				   struct commit_list **tried,
				   int show_all,
				   int *count,
				   int *skipped_first)
{
	struct commit_list *filtered = NULL, **f = &filtered;

	*tried = NULL;

	if (skipped_first)
		*skipped_first = 0;
	if (count)
		*count = 0;

	if (!skipped_revs.nr)
		return list;

	while (list) {
		struct commit_list *next = list->next;
		list->next = NULL;
		if (0 <= oid_array_lookup(&skipped_revs, &list->item->object.oid)) {
			if (skipped_first && !*skipped_first)
				*skipped_first = 1;
			/* Move current to tried list */
			*tried = list;
			tried = &list->next;
		} else {
			if (!show_all) {
				if (!skipped_first || !*skipped_first)
					return list;
			} else if (skipped_first && !*skipped_first) {
				/* This means we know it's not skipped */
				*skipped_first = -1;
			}
			/* Move current to filtered list */
			*f = list;
			f = &list->next;
			if (count)
				(*count)++;
		}
		list = next;
	}

	if (skipped_first && *skipped_first == -1)
		*skipped_first = 0;

	return filtered;
}

#define PRN_MODULO 32768

/*
 * This is a pseudo random number generator based on "man 3 rand".
 * It is not used properly because the seed is the argument and it
 * is increased by one between each call, but that should not matter
 * for this application.
 */
static unsigned get_prn(unsigned count)
{
	count = count * 1103515245 + 12345;
	return (count/65536) % PRN_MODULO;
}

/*
 * Custom integer square root from
 * https://en.wikipedia.org/wiki/Integer_square_root
 */
static int sqrti(int val)
{
	float d, x = val;

	if (!val)
		return 0;

	do {
		float y = (x + (float)val / x) / 2;
		d = (y > x) ? y - x : x - y;
		x = y;
	} while (d >= 0.5);

	return (int)x;
}

static struct commit_list *skip_away(struct commit_list *list, int count)
{
	struct commit_list *cur, *previous;
	int prn, index, i;

	prn = get_prn(count);
	index = (count * prn / PRN_MODULO) * sqrti(prn) / sqrti(PRN_MODULO);

	cur = list;
	previous = NULL;

	for (i = 0; cur; cur = cur->next, i++) {
		if (i == index) {
			if (!oideq(&cur->item->object.oid, current_bad_oid))
				return cur;
			if (previous)
				return previous;
			return list;
		}
		previous = cur;
	}

	return list;
}

static struct commit_list *managed_skipped(struct commit_list *list,
					   struct commit_list **tried)
{
	int count, skipped_first;

	*tried = NULL;

	if (!skipped_revs.nr)
		return list;

	list = filter_skipped(list, tried, 0, &count, &skipped_first);

	if (!skipped_first)
		return list;

	return skip_away(list, count);
}

static void bisect_rev_setup(struct repository *r, struct rev_info *revs,
			     struct strvec *rev_argv,
			     const char *prefix,
			     const char *bad_format, const char *good_format,
			     int read_paths)
{
	struct setup_revision_opt opt = {
		.free_removed_argv_elements = 1,
	};
	int i;

	repo_init_revisions(r, revs, prefix);
	revs->abbrev = 0;
	revs->commit_format = CMIT_FMT_UNSPECIFIED;

	/* rev_argv.argv[0] will be ignored by setup_revisions */
	strvec_push(rev_argv, "bisect_rev_setup");
	strvec_pushf(rev_argv, bad_format, oid_to_hex(current_bad_oid));
	for (i = 0; i < good_revs.nr; i++)
		strvec_pushf(rev_argv, good_format,
			     oid_to_hex(good_revs.oid + i));
	strvec_push(rev_argv, "--");
	if (read_paths)
		read_bisect_paths(rev_argv);

	setup_revisions(rev_argv->nr, rev_argv->v, revs, &opt);
}

static void bisect_common(struct rev_info *revs)
{
	if (prepare_revision_walk(revs))
		die("revision walk setup failed");
	if (revs->tree_objects)
		mark_edges_uninteresting(revs, NULL, 0);
}

static enum bisect_error error_if_skipped_commits(struct commit_list *tried,
				    const struct object_id *bad)
{
	if (!tried)
		return BISECT_OK;

	printf("There are only 'skip'ped commits left to test.\n"
	       "The first %s commit could be any of:\n", term_bad);

	for ( ; tried; tried = tried->next)
		printf("%s\n", oid_to_hex(&tried->item->object.oid));

	if (bad)
		printf("%s\n", oid_to_hex(bad));
	printf(_("We cannot bisect more!\n"));

	return BISECT_ONLY_SKIPPED_LEFT;
}

static int is_expected_rev(const struct object_id *oid)
{
	const char *filename = git_path_bisect_expected_rev();
	struct stat st;
	struct strbuf str = STRBUF_INIT;
	FILE *fp;
	int res = 0;

	if (stat(filename, &st) || !S_ISREG(st.st_mode))
		return 0;

	fp = fopen_or_warn(filename, "r");
	if (!fp)
		return 0;

	if (strbuf_getline_lf(&str, fp) != EOF)
		res = !strcmp(str.buf, oid_to_hex(oid));

	strbuf_release(&str);
	fclose(fp);

	return res;
}

enum bisect_error bisect_checkout(const struct object_id *bisect_rev,
				  int no_checkout)
{
	struct commit *commit;
	struct pretty_print_context pp = {0};
	struct strbuf commit_msg = STRBUF_INIT;

	update_ref(NULL, "BISECT_EXPECTED_REV", bisect_rev, NULL, 0, UPDATE_REFS_DIE_ON_ERR);

	if (no_checkout) {
		update_ref(NULL, "BISECT_HEAD", bisect_rev, NULL, 0,
			   UPDATE_REFS_DIE_ON_ERR);
	} else {
		struct child_process cmd = CHILD_PROCESS_INIT;

		cmd.git_cmd = 1;
		strvec_pushl(&cmd.args, "checkout", "-q",
			     oid_to_hex(bisect_rev), "--", NULL);
		if (run_command(&cmd))
			/*
			 * Errors in `run_command()` itself, signaled by res < 0,
			 * and errors in the child process, signaled by res > 0
			 * can both be treated as regular BISECT_FAILED (-1).
			 */
			return BISECT_FAILED;
	}

	commit = lookup_commit_reference(the_repository, bisect_rev);
	repo_format_commit_message(the_repository, commit, "[%H] %s%n",
				   &commit_msg, &pp);
	fputs(commit_msg.buf, stdout);
	strbuf_release(&commit_msg);

	return BISECT_OK;
}

static struct commit *get_commit_reference(struct repository *r,
					   const struct object_id *oid)
{
	struct commit *c = lookup_commit_reference(r, oid);
	if (!c)
		die(_("Not a valid commit name %s"), oid_to_hex(oid));
	return c;
}

static struct commit **get_bad_and_good_commits(struct repository *r,
						int *rev_nr)
{
	struct commit **rev;
	int i, n = 0;

	ALLOC_ARRAY(rev, 1 + good_revs.nr);
	rev[n++] = get_commit_reference(r, current_bad_oid);
	for (i = 0; i < good_revs.nr; i++)
		rev[n++] = get_commit_reference(r, good_revs.oid + i);
	*rev_nr = n;

	return rev;
}

static enum bisect_error handle_bad_merge_base(void)
{
	if (is_expected_rev(current_bad_oid)) {
		char *bad_hex = oid_to_hex(current_bad_oid);
		char *good_hex = join_oid_array_hex(&good_revs, ' ');
		if (!strcmp(term_bad, "bad") && !strcmp(term_good, "good")) {
			fprintf(stderr, _("The merge base %s is bad.\n"
				"This means the bug has been fixed "
				"between %s and [%s].\n"),
				bad_hex, bad_hex, good_hex);
		} else if (!strcmp(term_bad, "new") && !strcmp(term_good, "old")) {
			fprintf(stderr, _("The merge base %s is new.\n"
				"The property has changed "
				"between %s and [%s].\n"),
				bad_hex, bad_hex, good_hex);
		} else {
			fprintf(stderr, _("The merge base %s is %s.\n"
				"This means the first '%s' commit is "
				"between %s and [%s].\n"),
				bad_hex, term_bad, term_good, bad_hex, good_hex);
		}
		return BISECT_MERGE_BASE_CHECK;
	}

	fprintf(stderr, _("Some %s revs are not ancestors of the %s rev.\n"
		"git bisect cannot work properly in this case.\n"
		"Maybe you mistook %s and %s revs?\n"),
		term_good, term_bad, term_good, term_bad);
	return BISECT_FAILED;
}

static void handle_skipped_merge_base(const struct object_id *mb)
{
	char *mb_hex = oid_to_hex(mb);
	char *bad_hex = oid_to_hex(current_bad_oid);
	char *good_hex = join_oid_array_hex(&good_revs, ' ');

	warning(_("the merge base between %s and [%s] "
		"must be skipped.\n"
		"So we cannot be sure the first %s commit is "
		"between %s and %s.\n"
		"We continue anyway."),
		bad_hex, good_hex, term_bad, mb_hex, bad_hex);
	free(good_hex);
}

/*
 * "check_merge_bases" checks that merge bases are not "bad" (or "new").
 *
 * - If one is "bad" (or "new"), it means the user assumed something wrong
 * and we must return error with a non 0 error code.
 * - If one is "good" (or "old"), that's good, we have nothing to do.
 * - If one is "skipped", we can't know but we should warn.
 * - If we don't know, we should check it out and ask the user to test.
 * - If a merge base must be tested, on success return
 * BISECT_INTERNAL_SUCCESS_MERGE_BASE (-11) a special condition
 * for early success, this will be converted back to 0 in
 * check_good_are_ancestors_of_bad().
 */
static enum bisect_error check_merge_bases(int rev_nr, struct commit **rev, int no_checkout)
{
	enum bisect_error res = BISECT_OK;
	struct commit_list *result;

	result = repo_get_merge_bases_many(the_repository, rev[0], rev_nr - 1,
					   rev + 1);

	for (; result; result = result->next) {
		const struct object_id *mb = &result->item->object.oid;
		if (oideq(mb, current_bad_oid)) {
			res = handle_bad_merge_base();
			break;
		} else if (0 <= oid_array_lookup(&good_revs, mb)) {
			continue;
		} else if (0 <= oid_array_lookup(&skipped_revs, mb)) {
			handle_skipped_merge_base(mb);
		} else {
			printf(_("Bisecting: a merge base must be tested\n"));
			res = bisect_checkout(mb, no_checkout);
			if (!res)
				/* indicate early success */
				res = BISECT_INTERNAL_SUCCESS_MERGE_BASE;
			break;
		}
	}

	free_commit_list(result);
	return res;
}

static int check_ancestors(struct repository *r, int rev_nr,
			   struct commit **rev, const char *prefix)
{
	struct strvec rev_argv = STRVEC_INIT;
	struct rev_info revs;
	int res;

	bisect_rev_setup(r, &revs, &rev_argv, prefix, "^%s", "%s", 0);

	bisect_common(&revs);
	res = (revs.commits != NULL);

	/* Clean up objects used, as they will be reused. */
	clear_commit_marks_many(rev_nr, rev, ALL_REV_FLAGS);

	release_revisions(&revs);
	strvec_clear(&rev_argv);
	return res;
}

/*
 * "check_good_are_ancestors_of_bad" checks that all "good" revs are
 * ancestor of the "bad" rev.
 *
 * If that's not the case, we need to check the merge bases.
 * If a merge base must be tested by the user, its source code will be
 * checked out to be tested by the user and we will return.
 */

static enum bisect_error check_good_are_ancestors_of_bad(struct repository *r,
					    const char *prefix,
					    int no_checkout)
{
	char *filename;
	struct stat st;
	int fd, rev_nr;
	enum bisect_error res = BISECT_OK;
	struct commit **rev;

	if (!current_bad_oid)
		return error(_("a %s revision is needed"), term_bad);

	filename = git_pathdup("BISECT_ANCESTORS_OK");

	/* Check if file BISECT_ANCESTORS_OK exists. */
	if (!stat(filename, &st) && S_ISREG(st.st_mode))
		goto done;

	/* Bisecting with no good rev is ok. */
	if (!good_revs.nr)
		goto done;

	/* Check if all good revs are ancestor of the bad rev. */

	rev = get_bad_and_good_commits(r, &rev_nr);
	if (check_ancestors(r, rev_nr, rev, prefix))
		res = check_merge_bases(rev_nr, rev, no_checkout);
	free(rev);

	if (!res) {
		/* Create file BISECT_ANCESTORS_OK. */
		fd = open(filename, O_CREAT | O_TRUNC | O_WRONLY, 0600);
		if (fd < 0)
			/*
			 * BISECT_ANCESTORS_OK file is not absolutely necessary,
			 * the bisection process will continue at the next
			 * bisection step.
			 * So, just signal with a warning that something
			 * might be wrong.
			 */
			warning_errno(_("could not create file '%s'"),
				filename);
		else
			close(fd);
	}
 done:
	free(filename);
	return res;
}

/*
 * This does "git diff-tree --pretty COMMIT" without one fork+exec.
 */
static void show_diff_tree(struct repository *r,
			   const char *prefix,
			   struct commit *commit)
{
	const char *argv[] = {
		"diff-tree", "--pretty", "--stat", "--summary", "--cc", NULL
	};
	struct rev_info opt;

	git_config(git_diff_ui_config, NULL);
	repo_init_revisions(r, &opt, prefix);

	setup_revisions(ARRAY_SIZE(argv) - 1, argv, &opt, NULL);
	log_tree_commit(&opt, commit);
	release_revisions(&opt);
}

/*
 * The terms used for this bisect session are stored in BISECT_TERMS.
 * We read them and store them to adapt the messages accordingly.
 * Default is bad/good.
 */
void read_bisect_terms(const char **read_bad, const char **read_good)
{
	struct strbuf str = STRBUF_INIT;
	const char *filename = git_path_bisect_terms();
	FILE *fp = fopen(filename, "r");

	if (!fp) {
		if (errno == ENOENT) {
			*read_bad = "bad";
			*read_good = "good";
			return;
		} else {
			die_errno(_("could not read file '%s'"), filename);
		}
	} else {
		strbuf_getline_lf(&str, fp);
		*read_bad = strbuf_detach(&str, NULL);
		strbuf_getline_lf(&str, fp);
		*read_good = strbuf_detach(&str, NULL);
	}
	strbuf_release(&str);
	fclose(fp);
}

/*
 * We use the convention that return BISECT_INTERNAL_SUCCESS_1ST_BAD_FOUND (-10) means
 * the bisection process finished successfully.
 * In this case the calling function or command should not turn a
 * BISECT_INTERNAL_SUCCESS_1ST_BAD_FOUND return code into an error or a non zero exit code.
 *
 * Checking BISECT_INTERNAL_SUCCESS_1ST_BAD_FOUND
 * in bisect_helper::bisect_next() and only transforming it to 0 at
 * the end of bisect_helper::cmd_bisect__helper() helps bypassing
 * all the code related to finding a commit to test.
 */
enum bisect_error bisect_next_all(struct repository *r, const char *prefix)
{
	struct strvec rev_argv = STRVEC_INIT;
	struct rev_info revs = REV_INFO_INIT;
	struct commit_list *tried;
	int reaches = 0, all = 0, nr, steps;
	enum bisect_error res = BISECT_OK;
	struct object_id *bisect_rev;
	char *steps_msg;
	/*
	 * If no_checkout is non-zero, the bisection process does not
	 * checkout the trial commit but instead simply updates BISECT_HEAD.
	 */
	int no_checkout = ref_exists("BISECT_HEAD");
	unsigned bisect_flags = 0;

	read_bisect_terms(&term_bad, &term_good);
	if (read_bisect_refs())
		die(_("reading bisect refs failed"));

	if (file_exists(git_path_bisect_first_parent()))
		bisect_flags |= FIND_BISECTION_FIRST_PARENT_ONLY;

	if (skipped_revs.nr)
		bisect_flags |= FIND_BISECTION_ALL;

	res = check_good_are_ancestors_of_bad(r, prefix, no_checkout);
	if (res)
		goto cleanup;

	bisect_rev_setup(r, &revs, &rev_argv, prefix, "%s", "^%s", 1);

	revs.first_parent_only = !!(bisect_flags & FIND_BISECTION_FIRST_PARENT_ONLY);
	revs.limited = 1;

	bisect_common(&revs);

	find_bisection(&revs.commits, &reaches, &all, bisect_flags);
	revs.commits = managed_skipped(revs.commits, &tried);

	if (!revs.commits) {
		/*
		 * We should return error here only if the "bad"
		 * commit is also a "skip" commit.
		 */
		res = error_if_skipped_commits(tried, NULL);
		if (res < 0)
			goto cleanup;
		printf(_("%s was both %s and %s\n"),
		       oid_to_hex(current_bad_oid),
		       term_good,
		       term_bad);

		res = BISECT_FAILED;
		goto cleanup;
	}

	if (!all) {
		fprintf(stderr, _("No testable commit found.\n"
			"Maybe you started with bad path arguments?\n"));

		res = BISECT_NO_TESTABLE_COMMIT;
		goto cleanup;
	}

	bisect_rev = &revs.commits->item->object.oid;

	if (oideq(bisect_rev, current_bad_oid)) {
		res = error_if_skipped_commits(tried, current_bad_oid);
		if (res)
			return res;
		printf("%s is the first %s commit\n", oid_to_hex(bisect_rev),
			term_bad);

		show_diff_tree(r, prefix, revs.commits->item);
		/*
		 * This means the bisection process succeeded.
		 * Using BISECT_INTERNAL_SUCCESS_1ST_BAD_FOUND (-10)
		 * so that the call chain can simply check
		 * for negative return values for early returns up
		 * until the cmd_bisect__helper() caller.
		 */
		res = BISECT_INTERNAL_SUCCESS_1ST_BAD_FOUND;
		goto cleanup;
	}

	nr = all - reaches - 1;
	steps = estimate_bisect_steps(all);

	steps_msg = xstrfmt(Q_("(roughly %d step)", "(roughly %d steps)",
		  steps), steps);
	/*
	 * TRANSLATORS: the last %s will be replaced with "(roughly %d
	 * steps)" translation.
	 */
	printf(Q_("Bisecting: %d revision left to test after this %s\n",
		  "Bisecting: %d revisions left to test after this %s\n",
		  nr), nr, steps_msg);
	free(steps_msg);
	/* Clean up objects used, as they will be reused. */
	repo_clear_commit_marks(r, ALL_REV_FLAGS);

	res = bisect_checkout(bisect_rev, no_checkout);
cleanup:
	release_revisions(&revs);
	strvec_clear(&rev_argv);
	return res;
}

static inline int log2i(int n)
{
	int log2 = 0;

	for (; n > 1; n >>= 1)
		log2++;

	return log2;
}

static inline int exp2i(int n)
{
	return 1 << n;
}

/*
 * Estimate the number of bisect steps left (after the current step)
 *
 * For any x between 0 included and 2^n excluded, the probability for
 * n - 1 steps left looks like:
 *
 * P(2^n + x) == (2^n - x) / (2^n + x)
 *
 * and P(2^n + x) < 0.5 means 2^n < 3x
 */
int estimate_bisect_steps(int all)
{
	int n, x, e;

	if (all < 3)
		return 0;

	n = log2i(all);
	e = exp2i(n);
	x = all - e;

	return (e < 3 * x) ? n : n - 1;
}

static int mark_for_removal(const char *refname,
			    const struct object_id *oid UNUSED,
			    int flag UNUSED, void *cb_data)
{
	struct string_list *refs = cb_data;
	char *ref = xstrfmt("refs/bisect%s", refname);
	string_list_append(refs, ref);
	return 0;
}

int bisect_clean_state(void)
{
	int result = 0;

	/* There may be some refs packed during bisection */
	struct string_list refs_for_removal = STRING_LIST_INIT_NODUP;
	for_each_ref_in("refs/bisect", mark_for_removal, (void *) &refs_for_removal);
	string_list_append(&refs_for_removal, xstrdup("BISECT_HEAD"));
	result = delete_refs("bisect: remove", &refs_for_removal, REF_NO_DEREF);
	refs_for_removal.strdup_strings = 1;
	string_list_clear(&refs_for_removal, 0);
	unlink_or_warn(git_path_bisect_expected_rev());
	unlink_or_warn(git_path_bisect_ancestors_ok());
	unlink_or_warn(git_path_bisect_log());
	unlink_or_warn(git_path_bisect_names());
	unlink_or_warn(git_path_bisect_run());
	unlink_or_warn(git_path_bisect_terms());
	unlink_or_warn(git_path_bisect_first_parent());
	/*
	 * Cleanup BISECT_START last to support the --no-checkout option
	 * introduced in the commit 4796e823a.
	 */
	unlink_or_warn(git_path_bisect_start());

	return result;
}
