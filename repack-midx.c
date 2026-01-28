#include "git-compat-util.h"
#include "repack.h"
#include "hash.h"
#include "hex.h"
#include "odb.h"
#include "oidset.h"
#include "pack-bitmap.h"
#include "refs.h"
#include "run-command.h"
#include "tempfile.h"

struct midx_snapshot_ref_data {
	struct repository *repo;
	struct tempfile *f;
	struct oidset seen;
	int preferred;
};

static int midx_snapshot_ref_one(const struct reference *ref, void *_data)
{
	struct midx_snapshot_ref_data *data = _data;
	const struct object_id *maybe_peeled = ref->oid;
	struct object_id peeled;

	if (!reference_get_peeled_oid(data->repo, ref, &peeled))
		maybe_peeled = &peeled;

	if (oidset_insert(&data->seen, maybe_peeled))
		return 0; /* already seen */

	if (odb_read_object_info(data->repo->objects, maybe_peeled, NULL) != OBJ_COMMIT)
		return 0;

	fprintf(data->f->fp, "%s%s\n", data->preferred ? "+" : "",
		oid_to_hex(maybe_peeled));

	return 0;
}

void midx_snapshot_refs(struct repository *repo, struct tempfile *f)
{
	struct midx_snapshot_ref_data data;

	data.repo = repo;
	data.f = f;
	data.preferred = 0;
	oidset_init(&data.seen, 0);

	if (!fdopen_tempfile(f, "w"))
		 die(_("could not open tempfile %s for writing"),
		     get_tempfile_path(f));

	data.preferred = 1;
	for_each_preferred_bitmap_tip(repo, midx_snapshot_ref_one, &data);
	data.preferred = 0;

	refs_for_each_ref(get_main_ref_store(repo),
			  midx_snapshot_ref_one, &data);

	if (close_tempfile_gently(f)) {
		int save_errno = errno;
		delete_tempfile(&f);
		errno = save_errno;
		die_errno(_("could not close refs snapshot tempfile"));
	}

	oidset_clear(&data.seen);
}

static int midx_has_unknown_packs(struct string_list *include,
				  struct pack_geometry *geometry,
				  struct existing_packs *existing)
{
	struct string_list_item *item;

	string_list_sort(include);

	for_each_string_list_item(item, &existing->midx_packs) {
		const char *pack_name = item->string;

		/*
		 * Determine whether or not each MIDX'd pack from the existing
		 * MIDX (if any) is represented in the new MIDX. For each pack
		 * in the MIDX, it must either be:
		 *
		 *  - In the "include" list of packs to be included in the new
		 *    MIDX. Note this function is called before the include
		 *    list is populated with any cruft pack(s).
		 *
		 *  - Below the geometric split line (if using pack geometry),
		 *    indicating that the pack won't be included in the new
		 *    MIDX, but its contents were rolled up as part of the
		 *    geometric repack.
		 *
		 *  - In the existing non-kept packs list (if not using pack
		 *    geometry), and marked as non-deleted.
		 */
		if (string_list_has_string(include, pack_name)) {
			continue;
		} else if (geometry) {
			struct strbuf buf = STRBUF_INIT;
			uint32_t j;

			for (j = 0; j < geometry->split; j++) {
				strbuf_reset(&buf);
				strbuf_addstr(&buf, pack_basename(geometry->pack[j]));
				strbuf_strip_suffix(&buf, ".pack");
				strbuf_addstr(&buf, ".idx");

				if (!strcmp(pack_name, buf.buf)) {
					strbuf_release(&buf);
					break;
				}
			}

			strbuf_release(&buf);

			if (j < geometry->split)
				continue;
		} else {
			struct string_list_item *item;

			item = string_list_lookup(&existing->non_kept_packs,
						  pack_name);
			if (item && !existing_pack_is_marked_for_deletion(item))
				continue;
		}

		/*
		 * If we got to this point, the MIDX includes some pack that we
		 * don't know about.
		 */
		return 1;
	}

	return 0;
}

static void midx_included_packs(struct string_list *include,
				struct repack_write_midx_opts *opts)
{
	struct existing_packs *existing = opts->existing;
	struct pack_geometry *geometry = opts->geometry;
	struct string_list *names = opts->names;
	struct string_list_item *item;
	struct strbuf buf = STRBUF_INIT;

	for_each_string_list_item(item, &existing->kept_packs) {
		strbuf_reset(&buf);
		strbuf_addf(&buf, "%s.idx", item->string);
		string_list_insert(include, buf.buf);
	}

	for_each_string_list_item(item, names) {
		strbuf_reset(&buf);
		strbuf_addf(&buf, "pack-%s.idx", item->string);
		string_list_insert(include, buf.buf);
	}

	if (geometry->split_factor) {
		uint32_t i;

		for (i = geometry->split; i < geometry->pack_nr; i++) {
			struct packed_git *p = geometry->pack[i];

			/*
			 * The multi-pack index never refers to packfiles part
			 * of an alternate object database, so we skip these.
			 * While git-multi-pack-index(1) would silently ignore
			 * them anyway, this allows us to skip executing the
			 * command completely when we have only non-local
			 * packfiles.
			 */
			if (!p->pack_local)
				continue;

			strbuf_reset(&buf);
			strbuf_addstr(&buf, pack_basename(p));
			strbuf_strip_suffix(&buf, ".pack");
			strbuf_addstr(&buf, ".idx");

			string_list_insert(include, buf.buf);
		}
	} else {
		for_each_string_list_item(item, &existing->non_kept_packs) {
			if (existing_pack_is_marked_for_deletion(item))
				continue;

			strbuf_reset(&buf);
			strbuf_addf(&buf, "%s.idx", item->string);
			string_list_insert(include, buf.buf);
		}
	}

