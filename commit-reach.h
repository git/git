#ifndef __COMMIT_REACH_H__
#define __COMMIT_REACH_H__

#include "commit-slab.h"

struct commit;
struct commit_list;
struct contains_cache;
struct ref_filter;

struct commit_list *get_merge_bases_many(struct commit *one,
					 int n,
					 struct commit **twos);
struct commit_list *get_merge_bases_many_dirty(struct commit *one,
					       int n,
					       struct commit **twos);
struct commit_list *get_merge_bases(struct commit *one, struct commit *two);
struct commit_list *get_octopus_merge_bases(struct commit_list *in);

/* To be used only when object flags after this call no longer matter */
struct commit_list *get_merge_bases_many_dirty(struct commit *one, int n, struct commit **twos);

int is_descendant_of(struct commit *commit, struct commit_list *with_commit);
int in_merge_bases_many(struct commit *commit, int nr_reference, struct commit **reference);
int in_merge_bases(struct commit *commit, struct commit *reference);

/*
 * Takes a list of commits and returns a new list where those
 * have been removed that can be reached from other commits in
 * the list. It is useful for, e.g., reducing the commits
 * randomly thrown at the git-merge command and removing
 * redundant commits that the user shouldn't have given to it.
 *
 * This function destroys the STALE bit of the commit objects'
 * flags.
 */
struct commit_list *reduce_heads(struct commit_list *heads);

/*
 * Like `reduce_heads()`, except it replaces the list. Use this
 * instead of `foo = reduce_heads(foo);` to avoid memory leaks.
 */
void reduce_heads_replace(struct commit_list **heads);

int ref_newer(const struct object_id *new_oid, const struct object_id *old_oid);

/*
 * Unknown has to be "0" here, because that's the default value for
 * contains_cache slab entries that have not yet been assigned.
 */
enum contains_result {
	CONTAINS_UNKNOWN = 0,
	CONTAINS_NO,
	CONTAINS_YES
};

define_commit_slab(contains_cache, enum contains_result);

int commit_contains(struct ref_filter *filter, struct commit *commit,
		    struct commit_list *list, struct contains_cache *cache);

/*
 * Determine if every commit in 'from' can reach at least one commit
 * that is marked with 'with_flag'. As we traverse, use 'assign_flag'
 * as a marker for commits that are already visited. Do not walk
 * commits with date below 'min_commit_date' or generation below
 * 'min_generation'.
 */
int can_all_from_reach_with_flag(struct object_array *from,
				 unsigned int with_flag,
				 unsigned int assign_flag,
				 time_t min_commit_date,
				 uint32_t min_generation);
int can_all_from_reach(struct commit_list *from, struct commit_list *to,
		       int commit_date_cutoff);

#endif
