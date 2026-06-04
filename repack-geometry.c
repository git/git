#define DISABLE_SIGN_COMPARE_WARNINGS

#include "git-compat-util.h"
#include "repack.h"
#include "repository.h"
#include "hex.h"
#include "midx.h"
#include "packfile.h"

static uint32_t pack_geometry_weight(struct packed_git *p)
{
	if (open_pack_index(p))
		die(_("cannot open index for %s"), p->pack_name);
	return p->num_objects;
}

static int pack_geometry_cmp(const void *va, const void *vb)
{
	uint32_t aw = pack_geometry_weight(*(struct packed_git **)va),
		 bw = pack_geometry_weight(*(struct packed_git **)vb);

	if (aw < bw)
		return -1;
	if (aw > bw)
		return 1;
	return 0;
}

void pack_geometry_init(struct pack_geometry *geometry,
			struct existing_packs *existing,
			const struct pack_objects_args *args)
{
	struct packed_git *p;
	struct strbuf buf = STRBUF_INIT;
	struct multi_pack_index *m = get_multi_pack_index(existing->source);

	repo_for_each_pack(existing->repo, p) {
		if (geometry->midx_layer_threshold_set && m &&
		    p->multi_pack_index) {
			/*
			 * When writing MIDX layers incrementally,
			 * ignore packs unless they are in the most
			 * recent MIDX layer *and* there are at least
			 * 'midx_layer_threshold' packs in that layer.
			 *
			 * Otherwise 'p' is either in an older layer, or
			 * the youngest layer does not have enough packs
			 * to consider its packs as candidates for
			 * repacking. In either of those cases we want
			 * to ignore the pack.
			 */
			if (m->num_packs < geometry->midx_layer_threshold ||
			    !midx_layer_contains_pack(m, pack_basename(p)))
				continue;
		}

		if (args->local && !p->pack_local)
			/*
			 * When asked to only repack local packfiles we skip
			 * over any packfiles that are borrowed from alternate
			 * object directories.
			 */
			continue;

		if (!args->pack_kept_objects) {
			/*
			 * Any pack that has its pack_keep bit set will
			 * appear in existing->kept_packs below, but
			 * this saves us from doing a more expensive
			 * check.
			 */
			if (p->pack_keep)
				continue;

			/*
			 * The pack may be kept via the --keep-pack
			 * option; check 'existing->kept_packs' to
			 * determine whether to ignore it.
			 */
			strbuf_reset(&buf);
			strbuf_addstr(&buf, pack_basename(p));
			strbuf_strip_suffix(&buf, ".pack");

			if (string_list_has_string(&existing->kept_packs, buf.buf))
				continue;
		}
		if (p->is_cruft)
			continue;

		if (p->pack_promisor) {
			ALLOC_GROW(geometry->promisor_pack,
				   geometry->promisor_pack_nr + 1,
				   geometry->promisor_pack_alloc);

			geometry->promisor_pack[geometry->promisor_pack_nr] = p;
			geometry->promisor_pack_nr++;
		} else {
			ALLOC_GROW(geometry->pack,
				   geometry->pack_nr + 1,
				   geometry->pack_alloc);

			geometry->pack[geometry->pack_nr] = p;
			geometry->pack_nr++;
		}
	}

	QSORT(geometry->pack, geometry->pack_nr, pack_geometry_cmp);
	QSORT(geometry->promisor_pack, geometry->promisor_pack_nr, pack_geometry_cmp);
	strbuf_release(&buf);
}

static uint32_t compute_pack_geometry_split(struct packed_git **pack, size_t pack_nr,
					    int split_factor)
{
	uint32_t i;
	uint32_t split;
	off_t total_size = 0;

	if (!pack_nr)
		return 0;

	/*
	 * First, count the number of packs (in descending order of size) which
	 * already form a geometric progression.
	 */
	for (i = pack_nr - 1; i > 0; i--) {
		struct packed_git *ours = pack[i];
		struct packed_git *prev = pack[i - 1];

		if (unsigned_mult_overflows(split_factor,
					    pack_geometry_weight(prev)))
			die(_("pack %s too large to consider in geometric "
			      "progression"),
			    prev->pack_name);

		if (pack_geometry_weight(ours) <
		    split_factor * pack_geometry_weight(prev))
			break;
	}

	split = i;

	if (split) {
		/*
		 * Move the split one to the right, since the top element in the
		 * last-compared pair can't be in the progression. Only do this
		 * when we split in the middle of the array (otherwise if we got
		 * to the end, then the split is in the right place).
		 */
		split++;
	}

	/*
	 * Then, anything to the left of 'split' must be in a new pack. But,
	 * creating that new pack may cause packs in the heavy half to no longer
	 * form a geometric progression.
	 *
	 * Compute an expected size of the new pack, and then determine how many
	 * packs in the heavy half need to be joined into it (if any) to restore
	 * the geometric progression.
	 */
	for (i = 0; i < split; i++) {
		struct packed_git *p = pack[i];

		if (unsigned_add_overflows(total_size, pack_geometry_weight(p)))
			die(_("pack %s too large to roll up"), p->pack_name);
		total_size += pack_geometry_weight(p);
	}
	for (i = split; i < pack_nr; i++) {
		struct packed_git *ours = pack[i];

		if (unsigned_mult_overflows(split_factor, total_size))
			die(_("pack %s too large to roll up"), ours->pack_name);

		if (pack_geometry_weight(ours) < split_factor * total_size) {
			if (unsigned_add_overflows(total_size,
						   pack_geometry_weight(ours)))
				die(_("pack %s too large to roll up"),
				    ours->pack_name);

			split++;
			total_size += pack_geometry_weight(ours);
		} else
			break;
	}

	return split;
}

