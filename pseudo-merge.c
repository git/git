#define USE_THE_REPOSITORY_VARIABLE
#define DISABLE_SIGN_COMPARE_WARNINGS

#include "git-compat-util.h"
#include "pseudo-merge.h"
#include "date.h"
#include "oid-array.h"
#include "strbuf.h"
#include "config.h"
#include "string-list.h"
#include "refs.h"
#include "pack-bitmap.h"
#include "commit.h"
#include "alloc.h"
#include "progress.h"
#include "hex.h"

#define DEFAULT_PSEUDO_MERGE_DECAY 1.0
#define DEFAULT_PSEUDO_MERGE_MAX_MERGES 64
#define DEFAULT_PSEUDO_MERGE_SAMPLE_RATE 1
#define DEFAULT_PSEUDO_MERGE_THRESHOLD approxidate("1.week.ago")
#define DEFAULT_PSEUDO_MERGE_STABLE_THRESHOLD approxidate("1.month.ago")
#define DEFAULT_PSEUDO_MERGE_STABLE_SIZE 512

static double gitexp(double base, int exp)
{
	double result = 1;
	while (1) {
		if (exp % 2)
			result *= base;
		exp >>= 1;
		if (!exp)
			break;
		base *= base;
	}
	return result;
}

static uint32_t pseudo_merge_group_size(const struct pseudo_merge_group *group,
					const struct pseudo_merge_matches *matches,
					uint32_t i)
{
	double C = 0.0f;
	uint32_t n;

	/*
	 * The size of pseudo-merge groups decays according to a power series,
	 * which looks like:
	 *
	 *   f(n) = C * n^-k
	 *
	 * , where 'n' is the n-th pseudo-merge group, 'f(n)' is its size, 'k'
	 * is the decay rate, and 'C' is a scaling value.
	 *
	 * The value of C depends on the number of groups, decay rate, and total
	 * number of commits. It is computed such that if there are M and N
	 * total groups and commits, respectively, that:
	 *
	 *   N = f(0) + f(1) + ... f(M-1)
	 *
	 * Rearranging to isolate C, we get:
	 *
	 *   N = \sum_{n=1}^M C / n^k
	 *
	 *   N / C = \sum_{n=1}^M n^-k
	 *
	 *   C = N / \sum_{n=1}^M n^-k
	 *
	 * For example, if we have a decay rate of 'k' being equal to 1.5, 'N'
	 * total commits equal to 10,000, and 'M' being equal to 6 groups, then
	 * the (rounded) group sizes are:
	 *
	 *   { 5469, 1934, 1053, 684, 489, 372 }
	 *
	 * increasing the number of total groups, say to 10, scales the group
	 * sizes appropriately:
	 *
	 *   { 5012, 1772, 964, 626, 448, 341, 271, 221, 186, 158 }
	 */
	for (n = 0; n < group->max_merges; n++)
		C += 1.0 / gitexp(n + 1, group->decay);
	C = matches->unstable_nr / C;

	return (uint32_t)((C / gitexp(i + 1, group->decay)) + 0.5);
}

static void pseudo_merge_group_init(struct pseudo_merge_group *group)
{
	memset(group, 0, sizeof(struct pseudo_merge_group));

	strmap_init_with_options(&group->matches, NULL, 1);

	group->decay = DEFAULT_PSEUDO_MERGE_DECAY;
	group->max_merges = DEFAULT_PSEUDO_MERGE_MAX_MERGES;
	group->sample_rate = DEFAULT_PSEUDO_MERGE_SAMPLE_RATE;
	group->threshold = DEFAULT_PSEUDO_MERGE_THRESHOLD;
	group->stable_threshold = DEFAULT_PSEUDO_MERGE_STABLE_THRESHOLD;
	group->stable_size = DEFAULT_PSEUDO_MERGE_STABLE_SIZE;
}

void pseudo_merge_group_release(struct pseudo_merge_group *group)
{
	struct hashmap_iter iter;
	struct strmap_entry *e;

	regfree(group->pattern);
	free(group->pattern);

	strmap_for_each_entry(&group->matches, &iter, e) {
		struct pseudo_merge_matches *matches = e->value;
		free(matches->stable);
		free(matches->unstable);
		free(matches);
	}
	strmap_clear(&group->matches, 0);

	free(group->merges);
}

