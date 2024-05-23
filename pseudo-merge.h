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

#endif