void pack_geometry_split(struct pack_geometry *geometry)
{
	geometry->split = compute_pack_geometry_split(geometry->pack, geometry->pack_nr,
						      geometry->split_factor);
	geometry->promisor_split = compute_pack_geometry_split(geometry->promisor_pack,
							       geometry->promisor_pack_nr,
							       geometry->split_factor);
	for (uint32_t i = 0; i < geometry->split; i++) {
		struct packed_git *p = geometry->pack[i];
		/*
		 * During incremental MIDX/bitmap repacking, any packs
		 * included in the rollup are either (a) not MIDX'd, or
		 * (b) contained in the tip layer iff it has at least
		 * the threshold number of packs.
		 *
		 * In the latter case, we can safely conclude that the
		 * tip of the MIDX chain will be rewritten.
		 */
		if (p->multi_pack_index)
			geometry->midx_tip_rewritten = true;
	}
}

struct packed_git *pack_geometry_preferred_pack(struct pack_geometry *geometry)
{
	uint32_t i;

	if (!geometry) {
		/*
		 * No geometry means either an all-into-one repack (in which
		 * case there is only one pack left and it is the largest) or an
		 * incremental one.
		 *
		 * If repacking incrementally, then we could check the size of
		 * all packs to determine which should be preferred, but leave
		 * this for later.
		 */
		return NULL;
	}
	if (geometry->split == geometry->pack_nr)
		return NULL;

	/*
	 * The preferred pack is the largest pack above the split line. In
	 * other words, it is the largest pack that does not get rolled up in
	 * the geometric repack.
	 */
	for (i = geometry->pack_nr; i > geometry->split; i--)
		/*
		 * A pack that is not local would never be included in a
		 * multi-pack index. We thus skip over any non-local packs.
		 */
		if (geometry->pack[i - 1]->pack_local)
			return geometry->pack[i - 1];

	return NULL;
}

static void remove_redundant_packs(struct packed_git **pack,
				   uint32_t pack_nr,
				   struct string_list *names,
				   struct existing_packs *existing,
				   const char *packdir,
				   bool wrote_incremental_midx)
{
	const struct git_hash_algo *algop = existing->repo->hash_algo;
	struct strbuf buf = STRBUF_INIT;
	uint32_t i;

	for (i = 0; i < pack_nr; i++) {
		struct packed_git *p = pack[i];
		if (string_list_has_string(names, hash_to_hex_algop(p->hash,
								    algop)))
			continue;

		strbuf_reset(&buf);
		strbuf_addstr(&buf, pack_basename(p));
		strbuf_strip_suffix(&buf, ".pack");

		if ((p->pack_keep) ||
		    (string_list_has_string(&existing->kept_packs, buf.buf)))
			continue;

		repack_remove_redundant_pack(existing->repo, packdir, buf.buf,
					     wrote_incremental_midx);
	}

	strbuf_release(&buf);
}

void pack_geometry_remove_redundant(struct pack_geometry *geometry,
				    struct string_list *names,
				    struct existing_packs *existing,
				    const char *packdir,
				    bool wrote_incremental_midx)
{
	remove_redundant_packs(geometry->pack, geometry->split,
			       names, existing, packdir, wrote_incremental_midx);
	remove_redundant_packs(geometry->promisor_pack, geometry->promisor_split,
			       names, existing, packdir, wrote_incremental_midx);
}

void pack_geometry_release(struct pack_geometry *geometry)
{
	if (!geometry)
		return;

	free(geometry->pack);
	free(geometry->promisor_pack);
}