static int pseudo_merge_config(const char *var, const char *value,
			       const struct config_context *ctx,
			       void *cb_data)
{
	struct string_list *list = cb_data;
	struct string_list_item *item;
	struct pseudo_merge_group *group;
	struct strbuf buf = STRBUF_INIT;
	const char *sub, *key;
	size_t sub_len;
	int ret = 0;

	if (parse_config_key(var, "bitmappseudomerge", &sub, &sub_len, &key))
		goto done;

	if (!sub_len)
		goto done;

	strbuf_add(&buf, sub, sub_len);

	item = string_list_lookup(list, buf.buf);
	if (!item) {
		item = string_list_insert(list, buf.buf);

		item->util = xmalloc(sizeof(struct pseudo_merge_group));
		pseudo_merge_group_init(item->util);
	}

	group = item->util;

	if (!strcmp(key, "pattern")) {
		struct strbuf re = STRBUF_INIT;

		free(group->pattern);
		if (*value != '^')
			strbuf_addch(&re, '^');
		strbuf_addstr(&re, value);

		group->pattern = xcalloc(1, sizeof(regex_t));
		if (regcomp(group->pattern, re.buf, REG_EXTENDED))
			die(_("failed to load pseudo-merge regex for %s: '%s'"),
			    sub, re.buf);

		strbuf_release(&re);
	} else if (!strcmp(key, "decay")) {
		group->decay = git_config_double(var, value, ctx->kvi);
		if (group->decay < 0) {
			warning(_("%s must be non-negative, using default"), var);
			group->decay = DEFAULT_PSEUDO_MERGE_DECAY;
		}
	} else if (!strcmp(key, "samplerate")) {
		group->sample_rate = git_config_double(var, value, ctx->kvi);
		if (!(0 <= group->sample_rate && group->sample_rate <= 1)) {
			warning(_("%s must be between 0 and 1, using default"), var);
			group->sample_rate = DEFAULT_PSEUDO_MERGE_SAMPLE_RATE;
		}
	} else if (!strcmp(key, "threshold")) {
		if (git_config_expiry_date(&group->threshold, var, value)) {
			ret = -1;
			goto done;
		}
	} else if (!strcmp(key, "maxmerges")) {
		group->max_merges = git_config_int(var, value, ctx->kvi);
		if (group->max_merges < 0) {
			warning(_("%s must be non-negative, using default"), var);
			group->max_merges = DEFAULT_PSEUDO_MERGE_MAX_MERGES;
		}
	} else if (!strcmp(key, "stablethreshold")) {
		if (git_config_expiry_date(&group->stable_threshold, var, value)) {
			ret = -1;
			goto done;
		}
	} else if (!strcmp(key, "stablesize")) {
		group->stable_size = git_config_int(var, value, ctx->kvi);
		if (group->stable_size <= 0) {
			warning(_("%s must be positive, using default"), var);
			group->stable_size = DEFAULT_PSEUDO_MERGE_STABLE_SIZE;
		}
	}

done:
	strbuf_release(&buf);

	return ret;
}

void load_pseudo_merges_from_config(struct repository *r,
				    struct string_list *list)
{
	struct string_list_item *item;

	repo_config(r, pseudo_merge_config, list);

	for_each_string_list_item(item, list) {
		struct pseudo_merge_group *group = item->util;
		if (!group->pattern)
			die(_("pseudo-merge group '%s' missing required pattern"),
			    item->string);
		if (group->threshold < group->stable_threshold)
			die(_("pseudo-merge group '%s' has unstable threshold "
			      "before stable one"), item->string);
	}
}

