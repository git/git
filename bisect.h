#ifndef BISECT_H
#define BISECT_H

struct commit_list;
struct repository;

/*
 * Find bisection. If something is found, `reaches` will be the number of
 * commits that the best commit reaches. `all` will be the count of
 * non-SAMETREE commits. If nothing is found, `list` will be NULL.
 * Otherwise, it will be either all non-SAMETREE commits or the single
 * best commit, as chosen by `find_all`.
 */
void find_bisection(struct commit_list **list, int *reaches, int *all,
		    int find_all);

struct commit_list *filter_skipped(struct commit_list *list,
				   struct commit_list **tried,
				   int show_all,
				   int *count,
				   int *skipped_first);

#define BISECT_SHOW_ALL		(1<<0)
#define REV_LIST_QUIET		(1<<1)

struct rev_list_info {
	struct rev_info *revs;
	int flags;
	int show_timestamp;
	int hdr_termination;
	const char *header_prefix;
};

/*
 * enum bisect_error represents the following return codes:
 * BISECT_OK: success code. Internally, it means that next
 * commit has been found (and possibly checked out) and it
 * should be tested.
 * BISECT_FAILED error code: default error code.
 * BISECT_ONLY_SKIPPED_LEFT error code: only skipped
 * commits left to be tested.
 * BISECT_MERGE_BASE_CHECK error code: merge base check failed.
 * BISECT_NO_TESTABLE_COMMIT error code: no testable commit found.
 * BISECT_INTERNAL_SUCCESS_1ST_BAD_FOUND early success code:
 * first term_bad commit found.
 * BISECT_INTERNAL_SUCCESS_MERGE_BASE early success
 * code: found merge base that should be tested.
 * Early success codes BISECT_INTERNAL_SUCCESS_1ST_BAD_FOUND and
 * BISECT_INTERNAL_SUCCESS_MERGE_BASE should be only internal codes.
 */
enum bisect_error {
	BISECT_OK = 0,
	BISECT_FAILED = -1,
	BISECT_ONLY_SKIPPED_LEFT = -2,
	BISECT_MERGE_BASE_CHECK = -3,
	BISECT_NO_TESTABLE_COMMIT = -4,
	BISECT_INTERNAL_SUCCESS_1ST_BAD_FOUND = -10,
	BISECT_INTERNAL_SUCCESS_MERGE_BASE = -11
};

enum bisect_error bisect_next_all(struct repository *r,
		    const char *prefix,
		    int no_checkout);

int estimate_bisect_steps(int all);

void read_bisect_terms(const char **bad, const char **good);

int bisect_clean_state(void);

#endif
