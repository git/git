#include "git-compat-util.h"
#include "repack.h"
#include "hash.h"
#include "hex.h"
#include "lockfile.h"
#include "midx.h"
#include "odb.h"
#include "oidset.h"
#include "pack-bitmap.h"
#include "path.h"
#include "refs.h"
#include "run-command.h"
#include "tempfile.h"
#include "trace2.h"

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

static void repack_prepare_midx_command(struct child_process *cmd,
					struct repack_write_midx_opts *opts,
					const char *verb)
{
	cmd->git_cmd = 1;

	strvec_pushl(&cmd->args, "multi-pack-index", verb, NULL);

	if (opts->show_progress)
		strvec_push(&cmd->args, "--progress");
	else
		strvec_push(&cmd->args, "--no-progress");

	if (opts->write_bitmaps)
		strvec_push(&cmd->args, "--bitmap");
}

static int repack_fill_midx_stdin_packs(struct child_process *cmd,
					struct string_list *include,
					struct string_list *out)
{
	struct string_list_item *item;
	FILE *in;
	int ret;

	cmd->in = -1;
	if (out)
		cmd->out = -1;

	strvec_push(&cmd->args, "--stdin-packs");

	ret = start_command(cmd);
	if (ret)
		return ret;

	in = xfdopen(cmd->in, "w");
	for_each_string_list_item(item, include)
		fprintf(in, "%s\n", item->string);
	fclose(in);

	if (out) {
		struct strbuf buf = STRBUF_INIT;
		FILE *outf = xfdopen(cmd->out, "r");

		while (strbuf_getline(&buf, outf) != EOF)
			string_list_append(out, buf.buf);
		strbuf_release(&buf);

		fclose(outf);
	}

	return finish_command(cmd);
}

static int write_midx_included_packs(struct repack_write_midx_opts *opts)
{
	struct child_process cmd = CHILD_PROCESS_INIT;
	struct string_list include = STRING_LIST_INIT_DUP;
	struct string_list_item *item;
	struct packed_git *preferred = pack_geometry_preferred_pack(opts->geometry);
	int ret = 0;

	midx_included_packs(&include, opts);
	if (!include.nr)
		goto done;

	repack_prepare_midx_command(&cmd, opts, "write");

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

	ret = repack_fill_midx_stdin_packs(&cmd, &include, NULL);
done:
	if (!ret && opts->write_bitmaps)
		remove_redundant_bitmaps(&include, opts->packdir);

	string_list_clear(&include, 0);

	return ret;
}

struct midx_compaction_step {
	union {
		struct multi_pack_index *copy;
		struct string_list write;
		struct {
			struct multi_pack_index *from;
			struct multi_pack_index *to;
		} compact;
	} u;

	uint32_t objects_nr;
	char *csum;

	enum {
		MIDX_COMPACTION_STEP_UNKNOWN,
		MIDX_COMPACTION_STEP_COPY,
		MIDX_COMPACTION_STEP_WRITE,
		MIDX_COMPACTION_STEP_COMPACT,
	} type;
};

static const char *midx_compaction_step_base(const struct midx_compaction_step *step)
{
	switch (step->type) {
	case MIDX_COMPACTION_STEP_UNKNOWN:
		BUG("cannot use UNKNOWN step as a base");
	case MIDX_COMPACTION_STEP_COPY:
		return midx_get_checksum_hex(step->u.copy);
	case MIDX_COMPACTION_STEP_WRITE:
		BUG("cannot use WRITE step as a base");
	case MIDX_COMPACTION_STEP_COMPACT:
		return midx_get_checksum_hex(step->u.compact.to);
	default:
		BUG("unhandled midx compaction step type %d", step->type);
	}
}

static int midx_compaction_step_exec_copy(struct midx_compaction_step *step)
{
	step->csum = xstrdup(midx_get_checksum_hex(step->u.copy));
	return 0;
}