static int find_pseudo_merge_group_for_ref(const char *refname,
					   const char *referent UNUSED,
					   const struct object_id *oid,
					   int flags UNUSED,
					   void *_data)
{
	struct bitmap_writer *writer = _data;
	struct object_id peeled;
	struct commit *c;
	uint32_t i;
	int has_bitmap;

	if (!peel_iterated_oid(the_repository, oid, &peeled))
		oid = &peeled;

	c = lookup_commit(the_repository, oid);
	if (!c)
		return 0;
	if (!packlist_find(writer->to_pack, oid))
		return 0;

	has_bitmap = bitmap_writer_has_bitmapped_object_id(writer, oid);

	for (i = 0; i < writer->pseudo_merge_groups.nr; i++) {
		struct pseudo_merge_group *group;
		struct pseudo_merge_matches *matches;
		struct strbuf group_name = STRBUF_INIT;
		regmatch_t captures[16];
		size_t j;

		group = writer->pseudo_merge_groups.items[i].util;
		if (regexec(group->pattern, refname, ARRAY_SIZE(captures),
			    captures, 0))
			continue;

		if (captures[ARRAY_SIZE(captures) - 1].rm_so != -1)
			warning(_("pseudo-merge regex from config has too many capture "
				  "groups (max=%"PRIuMAX")"),
				(uintmax_t)ARRAY_SIZE(captures) - 2);

		for (j = !!group->pattern->re_nsub; j < ARRAY_SIZE(captures); j++) {
			regmatch_t *match = &captures[j];
			if (match->rm_so == -1)
				continue;

			if (group_name.len)
				strbuf_addch(&group_name, '-');

			strbuf_add(&group_name, refname + match->rm_so,
				   match->rm_eo - match->rm_so);
		}

		matches = strmap_get(&group->matches, group_name.buf);
		if (!matches) {
			matches = xcalloc(1, sizeof(*matches));
			strmap_put(&group->matches, group_name.buf,
				   matches);
		}

		if (c->date <= group->stable_threshold) {
			ALLOC_GROW(matches->stable, matches->stable_nr + 1,
				   matches->stable_alloc);
			matches->stable[matches->stable_nr++] = c;
		} else if (c->date <= group->threshold && !has_bitmap) {
			ALLOC_GROW(matches->unstable, matches->unstable_nr + 1,
				   matches->unstable_alloc);
			matches->unstable[matches->unstable_nr++] = c;
		}

		strbuf_release(&group_name);
	}

	return 0;
}

static struct commit *push_pseudo_merge(struct pseudo_merge_group *group)
{
	struct commit *merge;

	ALLOC_GROW(group->merges, group->merges_nr + 1, group->merges_alloc);

	merge = alloc_commit_node(the_repository);
	merge->object.parsed = 1;
	merge->object.flags |= BITMAP_PSEUDO_MERGE;

	group->merges[group->merges_nr++] = merge;

	return merge;
}

static struct pseudo_merge_commit_idx *pseudo_merge_idx(kh_oid_map_t *pseudo_merge_commits,
							const struct object_id *oid)

{
	struct pseudo_merge_commit_idx *pmc;
	int hash_ret;
	khiter_t hash_pos = kh_put_oid_map(pseudo_merge_commits, *oid,
					   &hash_ret);

	if (hash_ret) {
		CALLOC_ARRAY(pmc, 1);
		kh_value(pseudo_merge_commits, hash_pos) = pmc;
	} else {
		pmc = kh_value(pseudo_merge_commits, hash_pos);
	}

	return pmc;
}

#define MIN_PSEUDO_MERGE_SIZE 8

static void select_pseudo_merges_1(struct bitmap_writer *writer,
				   struct pseudo_merge_group *group,
				   struct pseudo_merge_matches *matches)
{
	uint32_t i, j;
	uint32_t stable_merges_nr;

	if (!matches->stable_nr && !matches->unstable_nr)
		return; /* all tips in this group already have bitmaps */

	stable_merges_nr = matches->stable_nr / group->stable_size;
	if (matches->stable_nr % group->stable_size)
		stable_merges_nr++;

	/* make stable_merges_nr pseudo merges for stable commits */
	for (i = 0, j = 0; i < stable_merges_nr; i++) {
		struct commit *merge;
		struct commit_list **p;

		merge = push_pseudo_merge(group);
		p = &merge->parents;

		/*
		 * For each pseudo-merge created above, add parents to the
		 * allocated commit node from the stable set of commits
		 * (un-bitmapped, newer than the stable threshold).
		 */
		do {
			struct commit *c;
			struct pseudo_merge_commit_idx *pmc;

			if (j >= matches->stable_nr)
				break;

			c = matches->stable[j++];
			/*
			 * Here and below, make sure that we keep our mapping of
			 * commits -> pseudo-merge(s) which include the key'd
			 * commit up-to-date.
			 */
			pmc = pseudo_merge_idx(writer->pseudo_merge_commits,
					       &c->object.oid);

			ALLOC_GROW(pmc->pseudo_merge, pmc->nr + 1, pmc->alloc);

			pmc->pseudo_merge[pmc->nr++] = writer->pseudo_merges_nr;
			p = commit_list_append(c, p);
		} while (j % group->stable_size);

		if (merge->parents) {
			bitmap_writer_push_commit(writer, merge, 1);
			writer->pseudo_merges_nr++;
		}
	}

