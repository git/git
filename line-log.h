#ifndef LINE_LOG_H
#define LINE_LOG_H

struct rev_info;
struct commit;
struct string_list;

/* A range [start,end].  Lines are numbered starting at 0, and the
 * ranges include start but exclude end. */
struct range {
	long start, end;
};

/* A set of ranges.  The ranges must always be disjoint and sorted. */
struct range_set {
	unsigned int alloc, nr;
	struct range *ranges;
};

/* A diff, encoded as the set of pre- and post-image ranges where the
 * files differ. A pair of ranges corresponds to a hunk. */
struct diff_ranges {
	struct range_set parent;
	struct range_set target;
};

void range_set_init(struct range_set *, size_t prealloc);
void range_set_release(struct range_set *);
/* Range includes start; excludes end */
void range_set_append_unsafe(struct range_set *, long start, long end);
/* New range must begin at or after end of last added range */
void range_set_append(struct range_set *, long start, long end);
/*
 * In-place pass of sorting and merging the ranges in the range set,
 * to sort and make the ranges disjoint.
 */
void sort_and_merge_range_set(struct range_set *);

/* Linked list of interesting files and their associated ranges.  The
 * list must be kept sorted by path.
 *
 * For simplicity, even though this is highly redundant, each
 * line_log_data owns its 'path'.
 */
struct line_log_data {
	struct line_log_data *next;
	char *path;
	struct range_set ranges;
	struct diff_filepair *pair;
	struct diff_ranges diff;
};

void line_log_init(struct rev_info *rev, const char *prefix,
		   struct string_list *args);

int line_log_filter(struct rev_info *rev);
int line_log_process_ranges_arbitrary_commit(struct rev_info *rev,
					     struct commit *commit);

int line_log_print(struct rev_info *rev, struct commit *commit);

void line_log_free(struct rev_info *rev);

#endif /* LINE_LOG_H */