static int midx_compaction_step_exec_write(struct midx_compaction_step *step,
					   struct repack_write_midx_opts *opts,
					   const char *base)
{
	struct child_process cmd = CHILD_PROCESS_INIT;
	struct string_list hash = STRING_LIST_INIT_DUP;
	struct string_list_item *item;
	const char *preferred_pack = NULL;
	int ret = 0;

	if (!step->u.write.nr) {
		ret = error(_("no packs to write MIDX during compaction"));
		goto out;
	}

	for_each_string_list_item(item, &step->u.write) {
		if (item->util)
			preferred_pack = item->string;
	}

	repack_prepare_midx_command(&cmd, opts, "write");
	strvec_pushl(&cmd.args, "--incremental", "--checksum-only", NULL);
	strvec_pushf(&cmd.args, "--base=%s", base ? base : "none");

	if (preferred_pack) {
		struct strbuf buf = STRBUF_INIT;

		strbuf_addstr(&buf, preferred_pack);
		strbuf_strip_suffix(&buf, ".idx");
		strbuf_addstr(&buf, ".pack");

		strvec_pushf(&cmd.args, "--preferred-pack=%s", buf.buf);

		strbuf_release(&buf);
	}

	ret = repack_fill_midx_stdin_packs(&cmd, &step->u.write, &hash);
	if (hash.nr != 1) {
		ret = error(_("expected exactly one line during MIDX write, "
			      "got: %"PRIuMAX),
			    (uintmax_t)hash.nr);
		goto out;
	}

	step->csum = xstrdup(hash.items[0].string);

out:
	string_list_clear(&hash, 0);

	return ret;
}

static int midx_compaction_step_exec_compact(struct midx_compaction_step *step,
					     struct repack_write_midx_opts *opts)
{
	struct child_process cmd = CHILD_PROCESS_INIT;
	struct strbuf buf = STRBUF_INIT;
	FILE *out = NULL;
	int ret;

	repack_prepare_midx_command(&cmd, opts, "compact");
	strvec_pushl(&cmd.args, "--incremental", "--checksum-only",
		     midx_get_checksum_hex(step->u.compact.from),
		     midx_get_checksum_hex(step->u.compact.to), NULL);

	cmd.out = -1;

	ret = start_command(&cmd);
	if (ret)
		goto out;

	out = xfdopen(cmd.out, "r");
	while (strbuf_getline_lf(&buf, out) != EOF) {
		if (step->csum) {
			ret = error(_("unexpected MIDX output: '%s'"), buf.buf);
			goto out;
		}
		step->csum = strbuf_detach(&buf, NULL);
	}

	ret = finish_command(&cmd);

out:
	if (out)
		fclose(out);
	strbuf_release(&buf);

	return ret;
}

static int midx_compaction_step_exec(struct midx_compaction_step *step,
				     struct repack_write_midx_opts *opts,
				     const char *base)
{
	switch (step->type) {
	case MIDX_COMPACTION_STEP_UNKNOWN:
		BUG("cannot execute UNKNOWN midx compaction step");
	case MIDX_COMPACTION_STEP_COPY:
		return midx_compaction_step_exec_copy(step);
	case MIDX_COMPACTION_STEP_WRITE:
		return midx_compaction_step_exec_write(step, opts, base);
	case MIDX_COMPACTION_STEP_COMPACT:
		return midx_compaction_step_exec_compact(step, opts);
	default:
		BUG("unhandled midx compaction step type %d", step->type);
	}
}

static void midx_compaction_step_release(struct midx_compaction_step *step)
{
	if (step->type == MIDX_COMPACTION_STEP_WRITE)
		string_list_clear(&step->u.write, 0);
	free(step->csum);
}