	if (opts->midx_must_contain_cruft ||
	    midx_has_unknown_packs(include, geometry, existing)) {
		/*
		 * If there are one or more unknown pack(s) present (see
		 * midx_has_unknown_packs() for what makes a pack
		 * "unknown") in the MIDX before the repack, keep them
		 * as they may be required to form a reachability
		 * closure if the MIDX is bitmapped.
		 *
		 * For example, a cruft pack can be required to form a
		 * reachability closure if the MIDX is bitmapped and one
		 * or more of the bitmap's selected commits reaches a
		 * once-cruft object that was later made reachable.
		 */
		for_each_string_list_item(item, &existing->cruft_packs) {
			/*
			 * When doing a --geometric repack, there is no
			 * need to check for deleted packs, since we're
			 * by definition not doing an ALL_INTO_ONE
			 * repack (hence no packs will be deleted).
			 * Otherwise we must check for and exclude any
			 * packs which are enqueued for deletion.
			 *
			 * So we could omit the conditional below in the
			 * --geometric case, but doing so is unnecessary
			 *  since no packs are marked as pending
			 *  deletion (since we only call
			 *  `existing_packs_mark_for_deletion()` when
			 *  doing an all-into-one repack).
			 */
			if (existing_pack_is_marked_for_deletion(item))
				continue;

			strbuf_reset(&buf);
			strbuf_addf(&buf, "%s.idx", item->string);
			string_list_insert(include, buf.buf);
		}
	} else {
		/*
		 * Modern versions of Git (with the appropriate
		 * configuration setting) will write new copies of
		 * once-cruft objects when doing a --geometric repack.
		 *
		 * If the MIDX has no cruft pack, new packs written
		 * during a --geometric repack will not rely on the
		 * cruft pack to form a reachability closure, so we can
		 * avoid including them in the MIDX in that case.
		 */
		;
	}

	strbuf_release(&buf);
}

static void remove_redundant_bitmaps(struct string_list *include,
				     const char *packdir)
{
	struct strbuf path = STRBUF_INIT;
	struct string_list_item *item;
	size_t packdir_len;

	strbuf_addstr(&path, packdir);
	strbuf_addch(&path, '/');
	packdir_len = path.len;

	/*
	 * Remove any pack bitmaps corresponding to packs which are now
	 * included in the MIDX.
	 */
	for_each_string_list_item(item, include) {
		strbuf_addstr(&path, item->string);
		strbuf_strip_suffix(&path, ".idx");
		strbuf_addstr(&path, ".bitmap");

		if (unlink(path.buf) && errno != ENOENT)
			warning_errno(_("could not remove stale bitmap: %s"),
				      path.buf);

		strbuf_setlen(&path, packdir_len);
	}
	strbuf_release(&path);
}

int write_midx_included_packs(struct repack_write_midx_opts *opts)
{
	struct child_process cmd = CHILD_PROCESS_INIT;
	struct string_list include = STRING_LIST_INIT_DUP;
	struct string_list_item *item;
	struct packed_git *preferred = pack_geometry_preferred_pack(opts->geometry);
	FILE *in;
	int ret = 0;

	midx_included_packs(&include, opts);
	if (!include.nr)
		goto done;

	cmd.in = -1;
	cmd.git_cmd = 1;

	strvec_push(&cmd.args, "multi-pack-index");
	strvec_pushl(&cmd.args, "write", "--stdin-packs", NULL);

	if (opts->show_progress)
		strvec_push(&cmd.args, "--progress");
	else
		strvec_push(&cmd.args, "--no-progress");

	if (opts->write_bitmaps)
		strvec_push(&cmd.args, "--bitmap");

	if (preferred)
		strvec_pushf(&cmd.args, "--preferred-pack=%s",
			     pack_basename(preferred));
	else if (opts->names->nr) {
		/* The largest pack was repacked, meaning that either
		 * one or two packs exist depending on whether the
		 * repository has a cruft pack or not.
		 *
		 * Select the non-cruft one as preferred to encourage
		 * pack-reuse among packs containing reachable objects
		 * over unreachable ones.
		 *
		 * (Note we could write multiple packs here if
		 * `--max-pack-size` was given, but any one of them
		 * will suffice, so pick the first one.)
		 */
		for_each_string_list_item(item, opts->names) {
			struct generated_pack *pack = item->util;
			if (generated_pack_has_ext(pack, ".mtimes"))
				continue;

			strvec_pushf(&cmd.args, "--preferred-pack=pack-%s.pack",
				     item->string);
			break;
		}
	} else {
		/*
		 * No packs were kept, and no packs were written. The
		 * only thing remaining are .keep packs (unless
		 * --pack-kept-objects was given).
		 *
		 * Set the `--preferred-pack` arbitrarily here.
		 */
		;
	}

	if (opts->refs_snapshot)
		strvec_pushf(&cmd.args, "--refs-snapshot=%s",
			     opts->refs_snapshot);

	ret = start_command(&cmd);
	if (ret)
		goto done;

	in = xfdopen(cmd.in, "w");
	for_each_string_list_item(item, &include)
		fprintf(in, "%s\n", item->string);
	fclose(in);

	ret = finish_command(&cmd);
done:
	if (!ret && opts->write_bitmaps)
		remove_redundant_bitmaps(&include, opts->packdir);

	string_list_clear(&include, 0);

	return ret;
}
