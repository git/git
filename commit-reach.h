#ifndef COMMIT_REACH_H
#define COMMIT_REACH_H

#include "commit.h"
#include "commit-slab.h"

struct commit_list;
struct ref_filter;
struct object_id;
struct object_array;

int repo_get_merge_bases(struct repository *r,
			 struct commit *rev1,
			 struct commit *rev2,
			 struct commit_list **result);
int repo_get_merge_bases_many(struct repository *r,
			      struct commit *one, int n,
			      struct commit **twos,
			      struct commit_list **result);
/* To be used only when object flags after this call no longer matter */
int repo_get_merge_bases_many_dirty(struct repository *r,
				    struct commit *one, int n,
				    struct commit **twos,
				    struct commit_list **result);

int get_octopus_merge_bases(struct commit_list *in, struct commit_list **result);

int repo_is_descendant_of(struct repository *r,
			  struct commit *commit,
			  struct commit_list *with_commit);
int repo_in_merge_bases(struct repository *r,
			struct commit *commit,
			struct commit *reference);
int repo_in_merge_bases_many(struct repository *r,
			     struct commit *commit,
			     int nr_reference, struct commit **reference,
			     int ignore_missing_commits);

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
				 timestamp_t min_generation);
int can_all_from_reach(struct commit_list *from, struct commit_list *to,
		       int commit_date_cutoff);


/*
 * Return a list of commits containing the commits in the 'to' array
 * that are reachable from at least one commit in the 'from' array.
 * Also add the given 'flag' to each of the commits in the returned list.
 *
 * This method uses the PARENT1 and PARENT2 flags during its operation,
 * so be sure these flags are not set before calling the method.
 */
struct commit_list *get_reachable_subset(struct commit **from, int nr_from,
					 struct commit **to, int nr_to,
					 unsigned int reachable_flag);

struct ahead_behind_count {
	/**
	 * As input, the *_index members indicate which positions in
	 * the 'tips' array correspond to the tip and base of this
	 * comparison.
	 */
	size_t tip_index;
	size_t base_index;

	/**
	 * These values store the computed counts for each side of the
	 * symmetric difference:
	 *
	 * 'ahead' stores the number of commits reachable from the tip
	 * and not reachable from the base.
	 *
	 * 'behind' stores the number of commits reachable from the base
	 * and not reachable from the tip.
	 */
	unsigned int ahead;
	unsigned int behind;
};

/*
 * Given an array of commits and an array of ahead_behind_count pairs,
 * compute the ahead/behind counts for each pair.
 */
void ahead_behind(struct repository *r,
		  struct commit **commits, size_t commits_nr,
		  struct ahead_behind_count *counts, size_t counts_nr);

/*
 * For all tip commits, add 'mark' to their flags if and only if they
 * are reachable from one of the commits in 'bases'.
 */
void tips_reachable_from_bases(struct repository *r,
			       struct commit_list *bases,
			       struct commit **tips, size_t tips_nr,
			       int mark);

#endif