static int repack_make_midx_compaction_plan(struct repack_write_midx_opts *opts,
					    struct midx_compaction_step **steps_p,
					    size_t *steps_nr_p)
{
	struct multi_pack_index *m;
	struct midx_compaction_step *steps = NULL;
	struct midx_compaction_step step = { 0 };
	struct strbuf buf = STRBUF_INIT;
	size_t steps_nr = 0, steps_alloc = 0;
	uint32_t i;
	int ret = 0;

	trace2_region_enter("repack", "make_midx_compaction_plan",
			    opts->existing->repo);

	odb_reprepare(opts->existing->repo->objects);
	m = get_multi_pack_index(opts->existing->source);

	for (i = 0; m && i < m->num_packs + m->num_packs_in_base; i++) {
		if (prepare_midx_pack(m, i)) {
			ret = error(_("could not load pack %"PRIu32" from MIDX"),
				    i);
			goto out;
		}
	}

	trace2_region_enter("repack", "steps:write", opts->existing->repo);

	/*
	 * The first MIDX in the resulting chain is always going to be
	 * new.
	 *
	 * At a minimum, it will include all of the newly written packs.
	 * If there is an existing MIDX whose tip layer contains packs
	 * that were repacked, it will also include any of its packs
	 * which were *not* rolled up as part of the geometric repack
	 * (if any), and the previous tip will be replaced.
	 *
	 * It may grow to include the packs from zero or more MIDXs from
	 * the old chain, beginning either at the old tip (if the MIDX
	 * was *not* rewritten) or the old tip's base MIDX layer
	 * (otherwise).
	 */
	step.type = MIDX_COMPACTION_STEP_WRITE;
	string_list_init_dup(&step.u.write);

	for (i = 0; i < opts->names->nr; i++) {
		strbuf_reset(&buf);
		strbuf_addf(&buf, "pack-%s.idx", opts->names->items[i].string);
		string_list_append(&step.u.write, buf.buf);

		trace2_data_string("repack", opts->existing->repo,
				   "include:fresh",
				   step.u.write.items[step.u.write.nr - 1].string);
	}
	for (i = 0; i < opts->geometry->split; i++) {
		struct packed_git *p = opts->geometry->pack[i];
		if (unsigned_add_overflows(step.objects_nr, p->num_objects)) {
			ret = error(_("too many objects in MIDX compaction step"));
			goto out;
		}

		step.objects_nr += p->num_objects;
	}
	trace2_data_intmax("repack", opts->existing->repo,
			   "include:fresh:objects_nr",
			   (uintmax_t)step.objects_nr);

	/*
	 * Now handle any existing packs which were *not* rewritten.
	 *
	 * The list of packs in opts->geometry only contains MIDX'd
	 * packs from the newest layer when that layer has more than
	 * 'repack.midxNewLayerThreshold' number of packs.
	 *
	 * If the MIDX tip was rewritten (that is, one or more of those
	 * packs appear below the split line), then add all packs above
	 * the split line to the new layer, as the old one is no longer
	 * usable.
	 *
	 * If the MIDX tip was not rewritten (that is, all MIDX'd packs
	 * from the youngest layer appear below the split line, or were
	 * not included in the geometric repack at all because there
	 * were too few of them), ignore them since we'll retain the
	 * existing layer as-is.
	 */
	for (i = opts->geometry->split; i < opts->geometry->pack_nr; i++) {
		struct packed_git *p = opts->geometry->pack[i];
		struct string_list_item *item;

		strbuf_reset(&buf);
		strbuf_addstr(&buf, pack_basename(p));
		strbuf_strip_suffix(&buf, ".pack");
		strbuf_addstr(&buf, ".idx");

		if (p->multi_pack_index &&
		    !opts->geometry->midx_tip_rewritten) {
			trace2_data_string("repack", opts->existing->repo,
					   "exclude:unmodified", buf.buf);
			continue;
		}

		trace2_data_string("repack", opts->existing->repo,
				   "include:unmodified", buf.buf);
		trace2_data_string("repack", opts->existing->repo,
				   "include:unmodified:midx",
				   p->multi_pack_index ? "true" : "false");

		item = string_list_append(&step.u.write, buf.buf);
		if (p->multi_pack_index || i == opts->geometry->pack_nr - 1)
			item->util = (void *)1; /* mark as preferred */

		if (unsigned_add_overflows(step.objects_nr, p->num_objects)) {
			ret = error(_("too many objects in MIDX compaction step"));
			goto out;
		}

		step.objects_nr += p->num_objects;
	}
	trace2_data_intmax("repack", opts->existing->repo,
			   "include:unmodified:objects_nr",
			   (uintmax_t)step.objects_nr);

	/*
	 * If the MIDX tip was rewritten, then we no longer consider it
	 * a candidate for compaction, since it will not exist in the
	 * MIDX chain being built.
	 */
	if (opts->geometry->midx_tip_rewritten)
		m = m->base_midx;

	trace2_data_string("repack", opts->existing->repo, "midx:rewrote-tip",
			   opts->geometry->midx_tip_rewritten ? "true" : "false");

	trace2_region_enter("repack", "compact", opts->existing->repo);

	/*
	 * Compact additional MIDX layers into this proposed one until
	 * the merging condition is violated.
	 */
	while (m) {
		uint32_t preferred_pack_idx;

		trace2_data_string("repack", opts->existing->repo,
				   "candidate", midx_get_checksum_hex(m));

		if (step.objects_nr < m->num_objects / opts->midx_split_factor) {
			/*
			 * Stop compacting MIDX layer as soon as the
			 * merged size is less than half the size of the
			 * next layer in the chain.
			 */
			trace2_data_string("repack", opts->existing->repo,
					   "compact", "violated");
			trace2_data_intmax("repack", opts->existing->repo,
					   "objects_nr",
					   (uintmax_t)step.objects_nr);
			trace2_data_intmax("repack", opts->existing->repo,
					   "next_objects_nr",
					   (uintmax_t)m->num_objects);
			trace2_data_intmax("repack", opts->existing->repo,
					   "split_factor",
					   (uintmax_t)opts->midx_split_factor);

			break;
		}

		if (midx_preferred_pack(m, &preferred_pack_idx) < 0) {
			ret = error(_("could not find preferred pack for MIDX "
				      "%s"), midx_get_checksum_hex(m));
			goto out;
		}

		for (i = 0; i < m->num_packs; i++) {
			struct string_list_item *item;
			uint32_t pack_int_id = i + m->num_packs_in_base;
			struct packed_git *p = nth_midxed_pack(m, pack_int_id);

			strbuf_reset(&buf);
			strbuf_addstr(&buf, pack_basename(p));
			strbuf_strip_suffix(&buf, ".pack");
			strbuf_addstr(&buf, ".idx");

			trace2_data_string("repack", opts->existing->repo,
					   "midx:pack", buf.buf);

			item = string_list_append(&step.u.write, buf.buf);
			if (pack_int_id == preferred_pack_idx)
				item->util = (void *)1; /* mark as preferred */
		}

		if (unsigned_add_overflows(step.objects_nr, m->num_objects)) {
			ret = error(_("too many objects in MIDX compaction step"));
			goto out;
		}
		step.objects_nr += m->num_objects;

		m = m->base_midx;
	}

	if (step.u.write.nr > 0) {
		/*
		 * As long as there is at least one new pack to write
		 * (and thus the MIDX is non-empty), add it to the plan.
		 */
		ALLOC_GROW(steps, steps_nr + 1, steps_alloc);
		steps[steps_nr++] = step;
	}

	trace2_data_intmax("repack", opts->existing->repo,
			   "step:objects_nr", (uintmax_t)step.objects_nr);
	trace2_data_intmax("repack", opts->existing->repo,
			   "step:packs_nr", (uintmax_t)step.u.write.nr);

	trace2_region_leave("repack", "compact", opts->existing->repo);
	trace2_region_leave("repack", "steps:write", opts->existing->repo);

	trace2_region_enter("repack", "steps:rest", opts->existing->repo);

	/*
	 * Then start over, repeat, and either compact or keep as-is
	 * each MIDX layer until we have exhausted the chain.
	 *
	 * Finally, evaluate the remainder of the chain (if any) and
	 * either compact a sequence of adjacent layers, or keep
	 * individual layers as-is according to the same merging
	 * condition as above.
	 */
	while (m) {
		struct multi_pack_index *next = m;

		ALLOC_GROW(steps, steps_nr + 1, steps_alloc);

		memset(&step, 0, sizeof(step));
		step.type = MIDX_COMPACTION_STEP_UNKNOWN;

		trace2_region_enter("repack", "step", opts->existing->repo);

		trace2_data_string("repack", opts->existing->repo,
				   "from", midx_get_checksum_hex(m));

		while (next) {
			uint32_t proposed_objects_nr;
			if (unsigned_add_overflows(step.objects_nr, next->num_objects)) {
				ret = error(_("too many objects in MIDX compaction step"));
				trace2_region_leave("repack", "step", opts->existing->repo);
				goto out;
			}

			proposed_objects_nr = step.objects_nr + next->num_objects;

			trace2_data_string("repack", opts->existing->repo,
					   "proposed",
					   midx_get_checksum_hex(next));
			trace2_data_intmax("repack", opts->existing->repo,
					   "proposed:objects_nr",
					   (uintmax_t)next->num_objects);

			if (!next->base_midx) {
				/*
				 * If we are at the end of the MIDX
				 * chain, there is nothing to compact,
				 * so mark it and stop.
				 */
				step.objects_nr = proposed_objects_nr;
				break;
			}

			if (proposed_objects_nr < next->base_midx->num_objects / opts->midx_split_factor) {
				/*
				 * If there is a MIDX following this
				 * one, but our accumulated size is less
				 * than half of its size, compacting
				 * them would violate the merging
				 * condition, so stop here.
				 */

				trace2_data_string("repack", opts->existing->repo,
						   "compact:violated:at",
						   midx_get_checksum_hex(next->base_midx));
				trace2_data_intmax("repack", opts->existing->repo,
						   "compact:violated:at:objects_nr",
						   (uintmax_t)next->base_midx->num_objects);
				break;
			}

			/*
			 * Otherwise, it is OK to compact the next layer
			 * into this one. Do so, and then continue
			 * through the remainder of the chain.
			 */
			step.objects_nr = proposed_objects_nr;
			trace2_data_intmax("repack", opts->existing->repo,
					   "step:objects_nr",
					   (uintmax_t)step.objects_nr);
			next = next->base_midx;
		}

		if (m == next) {
			step.type = MIDX_COMPACTION_STEP_COPY;
			step.u.copy = m;

			trace2_data_string("repack", opts->existing->repo,
					   "type", "copy");
		} else {
			step.type = MIDX_COMPACTION_STEP_COMPACT;
			step.u.compact.from = next;
			step.u.compact.to = m;

			trace2_data_string("repack", opts->existing->repo,
					   "to", midx_get_checksum_hex(m));
			trace2_data_string("repack", opts->existing->repo,
					   "type", "compact");
		}

		m = next->base_midx;
		steps[steps_nr++] = step;
		trace2_region_leave("repack", "step", opts->existing->repo);
	}

	trace2_region_leave("repack", "steps:rest", opts->existing->repo);

out:
	*steps_p = steps;
	*steps_nr_p = steps_nr;

	strbuf_release(&buf);

	trace2_region_leave("repack", "make_midx_compaction_plan",
			    opts->existing->repo);

	return ret;
}

