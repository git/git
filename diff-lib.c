/*
 * Copyright (C) 2005 Junio C Hamano
 */
#include "cache.h"
#include "quote.h"
#include "commit.h"
#include "diff.h"
#include "diffcore.h"
#include "revision.h"

/*
 * diff-files
 */

int run_diff_files(struct rev_info *revs, int silent_on_removed)
{
	int entries, i;
	int diff_unmerged_stage = revs->max_count;

	if (diff_unmerged_stage < 0)
		diff_unmerged_stage = 2;
	entries = read_cache();
	if (entries < 0) {
		perror("read_cache");
		return -1;
	}
	for (i = 0; i < entries; i++) {
		struct stat st;
		unsigned int oldmode, newmode;
		struct cache_entry *ce = active_cache[i];
		int changed;

		if (!ce_path_match(ce, revs->prune_data))
			continue;

		if (ce_stage(ce)) {
			struct {
				struct combine_diff_path p;
				struct combine_diff_parent filler[5];
			} combine;
			int num_compare_stages = 0;

			combine.p.next = NULL;
			combine.p.len = ce_namelen(ce);
			combine.p.path = xmalloc(combine.p.len + 1);
			memcpy(combine.p.path, ce->name, combine.p.len);
			combine.p.path[combine.p.len] = 0;
			combine.p.mode = 0;
			memset(combine.p.sha1, 0, 20);
			memset(&combine.p.parent[0], 0,
			       sizeof(combine.filler));

			while (i < entries) {
				struct cache_entry *nce = active_cache[i];
				int stage;

				if (strcmp(ce->name, nce->name))
					break;

				/* Stage #2 (ours) is the first parent,
				 * stage #3 (theirs) is the second.
				 */
				stage = ce_stage(nce);
				if (2 <= stage) {
					int mode = ntohl(nce->ce_mode);
					num_compare_stages++;
					memcpy(combine.p.parent[stage-2].sha1,
					       nce->sha1, 20);
					combine.p.parent[stage-2].mode =
						canon_mode(mode);
					combine.p.parent[stage-2].status =
						DIFF_STATUS_MODIFIED;
				}

				/* diff against the proper unmerged stage */
				if (stage == diff_unmerged_stage)
					ce = nce;
				i++;
			}
			/*
			 * Compensate for loop update
			 */
			i--;

			if (revs->combine_merges && num_compare_stages == 2) {
				show_combined_diff(&combine.p, 2,
						   revs->dense_combined_merges,
						   revs);
				free(combine.p.path);
				continue;
			}
			free(combine.p.path);

			/*
			 * Show the diff for the 'ce' if we found the one
			 * from the desired stage.
			 */
			diff_unmerge(&revs->diffopt, ce->name);
			if (ce_stage(ce) != diff_unmerged_stage)
				continue;
		}

		if (lstat(ce->name, &st) < 0) {
			if (errno != ENOENT && errno != ENOTDIR) {
				perror(ce->name);
				continue;
			}
			if (silent_on_removed)
				continue;
			diff_addremove(&revs->diffopt, '-', ntohl(ce->ce_mode),
				       ce->sha1, ce->name, NULL);
			continue;
		}
		changed = ce_match_stat(ce, &st, 0);
		if (!changed && !revs->diffopt.find_copies_harder)
			continue;
		oldmode = ntohl(ce->ce_mode);

		newmode = canon_mode(st.st_mode);
		if (!trust_executable_bit &&
		    S_ISREG(newmode) && S_ISREG(oldmode) &&
		    ((newmode ^ oldmode) == 0111))
			newmode = oldmode;
		diff_change(&revs->diffopt, oldmode, newmode,
			    ce->sha1, (changed ? null_sha1 : ce->sha1),
			    ce->name, NULL);

	}
	diffcore_std(&revs->diffopt);
	diff_flush(&revs->diffopt);
	return 0;
}

