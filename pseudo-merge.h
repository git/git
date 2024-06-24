#ifndef PSEUDO_MERGE_H
#define PSEUDO_MERGE_H

#include "git-compat-util.h"
#include "strmap.h"
#include "khash.h"
#include "ewah/ewok.h"

struct commit;
struct string_list;
struct bitmap_index;
struct bitmap_writer;

/*
 * A pseudo-merge group tracks the set of non-bitmapped reference tips
 * that match the given pattern.
 *
 * Within those matches, they are further segmented by separating
 * consecutive capture groups with '-' dash character capture groups
 * with '-' dash characters.
 *
 * Those groups are then ordered by committer date and partitioned
 * into individual pseudo-merge(s) according to the decay, max_merges,
 * sample_rate, and threshold parameters.
 */
struct pseudo_merge_group {
	regex_t *pattern;

	/* capture group(s) -> struct pseudo_merge_matches */
	struct strmap matches;

	/*
	 * The individual pseudo-merge(s) that are generated from the
	 * above array of matches, partitioned according to the below
	 * parameters.
	 */
	struct commit **merges;
	size_t merges_nr;
	size_t merges_alloc;

	/*
	 * Pseudo-merge grouping parameters. See git-config(1) for
	 * more information.
	 */
	double decay;
	int max_merges;
	double sample_rate;
	int stable_size;
	timestamp_t threshold;
	timestamp_t stable_threshold;
};

struct pseudo_merge_matches {
	struct commit **stable;
	struct commit **unstable;
	size_t stable_nr, stable_alloc;
	size_t unstable_nr, unstable_alloc;
};

/*
 * Read the repository's configuration:
 *
 *   - bitmapPseudoMerge.<name>.pattern
 *   - bitmapPseudoMerge.<name>.decay
 *   - bitmapPseudoMerge.<name>.sampleRate
 *   - bitmapPseudoMerge.<name>.threshold
 *   - bitmapPseudoMerge.<name>.maxMerges
 *   - bitmapPseudoMerge.<name>.stableThreshold
 *   - bitmapPseudoMerge.<name>.stableSize
 *
 * and populates the given `list` with pseudo-merge groups. String
 * entry keys are the pseudo-merge group names, and the values are
 * pointers to the pseudo_merge_group structure itself.
 */
void load_pseudo_merges_from_config(struct string_list *list);

/*
 * A pseudo-merge commit index (pseudo_merge_commit_idx) maps a
 * particular (non-pseudo-merge) commit to the list of pseudo-merge(s)
 * it appears in.
 */
struct pseudo_merge_commit_idx {
	uint32_t *pseudo_merge;
	size_t nr, alloc;
};

/*
 * Selects pseudo-merges from a list of commits, populating the given
 * string_list of pseudo-merge groups.
 *
 * Populates the pseudo_merge_commits map with a commit_idx
 * corresponding to each commit in the list. Counts the total number
 * of pseudo-merges generated.
 *
 * Optionally shows a progress meter.
 */
void select_pseudo_merges(struct bitmap_writer *writer,
			  struct commit **commits, size_t commits_nr);

/*
 * Represents a serialized view of a file containing pseudo-merge(s)
 * (see Documentation/technical/bitmap-format.txt for a specification
 * of the format).
 */
struct pseudo_merge_map {
	/*
	 * An array of pseudo-merge(s), lazily loaded from the .bitmap
	 * file.
	 */
	struct pseudo_merge *v;
	size_t nr;
	size_t commits_nr;

	/*
	 * Pointers into a memory-mapped view of the .bitmap file:
	 *
	 *   - map: the beginning of the .bitmap file
	 *   - commits: the beginning of the pseudo-merge commit index
	 *   - map_size: the size of the .bitmap file
	 */
	const unsigned char *map;
	const unsigned char *commits;

	size_t map_size;
};

/*
 * An individual pseudo-merge, storing a pair of lazily-loaded
 * bitmaps:
 *
 *  - commits: the set of commit(s) that are part of the pseudo-merge
 *  - bitmap: the set of object(s) reachable from the above set of
 *    commits.
 *
 * The `at` and `bitmap_at` fields are used to store the locations of
 * each of the above bitmaps in the .bitmap file.
 */
struct pseudo_merge {
	struct ewah_bitmap *commits;
	struct ewah_bitmap *bitmap;

	off_t at;
	off_t bitmap_at;

	/*
	 * `satisfied` indicates whether the given pseudo-merge has been
	 * used.
	 *
	 * `loaded_commits` and `loaded_bitmap` indicate whether the
	 * respective bitmaps have been loaded and read from the
	 * .bitmap file.
	 */
	unsigned satisfied : 1,
		 loaded_commits : 1,
		 loaded_bitmap : 1;
};

/*
 * Frees the given pseudo-merge map, releasing any memory held by (a)
 * parsed EWAH bitmaps, or (b) the array of pseudo-merges itself. Does
 * not free the memory-mapped view of the .bitmap file.
 */
void free_pseudo_merge_map(struct pseudo_merge_map *pm);

/*
 * Loads the bitmap corresponding to the given pseudo-merge from the
 * map, if it has not already been loaded.
 */
struct ewah_bitmap *pseudo_merge_bitmap(const struct pseudo_merge_map *pm,
					struct pseudo_merge *merge);

/*
 * Loads the pseudo-merge and its commits bitmap from the given
 * pseudo-merge map, if it has not already been loaded.
 */
struct pseudo_merge *use_pseudo_merge(const struct pseudo_merge_map *pm,
				      struct pseudo_merge *merge);

/*
 * Applies pseudo-merge(s) containing the given commit to the bitmap
 * "result".
 *
 * If any pseudo-merge(s) were satisfied, returns the number
 * satisfied, otherwise returns 0. If any were satisfied, the
 * remaining unsatisfied pseudo-merges are cascaded (see below).
 */
int apply_pseudo_merges_for_commit(const struct pseudo_merge_map *pm,
				   struct bitmap *result,
				   struct commit *commit, uint32_t commit_pos);

/*
 * Applies pseudo-merge(s) which are satisfied according to the
 * current bitmap in result (or roots, see below). If any
 * pseudo-merges were satisfied, repeat the process over unsatisfied
 * pseudo-merge commits until no more pseudo-merges are satisfied.
 *
 * Result is the bitmap to which the pseudo-merge(s) are applied.
 * Roots (if given) is a bitmap of the traversal tip(s) for either
 * side of a reachability traversal.
 *
 * Roots may given instead of a populated results bitmap at the
 * beginning of a traversal on either side where the reachability
 * closure over tips is not yet known.
 */
int cascade_pseudo_merges(const struct pseudo_merge_map *pm,
			  struct bitmap *result,
			  struct bitmap *roots);

/*
 * Returns a pseudo-merge which contains the exact set of commits
 * listed in the "parents" bitamp, or NULL if none could be found.
 */
struct pseudo_merge *pseudo_merge_for_parents(const struct pseudo_merge_map *pm,
					      struct bitmap *parents);

#endif