	/* make up to group->max_merges pseudo merges for unstable commits */
	for (i = 0, j = 0; i < group->max_merges; i++) {
		struct commit *merge;
		struct commit_list **p;
		uint32_t size, end;

		merge = push_pseudo_merge(group);
		p = &merge->parents;

		size = pseudo_merge_group_size(group, matches, i);
		end = size < MIN_PSEUDO_MERGE_SIZE ? matches->unstable_nr : j + size;

		/*
		 * For each pseudo-merge commit created above, add parents to
		 * the allocated commit node from the unstable set of commits
		 * (newer than the stable threshold).
		 *
		 * Account for the sample rate, since not every candidate from
		 * the set of stable commits will be included as a pseudo-merge
		 * parent.
		 */
		for (; j < end && j < matches->unstable_nr; j++) {
			struct commit *c = matches->unstable[j];
			struct pseudo_merge_commit_idx *pmc;

			if (j % (uint32_t)(1.0 / group->sample_rate))
				continue;

			pmc = pseudo_merge_idx(writer->pseudo_merge_commits,
					       &c->object.oid);

			ALLOC_GROW(pmc->pseudo_merge, pmc->nr + 1, pmc->alloc);

			pmc->pseudo_merge[pmc->nr++] = writer->pseudo_merges_nr;
			p = commit_list_append(c, p);
		}

		if (merge->parents) {
			bitmap_writer_push_commit(writer, merge, 1);
			writer->pseudo_merges_nr++; }
		if (end >= matches->unstable_nr)
			break;
	}
}

static int commit_date_cmp(const void *va, const void *vb)
{
	timestamp_t a = (*(const struct commit **)va)->date;
	timestamp_t b = (*(const struct commit **)vb)->date;

	if (a < b)
		return -1;
	else if (a > b)
		return 1;
	return 0;
}

static void sort_pseudo_merge_matches(struct pseudo_merge_matches *matches)
{
	QSORT(matches->stable, matches->stable_nr, commit_date_cmp);
	QSORT(matches->unstable, matches->unstable_nr, commit_date_cmp);
}

void select_pseudo_merges(struct bitmap_writer *writer)
{
	struct progress *progress = NULL;
	uint32_t i;

	if (!writer->pseudo_merge_groups.nr)
		return;

	if (writer->show_progress)
		progress = start_progress("Selecting pseudo-merge commits",
					  writer->pseudo_merge_groups.nr);

	refs_for_each_ref(get_main_ref_store(the_repository),
			  find_pseudo_merge_group_for_ref, writer);

	for (i = 0; i < writer->pseudo_merge_groups.nr; i++) {
		struct pseudo_merge_group *group;
		struct hashmap_iter iter;
		struct strmap_entry *e;

		group = writer->pseudo_merge_groups.items[i].util;
		strmap_for_each_entry(&group->matches, &iter, e) {
			struct pseudo_merge_matches *matches = e->value;

			sort_pseudo_merge_matches(matches);

			select_pseudo_merges_1(writer, group, matches);
		}

		display_progress(progress, i + 1);
	}

	stop_progress(&progress);
}

void free_pseudo_merge_map(struct pseudo_merge_map *pm)
{
	uint32_t i;
	for (i = 0; i < pm->nr; i++) {
		ewah_pool_free(pm->v[i].commits);
		ewah_pool_free(pm->v[i].bitmap);
	}
	free(pm->v);
}

struct pseudo_merge_commit_ext {
	uint32_t nr;
	const unsigned char *ptr;
};

static int pseudo_merge_ext_at(const struct pseudo_merge_map *pm,
			       struct pseudo_merge_commit_ext *ext, size_t at)
{
	if (at >= pm->map_size)
		return error(_("extended pseudo-merge read out-of-bounds "
			       "(%"PRIuMAX" >= %"PRIuMAX")"),
			     (uintmax_t)at, (uintmax_t)pm->map_size);
	if (at + 4 >= pm->map_size)
		return error(_("extended pseudo-merge entry is too short "
			       "(%"PRIuMAX" >= %"PRIuMAX")"),
			     (uintmax_t)(at + 4), (uintmax_t)pm->map_size);

