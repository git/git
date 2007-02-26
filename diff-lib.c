/*
 * Copyright (C) 2005 Junio C Hamano
 */
#include "cache.h"
#include "quote.h"
#include "commit.h"
#include "diff.h"
#include "diffcore.h"
#include "revision.h"
#include "cache-tree.h"

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
			struct combine_diff_path *dpath;
			int num_compare_stages = 0;
			size_t path_len;

			path_len = ce_namelen(ce);

			dpath = xmalloc(combine_diff_path_size(5, path_len));
			dpath->path = (char *) &(dpath->parent[5]);

			dpath->next = NULL;
			dpath->len = path_len;
			memcpy(dpath->path, ce->name, path_len);
			dpath->path[path_len] = '\0';
			hashclr(dpath->sha1);
			memset(&(dpath->parent[0]), 0,
			       sizeof(struct combine_diff_parent)*5);

			if (lstat(ce->name, &st) < 0) {
				if (errno != ENOENT && errno != ENOTDIR) {
					perror(ce->name);
					continue;
				}
				if (silent_on_removed)
					continue;
			}
			else
				dpath->mode = canon_mode(st.st_mode);

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
					hashcpy(dpath->parent[stage-2].sha1, nce->sha1);
					dpath->parent[stage-2].mode =
						canon_mode(mode);
					dpath->parent[stage-2].status =
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
				show_combined_diff(dpath, 2,
						   revs->dense_combined_merges,
						   revs);
				free(dpath);
				continue;
			}
			free(dpath);
			dpath = NULL;

			/*
			 * Show the diff for the 'ce' if we found the one
			 * from the desired stage.
			 */
			diff_unmerge(&revs->diffopt, ce->name, 0, null_sha1);
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

/*
 * diff-index
 */

/* A file entry went away or appeared */
static void diff_index_show_file(struct rev_info *revs,
				 const char *prefix,
				 struct cache_entry *ce,
				 unsigned char *sha1, unsigned int mode)
{
	diff_addremove(&revs->diffopt, prefix[0], ntohl(mode),
		       sha1, ce->name, NULL);
}

static int get_stat_data(struct cache_entry *ce,
			 unsigned char **sha1p,
			 unsigned int *modep,
			 int cached, int match_missing)
{
	unsigned char *sha1 = ce->sha1;
	unsigned int mode = ce->ce_mode;

	if (!cached) {
		static unsigned char no_sha1[20];
		int changed;
		struct stat st;
		if (lstat(ce->name, &st) < 0) {
			if (errno == ENOENT && match_missing) {
				*sha1p = sha1;
				*modep = mode;
				return 0;
			}
			return -1;
		}
		changed = ce_match_stat(ce, &st, 0);
		if (changed) {
			mode = ce_mode_from_stat(ce, st.st_mode);
			sha1 = no_sha1;
		}
	}

	*sha1p = sha1;
	*modep = mode;
	return 0;
}

static void show_new_file(struct rev_info *revs,
			  struct cache_entry *new,
			  int cached, int match_missing)
{
	unsigned char *sha1;
	unsigned int mode;

	/* New file in the index: it might actually be different in
	 * the working copy.
	 */
	if (get_stat_data(new, &sha1, &mode, cached, match_missing) < 0)
		return;

	diff_index_show_file(revs, "+", new, sha1, mode);
}

static int show_modified(struct rev_info *revs,
			 struct cache_entry *old,
			 struct cache_entry *new,
			 int report_missing,
			 int cached, int match_missing)
{
	unsigned int mode, oldmode;
	unsigned char *sha1;

	if (get_stat_data(new, &sha1, &mode, cached, match_missing) < 0) {
		if (report_missing)
			diff_index_show_file(revs, "-", old,
					     old->sha1, old->ce_mode);
		return -1;
	}

	if (revs->combine_merges && !cached &&
	    (hashcmp(sha1, old->sha1) || hashcmp(old->sha1, new->sha1))) {
		struct combine_diff_path *p;
		int pathlen = ce_namelen(new);

		p = xmalloc(combine_diff_path_size(2, pathlen));
		p->path = (char *) &p->parent[2];
		p->next = NULL;
		p->len = pathlen;
		memcpy(p->path, new->name, pathlen);
		p->path[pathlen] = 0;
		p->mode = ntohl(mode);
		hashclr(p->sha1);
		memset(p->parent, 0, 2 * sizeof(struct combine_diff_parent));
		p->parent[0].status = DIFF_STATUS_MODIFIED;
		p->parent[0].mode = ntohl(new->ce_mode);
		hashcpy(p->parent[0].sha1, new->sha1);
		p->parent[1].status = DIFF_STATUS_MODIFIED;
		p->parent[1].mode = ntohl(old->ce_mode);
		hashcpy(p->parent[1].sha1, old->sha1);
		show_combined_diff(p, 2, revs->dense_combined_merges, revs);
		free(p);
		return 0;
	}

	oldmode = old->ce_mode;
	if (mode == oldmode && !hashcmp(sha1, old->sha1) &&
	    !revs->diffopt.find_copies_harder)
		return 0;

	mode = ntohl(mode);
	oldmode = ntohl(oldmode);

	diff_change(&revs->diffopt, oldmode, mode,
		    old->sha1, sha1, old->name, NULL);
	return 0;
}

