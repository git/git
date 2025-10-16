#define DISABLE_SIGN_COMPARE_WARNINGS

#include "git-compat-util.h"
#include "repack.h"
#include "repository.h"
#include "hex.h"
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

	repo_for_each_pack(existing->repo, p) {
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

		ALLOC_GROW(geometry->pack,
			   geometry->pack_nr + 1,
			   geometry->pack_alloc);

		geometry->pack[geometry->pack_nr] = p;
		geometry->pack_nr++;
	}

	QSORT(geometry->pack, geometry->pack_nr, pack_geometry_cmp);
	strbuf_release(&buf);
}

void pack_geometry_split(struct pack_geometry *geometry)
{
	uint32_t i;
	uint32_t split;
	off_t total_size = 0;

	if (!geometry->pack_nr) {
		geometry->split = geometry->pack_nr;
		return;
	}

	/*
	 * First, count the number of packs (in descending order of size) which
	 * already form a geometric progression.
	 */
	for (i = geometry->pack_nr - 1; i > 0; i--) {
		struct packed_git *ours = geometry->pack[i];
		struct packed_git *prev = geometry->pack[i - 1];

		if (unsigned_mult_overflows(geometry->split_factor,
					    pack_geometry_weight(prev)))
			die(_("pack %s too large to consider in geometric "
			      "progression"),
			    prev->pack_name);

		if (pack_geometry_weight(ours) <
		    geometry->split_factor * pack_geometry_weight(prev))
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
		struct packed_git *p = geometry->pack[i];

		if (unsigned_add_overflows(total_size, pack_geometry_weight(p)))
			die(_("pack %s too large to roll up"), p->pack_name);
		total_size += pack_geometry_weight(p);
	}
	for (i = split; i < geometry->pack_nr; i++) {
		struct packed_git *ours = geometry->pack[i];

		if (unsigned_mult_overflows(geometry->split_factor,
					    total_size))
			die(_("pack %s too large to roll up"), ours->pack_name);

		if (pack_geometry_weight(ours) <
		    geometry->split_factor * total_size) {
			if (unsigned_add_overflows(total_size,
						   pack_geometry_weight(ours)))
				die(_("pack %s too large to roll up"),
				    ours->pack_name);

			split++;
			total_size += pack_geometry_weight(ours);
		} else
			break;
	}

	geometry->split = split;
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

void pack_geometry_remove_redundant(struct pack_geometry *geometry,
				    struct string_list *names,
				    struct existing_packs *existing,
				    const char *packdir)
{
	const struct git_hash_algo *algop = existing->repo->hash_algo;
	struct strbuf buf = STRBUF_INIT;
	uint32_t i;

	for (i = 0; i < geometry->split; i++) {
		struct packed_git *p = geometry->pack[i];
		if (string_list_has_string(names, hash_to_hex_algop(p->hash,
								    algop)))
			continue;

		strbuf_reset(&buf);
		strbuf_addstr(&buf, pack_basename(p));
		strbuf_strip_suffix(&buf, ".pack");

		if ((p->pack_keep) ||
		    (string_list_has_string(&existing->kept_packs, buf.buf)))
			continue;

		repack_remove_redundant_pack(existing->repo, packdir, buf.buf);
	}

	strbuf_release(&buf);
}

void pack_geometry_release(struct pack_geometry *geometry)
{
	if (!geometry)
		return;

	free(geometry->pack);
}