	ext->nr = get_be32(pm->map + at);
	ext->ptr = pm->map + at + sizeof(uint32_t);

	return 0;
}

struct ewah_bitmap *pseudo_merge_bitmap(const struct pseudo_merge_map *pm,
					struct pseudo_merge *merge)
{
	if (!merge->loaded_commits)
		BUG("cannot use unloaded pseudo-merge bitmap");

	if (!merge->loaded_bitmap) {
		size_t at = merge->bitmap_at;

		merge->bitmap = read_bitmap(pm->map, pm->map_size, &at);
		merge->loaded_bitmap = 1;
	}

	return merge->bitmap;
}

struct pseudo_merge *use_pseudo_merge(const struct pseudo_merge_map *pm,
				      struct pseudo_merge *merge)
{
	if (!merge->loaded_commits) {
		size_t pos = merge->at;

		merge->commits = read_bitmap(pm->map, pm->map_size, &pos);
		merge->bitmap_at = pos;
		merge->loaded_commits = 1;
	}
	return merge;
}

static struct pseudo_merge *pseudo_merge_at(const struct pseudo_merge_map *pm,
					    struct object_id *oid,
					    size_t want)
{
	size_t lo = 0;
	size_t hi = pm->nr;

	while (lo < hi) {
		size_t mi = lo + (hi - lo) / 2;
		size_t got = pm->v[mi].at;

		if (got == want)
			return use_pseudo_merge(pm, &pm->v[mi]);
		else if (got < want)
			hi = mi;
		else
			lo = mi + 1;
	}

	warning(_("could not find pseudo-merge for commit %s at offset %"PRIuMAX),
		oid_to_hex(oid), (uintmax_t)want);

	return NULL;
}

struct pseudo_merge_commit {
	uint32_t commit_pos;
	uint64_t pseudo_merge_ofs;
};

#define PSEUDO_MERGE_COMMIT_RAWSZ (sizeof(uint32_t)+sizeof(uint64_t))

static void read_pseudo_merge_commit_at(struct pseudo_merge_commit *merge,
					const unsigned char *at)
{
	merge->commit_pos = get_be32(at);
	merge->pseudo_merge_ofs = get_be64(at + sizeof(uint32_t));
}

static int nth_pseudo_merge_ext(const struct pseudo_merge_map *pm,
				struct pseudo_merge_commit_ext *ext,
				struct pseudo_merge_commit *merge,
				uint32_t n)
{
	size_t ofs;

	if (n >= ext->nr)
		return error(_("extended pseudo-merge lookup out-of-bounds "
			       "(%"PRIu32" >= %"PRIu32")"), n, ext->nr);

	ofs = get_be64(ext->ptr + st_mult(n, sizeof(uint64_t)));
	if (ofs >= pm->map_size)
		return error(_("out-of-bounds read: (%"PRIuMAX" >= %"PRIuMAX")"),
			     (uintmax_t)ofs, (uintmax_t)pm->map_size);

	read_pseudo_merge_commit_at(merge, pm->map + ofs);

	return 0;
}

static unsigned apply_pseudo_merge(const struct pseudo_merge_map *pm,
				   struct pseudo_merge *merge,
				   struct bitmap *result,
				   struct bitmap *roots)
{
	if (merge->satisfied)
		return 0;

	if (!ewah_bitmap_is_subset(merge->commits, roots ? roots : result))
		return 0;

	bitmap_or_ewah(result, pseudo_merge_bitmap(pm, merge));
	if (roots)
		bitmap_or_ewah(roots, pseudo_merge_bitmap(pm, merge));
	merge->satisfied = 1;

	return 1;
}

static int pseudo_merge_commit_cmp(const void *va, const void *vb)
{
	struct pseudo_merge_commit merge;
	uint32_t key = *(uint32_t*)va;

	read_pseudo_merge_commit_at(&merge, vb);

	if (key < merge.commit_pos)
		return -1;
	if (key > merge.commit_pos)
		return 1;
	return 0;
}

static struct pseudo_merge_commit *find_pseudo_merge(const struct pseudo_merge_map *pm,
						     uint32_t pos)
{
	if (!pm->commits_nr)
		return NULL;

	return bsearch(&pos, pm->commits, pm->commits_nr,
		       PSEUDO_MERGE_COMMIT_RAWSZ, pseudo_merge_commit_cmp);
}