static int diff_cache(struct rev_info *revs,
		      struct cache_entry **ac, int entries,
		      const char **pathspec,
		      int cached, int match_missing)
{
	while (entries) {
		struct cache_entry *ce = *ac;
		int same = (entries > 1) && ce_same_name(ce, ac[1]);

		if (!ce_path_match(ce, pathspec))
			goto skip_entry;

		switch (ce_stage(ce)) {
		case 0:
			/* No stage 1 entry? That means it's a new file */
			if (!same) {
				show_new_file(revs, ce, cached, match_missing);
				break;
			}
			/* Show difference between old and new */
			show_modified(revs, ac[1], ce, 1,
				      cached, match_missing);
			break;
		case 1:
			/* No stage 3 (merge) entry?
			 * That means it's been deleted.
			 */
			if (!same) {
				diff_index_show_file(revs, "-", ce,
						     ce->sha1, ce->ce_mode);
				break;
			}
			/* We come here with ce pointing at stage 1
			 * (original tree) and ac[1] pointing at stage
			 * 3 (unmerged).  show-modified with
			 * report-missing set to false does not say the
			 * file is deleted but reports true if work
			 * tree does not have it, in which case we
			 * fall through to report the unmerged state.
			 * Otherwise, we show the differences between
			 * the original tree and the work tree.
			 */
			if (!cached &&
			    !show_modified(revs, ce, ac[1], 0,
					   cached, match_missing))
				break;
			diff_unmerge(&revs->diffopt, ce->name,
				     ntohl(ce->ce_mode), ce->sha1);
			break;
		case 3:
			diff_unmerge(&revs->diffopt, ce->name,
				     0, null_sha1);
			break;

		default:
			die("impossible cache entry stage");
		}

skip_entry:
		/*
		 * Ignore all the different stages for this file,
		 * we've handled the relevant cases now.
		 */
		do {
			ac++;
			entries--;
		} while (entries && ce_same_name(ce, ac[0]));
	}
	return 0;
}

/*
 * This turns all merge entries into "stage 3". That guarantees that
 * when we read in the new tree (into "stage 1"), we won't lose sight
 * of the fact that we had unmerged entries.
 */
static void mark_merge_entries(void)
{
	int i;
	for (i = 0; i < active_nr; i++) {
		struct cache_entry *ce = active_cache[i];
		if (!ce_stage(ce))
			continue;
		ce->ce_flags |= htons(CE_STAGEMASK);
	}
}

int run_diff_index(struct rev_info *revs, int cached)
{
	int ret;
	struct object *ent;
	struct tree *tree;
	const char *tree_name;
	int match_missing = 0;

	/* 
	 * Backward compatibility wart - "diff-index -m" does
	 * not mean "do not ignore merges", but totally different.
	 */
	if (!revs->ignore_merges)
		match_missing = 1;

	if (read_cache() < 0) {
		perror("read_cache");
		return -1;
	}
	mark_merge_entries();

	ent = revs->pending.objects[0].item;
	tree_name = revs->pending.objects[0].name;
	tree = parse_tree_indirect(ent->sha1);
	if (!tree)
		return error("bad tree object %s", tree_name);
	if (read_tree(tree, 1, revs->prune_data))
		return error("unable to read tree object %s", tree_name);
	ret = diff_cache(revs, active_cache, active_nr, revs->prune_data,
			 cached, match_missing);
	diffcore_std(&revs->diffopt);
	diff_flush(&revs->diffopt);
	return ret;
}

int do_diff_cache(const unsigned char *tree_sha1, struct diff_options *opt)
{
	struct tree *tree;
	struct rev_info revs;
	int i;
	struct cache_entry **dst;
	struct cache_entry *last = NULL;

	/*
	 * This is used by git-blame to run diff-cache internally;
	 * it potentially needs to repeatedly run this, so we will
	 * start by removing the higher order entries the last round
	 * left behind.
	 */
	dst = active_cache;
	for (i = 0; i < active_nr; i++) {
		struct cache_entry *ce = active_cache[i];
		if (ce_stage(ce)) {
			if (last && !strcmp(ce->name, last->name))
				continue;
			cache_tree_invalidate_path(active_cache_tree,
						   ce->name);
			last = ce;
			ce->ce_mode = 0;
			ce->ce_flags &= ~htons(CE_STAGEMASK);
		}
		*dst++ = ce;
	}
	active_nr = dst - active_cache;

	init_revisions(&revs, NULL);
	revs.prune_data = opt->paths;
	tree = parse_tree_indirect(tree_sha1);
	if (!tree)
		die("bad tree object %s", sha1_to_hex(tree_sha1));
	if (read_tree(tree, 1, opt->paths))
		return error("unable to read tree %s", sha1_to_hex(tree_sha1));
	return diff_cache(&revs, active_cache, active_nr, revs.prune_data,
			  1, 0);
}