static int write_midx_incremental(struct repack_write_midx_opts *opts)
{
	struct midx_compaction_step *steps = NULL;
	struct strbuf lock_name = STRBUF_INIT;
	struct lock_file lf;
	size_t steps_nr = 0;
	size_t i;
	int ret = 0;

	get_midx_chain_filename(opts->existing->source, &lock_name);
	if (safe_create_leading_directories(opts->existing->repo,
					    lock_name.buf))
		die_errno(_("unable to create leading directories of %s"),
			  lock_name.buf);
	hold_lock_file_for_update(&lf, lock_name.buf, LOCK_DIE_ON_ERROR);

	if (!fdopen_lock_file(&lf, "w")) {
		ret = error_errno(_("unable to open multi-pack-index chain file"));
		goto done;
	}

	if (repack_make_midx_compaction_plan(opts, &steps, &steps_nr) < 0) {
		ret = error(_("unable to generate compaction plan"));
		goto done;
	}

	for (i = 0; i < steps_nr; i++) {
		struct midx_compaction_step *step = &steps[i];
		char *base = NULL;

		if (i + 1 < steps_nr)
			base = xstrdup(midx_compaction_step_base(&steps[i + 1]));

		if (midx_compaction_step_exec(step, opts, base) < 0) {
			ret = error(_("unable to execute compaction step %"PRIuMAX),
				    (uintmax_t)i);
			free(base);
			goto done;
		}

		free(base);
	}

	i = steps_nr;
	while (i--) {
		struct midx_compaction_step *step = &steps[i];
		if (!step->csum)
			BUG("missing result for compaction step %"PRIuMAX,
			    (uintmax_t)i);
		fprintf(get_lock_file_fp(&lf), "%s\n", step->csum);
	}

	commit_lock_file(&lf);

done:
	strbuf_release(&lock_name);
	for (i = 0; i < steps_nr; i++)
		midx_compaction_step_release(&steps[i]);
	free(steps);
	return ret;
}

int repack_write_midx(struct repack_write_midx_opts *opts)
{
	switch (opts->mode) {
	case REPACK_WRITE_MIDX_NONE:
		BUG("write_midx mode is NONE?");
	case REPACK_WRITE_MIDX_DEFAULT:
		return write_midx_included_packs(opts);
	case REPACK_WRITE_MIDX_INCREMENTAL:
		return write_midx_incremental(opts);
	default:
		BUG("unhandled write_midx mode: %d", opts->mode);
	}
}