int apply_pseudo_merges_for_commit(const struct pseudo_merge_map *pm,
				   struct bitmap *result,
				   struct commit *commit, uint32_t commit_pos)
{
	struct pseudo_merge *merge;
	struct pseudo_merge_commit *merge_commit;
	int ret = 0;

	merge_commit = find_pseudo_merge(pm, commit_pos);
	if (!merge_commit)
		return 0;

	if (merge_commit->pseudo_merge_ofs & ((uint64_t)1<<63)) {
		struct pseudo_merge_commit_ext ext = { 0 };
		off_t ofs = merge_commit->pseudo_merge_ofs & ~((uint64_t)1<<63);
		uint32_t i;

		if (pseudo_merge_ext_at(pm, &ext, ofs) < -1) {
			warning(_("could not read extended pseudo-merge table "
				  "for commit %s"),
				oid_to_hex(&commit->object.oid));
			return ret;
		}

		for (i = 0; i < ext.nr; i++) {
			if (nth_pseudo_merge_ext(pm, &ext, merge_commit, i) < 0)
				return ret;

			merge = pseudo_merge_at(pm, &commit->object.oid,
						merge_commit->pseudo_merge_ofs);

			if (!merge)
				return ret;

			if (apply_pseudo_merge(pm, merge, result, NULL))
				ret++;
		}
	} else {
		merge = pseudo_merge_at(pm, &commit->object.oid,
					merge_commit->pseudo_merge_ofs);

		if (!merge)
			return ret;

		if (apply_pseudo_merge(pm, merge, result, NULL))
			ret++;
	}

	if (ret)
		cascade_pseudo_merges(pm, result, NULL);

	return ret;
}

int cascade_pseudo_merges(const struct pseudo_merge_map *pm,
			  struct bitmap *result,
			  struct bitmap *roots)
{
	unsigned any_satisfied;
	int ret = 0;

	do {
		struct pseudo_merge *merge;
		uint32_t i;

		any_satisfied = 0;

		for (i = 0; i < pm->nr; i++) {
			merge = use_pseudo_merge(pm, &pm->v[i]);
			if (apply_pseudo_merge(pm, merge, result, roots)) {
				any_satisfied |= 1;
				ret++;
			}
		}
	} while (any_satisfied);

	return ret;
}

struct pseudo_merge *pseudo_merge_for_parents(const struct pseudo_merge_map *pm,
					      struct bitmap *parents)
{
	struct pseudo_merge *match = NULL;
	size_t i;

	if (!pm->nr)
		return NULL;

	/*
	 * NOTE: this loop is quadratic in the worst-case (where no
	 * matching pseudo-merge bitmaps are found), but in practice
	 * this is OK for a few reasons:
	 *
	 *   - Rejecting pseudo-merge bitmaps that do not match the
	 *     given commit is done quickly (i.e. `bitmap_equals_ewah()`
	 *     returns early when we know the two bitmaps aren't equal.
	 *
	 *   - Already matched pseudo-merge bitmaps (which we track with
	 *     the `->satisfied` bit here) are skipped as potential
	 *     candidates.
	 *
	 *   - The number of pseudo-merges should be small (in the
	 *     hundreds for most repositories).
	 *
	 * If in the future this semi-quadratic behavior does become a
	 * problem, another approach would be to keep track of which
	 * pseudo-merges are still "viable" after enumerating the
	 * pseudo-merge commit's parents:
	 *
	 *   - A pseudo-merge bitmap becomes non-viable when the bit(s)
	 *     corresponding to one or more parent(s) of the given
	 *     commit are not set in a candidate pseudo-merge's commits
	 *     bitmap.
	 *
	 *   - After processing all bits, enumerate the remaining set of
	 *     viable pseudo-merge bitmaps, and check that their
	 *     popcount() matches the number of parents in the given
	 *     commit.
	 */
	for (i = 0; i < pm->nr; i++) {
		struct pseudo_merge *candidate = use_pseudo_merge(pm, &pm->v[i]);
		if (!candidate || candidate->satisfied)
			continue;
		if (!bitmap_equals_ewah(parents, candidate->commits))
			continue;

		match = candidate;
		match->satisfied = 1;
		break;
	}

	return match;
}
