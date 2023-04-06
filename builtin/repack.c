#include "builtin.h"
#include "alloc.h"
#include "config.h"
#include "dir.h"
#include "hex.h"
#include "parse-options.h"
#include "run-command.h"
#include "sigchain.h"
#include "strbuf.h"
#include "string-list.h"
#include "strvec.h"
#include "midx.h"
#include "packfile.h"
#include "prune-packed.h"
#include "object-store.h"
#include "promisor-remote.h"
#include "shallow.h"
#include "pack.h"
#include "pack-bitmap.h"
#include "refs.h"

#define ALL_INTO_ONE 1
#define LOOSEN_UNREACHABLE 2
#define PACK_CRUFT 4

#define DELETE_PACK 1
#define CRUFT_PACK 2

static int pack_everything;
static int delta_base_offset = 1;
static int pack_kept_objects = -1;
static int write_bitmaps = -1;
static int use_delta_islands;
static int run_update_server_info = 1;
static char *packdir, *packtmp_name, *packtmp;

static const char *const git_repack_usage[] = {
	N_("git repack [<options>]"),
	NULL
};

static const char incremental_bitmap_conflict_error[] = N_(
"Incremental repacks are incompatible with bitmap indexes.  Use\n"
"--no-write-bitmap-index or disable the pack.writeBitmaps configuration."
);

struct pack_objects_args {
	const char *window;
	const char *window_memory;
	const char *depth;
	const char *threads;
	const char *max_pack_size;
	int no_reuse_delta;
	int no_reuse_object;
	int quiet;
	int local;
};

static int repack_config(const char *var, const char *value, void *cb)
{
	struct pack_objects_args *cruft_po_args = cb;
	if (!strcmp(var, "repack.usedeltabaseoffset")) {
		delta_base_offset = git_config_bool(var, value);
		return 0;
	}
	if (!strcmp(var, "repack.packkeptobjects")) {
		pack_kept_objects = git_config_bool(var, value);
		return 0;
	}
	if (!strcmp(var, "repack.writebitmaps") ||
	    !strcmp(var, "pack.writebitmaps")) {
		write_bitmaps = git_config_bool(var, value);
		return 0;
	}
	if (!strcmp(var, "repack.usedeltaislands")) {
		use_delta_islands = git_config_bool(var, value);
		return 0;
	}
	if (strcmp(var, "repack.updateserverinfo") == 0) {
		run_update_server_info = git_config_bool(var, value);
		return 0;
	}
	if (!strcmp(var, "repack.cruftwindow"))
		return git_config_string(&cruft_po_args->window, var, value);
	if (!strcmp(var, "repack.cruftwindowmemory"))
		return git_config_string(&cruft_po_args->window_memory, var, value);
	if (!strcmp(var, "repack.cruftdepth"))
		return git_config_string(&cruft_po_args->depth, var, value);
	if (!strcmp(var, "repack.cruftthreads"))
		return git_config_string(&cruft_po_args->threads, var, value);
	return git_default_config(var, value, cb);
}

/*
 * Adds all packs hex strings to either fname_nonkept_list or
 * fname_kept_list based on whether each pack has a corresponding
 * .keep file or not.  Packs without a .keep file are not to be kept
 * if we are going to pack everything into one file.
 */
static void collect_pack_filenames(struct string_list *fname_nonkept_list,
				   struct string_list *fname_kept_list,
				   const struct string_list *extra_keep)
{
	DIR *dir;
	struct dirent *e;
	char *fname;

	if (!(dir = opendir(packdir)))
		return;

	while ((e = readdir(dir)) != NULL) {
		size_t len;
		int i;

		if (!strip_suffix(e->d_name, ".pack", &len))
			continue;

		for (i = 0; i < extra_keep->nr; i++)
			if (!fspathcmp(e->d_name, extra_keep->items[i].string))
				break;

		fname = xmemdupz(e->d_name, len);

		if ((extra_keep->nr > 0 && i < extra_keep->nr) ||
		    (file_exists(mkpath("%s/%s.keep", packdir, fname)))) {
			string_list_append_nodup(fname_kept_list, fname);
		} else {
			struct string_list_item *item;
			item = string_list_append_nodup(fname_nonkept_list,
							fname);
			if (file_exists(mkpath("%s/%s.mtimes", packdir, fname)))
				item->util = (void*)(uintptr_t)CRUFT_PACK;
		}
	}
	closedir(dir);

	string_list_sort(fname_kept_list);
}

static void remove_redundant_pack(const char *dir_name, const char *base_name)
{
	struct strbuf buf = STRBUF_INIT;
	struct multi_pack_index *m = get_local_multi_pack_index(the_repository);
	strbuf_addf(&buf, "%s.pack", base_name);
	if (m && midx_contains_pack(m, buf.buf))
		clear_midx_file(the_repository);
	strbuf_insertf(&buf, 0, "%s/", dir_name);
	unlink_pack_path(buf.buf, 1);
	strbuf_release(&buf);
}

static void prepare_pack_objects(struct child_process *cmd,
				 const struct pack_objects_args *args,
				 const char *out)
{
	strvec_push(&cmd->args, "pack-objects");
	if (args->window)
		strvec_pushf(&cmd->args, "--window=%s", args->window);
	if (args->window_memory)
		strvec_pushf(&cmd->args, "--window-memory=%s", args->window_memory);
	if (args->depth)
		strvec_pushf(&cmd->args, "--depth=%s", args->depth);
	if (args->threads)
		strvec_pushf(&cmd->args, "--threads=%s", args->threads);
	if (args->max_pack_size)
		strvec_pushf(&cmd->args, "--max-pack-size=%s", args->max_pack_size);
	if (args->no_reuse_delta)
		strvec_pushf(&cmd->args, "--no-reuse-delta");
	if (args->no_reuse_object)
		strvec_pushf(&cmd->args, "--no-reuse-object");
	if (args->local)
		strvec_push(&cmd->args,  "--local");
	if (args->quiet)
		strvec_push(&cmd->args,  "--quiet");
	if (delta_base_offset)
		strvec_push(&cmd->args,  "--delta-base-offset");
	strvec_push(&cmd->args, out);
	cmd->git_cmd = 1;
	cmd->out = -1;
}

/*
 * Write oid to the given struct child_process's stdin, starting it first if
 * necessary.
 */
static int write_oid(const struct object_id *oid,
		     struct packed_git *pack UNUSED,
		     uint32_t pos UNUSED, void *data)
{
	struct child_process *cmd = data;

	if (cmd->in == -1) {
		if (start_command(cmd))
			die(_("could not start pack-objects to repack promisor objects"));
	}

	xwrite(cmd->in, oid_to_hex(oid), the_hash_algo->hexsz);
	xwrite(cmd->in, "\n", 1);
	return 0;
}

static struct {
	const char *name;
	unsigned optional:1;
} exts[] = {
	{".pack"},
	{".rev", 1},
	{".mtimes", 1},
	{".bitmap", 1},
	{".promisor", 1},
	{".idx"},
};

struct generated_pack_data {
	struct tempfile *tempfiles[ARRAY_SIZE(exts)];
};

static struct generated_pack_data *populate_pack_exts(const char *name)
{
	struct stat statbuf;
	struct strbuf path = STRBUF_INIT;
	struct generated_pack_data *data = xcalloc(1, sizeof(*data));
	int i;

	for (i = 0; i < ARRAY_SIZE(exts); i++) {
		strbuf_reset(&path);
		strbuf_addf(&path, "%s-%s%s", packtmp, name, exts[i].name);

		if (stat(path.buf, &statbuf))
			continue;

		data->tempfiles[i] = register_tempfile(path.buf);
	}

	strbuf_release(&path);
	return data;
}

static void repack_promisor_objects(const struct pack_objects_args *args,
				    struct string_list *names)
{
	struct child_process cmd = CHILD_PROCESS_INIT;
	FILE *out;
	struct strbuf line = STRBUF_INIT;

	prepare_pack_objects(&cmd, args, packtmp);
	cmd.in = -1;

	/*
	 * NEEDSWORK: Giving pack-objects only the OIDs without any ordering
	 * hints may result in suboptimal deltas in the resulting pack. See if
	 * the OIDs can be sent with fake paths such that pack-objects can use a
	 * {type -> existing pack order} ordering when computing deltas instead
	 * of a {type -> size} ordering, which may produce better deltas.
	 */
	for_each_packed_object(write_oid, &cmd,
			       FOR_EACH_OBJECT_PROMISOR_ONLY);

	if (cmd.in == -1) {
		/* No packed objects; cmd was never started */
		child_process_clear(&cmd);
		return;
	}

	close(cmd.in);

	out = xfdopen(cmd.out, "r");
	while (strbuf_getline_lf(&line, out) != EOF) {
		struct string_list_item *item;
		char *promisor_name;

		if (line.len != the_hash_algo->hexsz)
			die(_("repack: Expecting full hex object ID lines only from pack-objects."));
		item = string_list_append(names, line.buf);

		/*
		 * pack-objects creates the .pack and .idx files, but not the
		 * .promisor file. Create the .promisor file, which is empty.
		 *
		 * NEEDSWORK: fetch-pack sometimes generates non-empty
		 * .promisor files containing the ref names and associated
		 * hashes at the point of generation of the corresponding
		 * packfile, but this would not preserve their contents. Maybe
		 * concatenate the contents of all .promisor files instead of
		 * just creating a new empty file.
		 */
		promisor_name = mkpathdup("%s-%s.promisor", packtmp,
					  line.buf);
		write_promisor_file(promisor_name, NULL, 0);

		item->util = populate_pack_exts(item->string);

		free(promisor_name);
	}
	fclose(out);
	if (finish_command(&cmd))
		die(_("could not finish pack-objects to repack promisor objects"));
}

struct pack_geometry {
	struct packed_git **pack;
	uint32_t pack_nr, pack_alloc;
	uint32_t split;
};

static uint32_t geometry_pack_weight(struct packed_git *p)
{
	if (open_pack_index(p))
		die(_("cannot open index for %s"), p->pack_name);
	return p->num_objects;
}

static int geometry_cmp(const void *va, const void *vb)
{
	uint32_t aw = geometry_pack_weight(*(struct packed_git **)va),
		 bw = geometry_pack_weight(*(struct packed_git **)vb);

	if (aw < bw)
		return -1;
	if (aw > bw)
		return 1;
	return 0;
}

static void init_pack_geometry(struct pack_geometry **geometry_p,
			       struct string_list *existing_kept_packs)
{
	struct packed_git *p;
	struct pack_geometry *geometry;
	struct strbuf buf = STRBUF_INIT;

	*geometry_p = xcalloc(1, sizeof(struct pack_geometry));
	geometry = *geometry_p;

	for (p = get_all_packs(the_repository); p; p = p->next) {
		if (!pack_kept_objects) {
			/*
			 * Any pack that has its pack_keep bit set will appear
			 * in existing_kept_packs below, but this saves us from
			 * doing a more expensive check.
			 */
			if (p->pack_keep)
				continue;

			/*
			 * The pack may be kept via the --keep-pack option;
			 * check 'existing_kept_packs' to determine whether to
			 * ignore it.
			 */
			strbuf_reset(&buf);
			strbuf_addstr(&buf, pack_basename(p));
			strbuf_strip_suffix(&buf, ".pack");

			if (string_list_has_string(existing_kept_packs, buf.buf))
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

	QSORT(geometry->pack, geometry->pack_nr, geometry_cmp);
	strbuf_release(&buf);
}

static void split_pack_geometry(struct pack_geometry *geometry, int factor)
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

		if (unsigned_mult_overflows(factor, geometry_pack_weight(prev)))
			die(_("pack %s too large to consider in geometric "
			      "progression"),
			    prev->pack_name);

		if (geometry_pack_weight(ours) < factor * geometry_pack_weight(prev))
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

		if (unsigned_add_overflows(total_size, geometry_pack_weight(p)))
			die(_("pack %s too large to roll up"), p->pack_name);
		total_size += geometry_pack_weight(p);
	}
	for (i = split; i < geometry->pack_nr; i++) {
		struct packed_git *ours = geometry->pack[i];

		if (unsigned_mult_overflows(factor, total_size))
			die(_("pack %s too large to roll up"), ours->pack_name);

		if (geometry_pack_weight(ours) < factor * total_size) {
			if (unsigned_add_overflows(total_size,
						   geometry_pack_weight(ours)))
				die(_("pack %s too large to roll up"),
				    ours->pack_name);

			split++;
			total_size += geometry_pack_weight(ours);
		} else
			break;
	}

	geometry->split = split;
}

static struct packed_git *get_largest_active_pack(struct pack_geometry *geometry)
{
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
	return geometry->pack[geometry->pack_nr - 1];
}

static void clear_pack_geometry(struct pack_geometry *geometry)
{
	if (!geometry)
		return;

	free(geometry->pack);
	geometry->pack_nr = 0;
	geometry->pack_alloc = 0;
	geometry->split = 0;
}

struct midx_snapshot_ref_data {
	struct tempfile *f;
	struct oidset seen;
	int preferred;
};

static int midx_snapshot_ref_one(const char *refname UNUSED,
				 const struct object_id *oid,
				 int flag UNUSED, void *_data)
{
	struct midx_snapshot_ref_data *data = _data;
	struct object_id peeled;

	if (!peel_iterated_oid(oid, &peeled))
		oid = &peeled;

	if (oidset_insert(&data->seen, oid))
		return 0; /* already seen */

	if (oid_object_info(the_repository, oid, NULL) != OBJ_COMMIT)
		return 0;

	fprintf(data->f->fp, "%s%s\n", data->preferred ? "+" : "",
		oid_to_hex(oid));

	return 0;
}

static void midx_snapshot_refs(struct tempfile *f)
{
	struct midx_snapshot_ref_data data;
	const struct string_list *preferred = bitmap_preferred_tips(the_repository);

	data.f = f;
	data.preferred = 0;
	oidset_init(&data.seen, 0);

	if (!fdopen_tempfile(f, "w"))
		 die(_("could not open tempfile %s for writing"),
		     get_tempfile_path(f));

	if (preferred) {
		struct string_list_item *item;

		data.preferred = 1;
		for_each_string_list_item(item, preferred)
			for_each_ref_in(item->string, midx_snapshot_ref_one, &data);
		data.preferred = 0;
	}

	for_each_ref(midx_snapshot_ref_one, &data);

	if (close_tempfile_gently(f)) {
		int save_errno = errno;
		delete_tempfile(&f);
		errno = save_errno;
		die_errno(_("could not close refs snapshot tempfile"));
	}

	oidset_clear(&data.seen);
}

static void midx_included_packs(struct string_list *include,
				struct string_list *existing_nonkept_packs,
				struct string_list *existing_kept_packs,
				struct string_list *names,
				struct pack_geometry *geometry)
{
	struct string_list_item *item;

	for_each_string_list_item(item, existing_kept_packs)
		string_list_insert(include, xstrfmt("%s.idx", item->string));
	for_each_string_list_item(item, names)
		string_list_insert(include, xstrfmt("pack-%s.idx", item->string));
	if (geometry) {
		struct strbuf buf = STRBUF_INIT;
		uint32_t i;
		for (i = geometry->split; i < geometry->pack_nr; i++) {
			struct packed_git *p = geometry->pack[i];

			strbuf_addstr(&buf, pack_basename(p));
			strbuf_strip_suffix(&buf, ".pack");
			strbuf_addstr(&buf, ".idx");

			string_list_insert(include, strbuf_detach(&buf, NULL));
		}

		for_each_string_list_item(item, existing_nonkept_packs) {
			if (!((uintptr_t)item->util & CRUFT_PACK)) {
				/*
				 * no need to check DELETE_PACK, since we're not
				 * doing an ALL_INTO_ONE repack
				 */
				continue;
			}
			string_list_insert(include, xstrfmt("%s.idx", item->string));
		}
	} else {
		for_each_string_list_item(item, existing_nonkept_packs) {
			if ((uintptr_t)item->util & DELETE_PACK)
				continue;
			string_list_insert(include, xstrfmt("%s.idx", item->string));
		}
	}
}

static int write_midx_included_packs(struct string_list *include,
				     struct pack_geometry *geometry,
				     const char *refs_snapshot,
				     int show_progress, int write_bitmaps)
{
	struct child_process cmd = CHILD_PROCESS_INIT;
	struct string_list_item *item;
	struct packed_git *largest = get_largest_active_pack(geometry);
	FILE *in;
	int ret;

	if (!include->nr)
		return 0;

	cmd.in = -1;
	cmd.git_cmd = 1;

	strvec_push(&cmd.args, "multi-pack-index");
	strvec_pushl(&cmd.args, "write", "--stdin-packs", NULL);

	if (show_progress)
		strvec_push(&cmd.args, "--progress");
	else
		strvec_push(&cmd.args, "--no-progress");

	if (write_bitmaps)
		strvec_push(&cmd.args, "--bitmap");

	if (largest)
		strvec_pushf(&cmd.args, "--preferred-pack=%s",
			     pack_basename(largest));

	if (refs_snapshot)
		strvec_pushf(&cmd.args, "--refs-snapshot=%s", refs_snapshot);

	ret = start_command(&cmd);
	if (ret)
		return ret;

	in = xfdopen(cmd.in, "w");
	for_each_string_list_item(item, include)
		fprintf(in, "%s\n", item->string);
	fclose(in);

	return finish_command(&cmd);
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

static int write_cruft_pack(const struct pack_objects_args *args,
			    const char *destination,
			    const char *pack_prefix,
			    const char *cruft_expiration,
			    struct string_list *names,
			    struct string_list *existing_packs,
			    struct string_list *existing_kept_packs)
{
	struct child_process cmd = CHILD_PROCESS_INIT;
	struct strbuf line = STRBUF_INIT;
	struct string_list_item *item;
	FILE *in, *out;
	int ret;
	const char *scratch;
	int local = skip_prefix(destination, packdir, &scratch);

	prepare_pack_objects(&cmd, args, destination);

	strvec_push(&cmd.args, "--cruft");
	if (cruft_expiration)
		strvec_pushf(&cmd.args, "--cruft-expiration=%s",
			     cruft_expiration);

	strvec_push(&cmd.args, "--honor-pack-keep");
	strvec_push(&cmd.args, "--non-empty");
	strvec_push(&cmd.args, "--max-pack-size=0");

	cmd.in = -1;

	ret = start_command(&cmd);
	if (ret)
		return ret;

	/*
	 * names has a confusing double use: it both provides the list
	 * of just-written new packs, and accepts the name of the cruft
	 * pack we are writing.
	 *
	 * By the time it is read here, it contains only the pack(s)
	 * that were just written, which is exactly the set of packs we
	 * want to consider kept.
	 *
	 * If `--expire-to` is given, the double-use served by `names`
	 * ensures that the pack written to `--expire-to` excludes any
	 * objects contained in the cruft pack.
	 */
	in = xfdopen(cmd.in, "w");
	for_each_string_list_item(item, names)
		fprintf(in, "%s-%s.pack\n", pack_prefix, item->string);
	for_each_string_list_item(item, existing_packs)
		fprintf(in, "-%s.pack\n", item->string);
	for_each_string_list_item(item, existing_kept_packs)
		fprintf(in, "%s.pack\n", item->string);
	fclose(in);

	out = xfdopen(cmd.out, "r");
	while (strbuf_getline_lf(&line, out) != EOF) {
		struct string_list_item *item;

		if (line.len != the_hash_algo->hexsz)
			die(_("repack: Expecting full hex object ID lines only "
			      "from pack-objects."));
		/*
		 * avoid putting packs written outside of the repository in the
		 * list of names
		 */
		if (local) {
			item = string_list_append(names, line.buf);
			item->util = populate_pack_exts(line.buf);
		}
	}
	fclose(out);

	strbuf_release(&line);

	return finish_command(&cmd);
}

int cmd_repack(int argc, const char **argv, const char *prefix)
{
	struct child_process cmd = CHILD_PROCESS_INIT;
	struct string_list_item *item;
	struct string_list names = STRING_LIST_INIT_DUP;
	struct string_list existing_nonkept_packs = STRING_LIST_INIT_DUP;
	struct string_list existing_kept_packs = STRING_LIST_INIT_DUP;
	struct pack_geometry *geometry = NULL;
	struct strbuf line = STRBUF_INIT;
	struct tempfile *refs_snapshot = NULL;
	int i, ext, ret;
	FILE *out;
	int show_progress;

	/* variables to be filled by option parsing */
	int delete_redundant = 0;
	const char *unpack_unreachable = NULL;
	int keep_unreachable = 0;
	struct string_list keep_pack_list = STRING_LIST_INIT_NODUP;
	struct pack_objects_args po_args = {NULL};
	struct pack_objects_args cruft_po_args = {NULL};
	int geometric_factor = 0;
	int write_midx = 0;
	const char *cruft_expiration = NULL;
	const char *expire_to = NULL;

	struct option builtin_repack_options[] = {
		OPT_BIT('a', NULL, &pack_everything,
				N_("pack everything in a single pack"), ALL_INTO_ONE),
		OPT_BIT('A', NULL, &pack_everything,
				N_("same as -a, and turn unreachable objects loose"),
				   LOOSEN_UNREACHABLE | ALL_INTO_ONE),
		OPT_BIT(0, "cruft", &pack_everything,
				N_("same as -a, pack unreachable cruft objects separately"),
				   PACK_CRUFT),
		OPT_STRING(0, "cruft-expiration", &cruft_expiration, N_("approxidate"),
				N_("with -C, expire objects older than this")),
		OPT_BOOL('d', NULL, &delete_redundant,
				N_("remove redundant packs, and run git-prune-packed")),
		OPT_BOOL('f', NULL, &po_args.no_reuse_delta,
				N_("pass --no-reuse-delta to git-pack-objects")),
		OPT_BOOL('F', NULL, &po_args.no_reuse_object,
				N_("pass --no-reuse-object to git-pack-objects")),
		OPT_NEGBIT('n', NULL, &run_update_server_info,
				N_("do not run git-update-server-info"), 1),
		OPT__QUIET(&po_args.quiet, N_("be quiet")),
		OPT_BOOL('l', "local", &po_args.local,
				N_("pass --local to git-pack-objects")),
		OPT_BOOL('b', "write-bitmap-index", &write_bitmaps,
				N_("write bitmap index")),
		OPT_BOOL('i', "delta-islands", &use_delta_islands,
				N_("pass --delta-islands to git-pack-objects")),
		OPT_STRING(0, "unpack-unreachable", &unpack_unreachable, N_("approxidate"),
				N_("with -A, do not loosen objects older than this")),
		OPT_BOOL('k', "keep-unreachable", &keep_unreachable,
				N_("with -a, repack unreachable objects")),
		OPT_STRING(0, "window", &po_args.window, N_("n"),
				N_("size of the window used for delta compression")),
		OPT_STRING(0, "window-memory", &po_args.window_memory, N_("bytes"),
				N_("same as the above, but limit memory size instead of entries count")),
		OPT_STRING(0, "depth", &po_args.depth, N_("n"),
				N_("limits the maximum delta depth")),
		OPT_STRING(0, "threads", &po_args.threads, N_("n"),
				N_("limits the maximum number of threads")),
		OPT_STRING(0, "max-pack-size", &po_args.max_pack_size, N_("bytes"),
				N_("maximum size of each packfile")),
		OPT_BOOL(0, "pack-kept-objects", &pack_kept_objects,
				N_("repack objects in packs marked with .keep")),
		OPT_STRING_LIST(0, "keep-pack", &keep_pack_list, N_("name"),
				N_("do not repack this pack")),
		OPT_INTEGER('g', "geometric", &geometric_factor,
			    N_("find a geometric progression with factor <N>")),
		OPT_BOOL('m', "write-midx", &write_midx,
			   N_("write a multi-pack index of the resulting packs")),
		OPT_STRING(0, "expire-to", &expire_to, N_("dir"),
			   N_("pack prefix to store a pack containing pruned objects")),
		OPT_END()
	};

	git_config(repack_config, &cruft_po_args);

	argc = parse_options(argc, argv, prefix, builtin_repack_options,
				git_repack_usage, 0);

	if (delete_redundant && repository_format_precious_objects)
		die(_("cannot delete packs in a precious-objects repo"));

	if (keep_unreachable &&
	    (unpack_unreachable || (pack_everything & LOOSEN_UNREACHABLE)))
		die(_("options '%s' and '%s' cannot be used together"), "--keep-unreachable", "-A");

	if (pack_everything & PACK_CRUFT) {
		pack_everything |= ALL_INTO_ONE;

		if (unpack_unreachable || (pack_everything & LOOSEN_UNREACHABLE))
			die(_("options '%s' and '%s' cannot be used together"), "--cruft", "-A");
		if (keep_unreachable)
			die(_("options '%s' and '%s' cannot be used together"), "--cruft", "-k");
	}

	if (write_bitmaps < 0) {
		if (!write_midx &&
		    (!(pack_everything & ALL_INTO_ONE) || !is_bare_repository()))
			write_bitmaps = 0;
	} else if (write_bitmaps &&
		   git_env_bool(GIT_TEST_MULTI_PACK_INDEX, 0) &&
		   git_env_bool(GIT_TEST_MULTI_PACK_INDEX_WRITE_BITMAP, 0)) {
		write_bitmaps = 0;
	}
	if (pack_kept_objects < 0)
		pack_kept_objects = write_bitmaps > 0 && !write_midx;

	if (write_bitmaps && !(pack_everything & ALL_INTO_ONE) && !write_midx)
		die(_(incremental_bitmap_conflict_error));

	if (write_midx && write_bitmaps) {
		struct strbuf path = STRBUF_INIT;

		strbuf_addf(&path, "%s/%s_XXXXXX", get_object_directory(),
			    "bitmap-ref-tips");

		refs_snapshot = xmks_tempfile(path.buf);
		midx_snapshot_refs(refs_snapshot);

		strbuf_release(&path);
	}

	packdir = mkpathdup("%s/pack", get_object_directory());
	packtmp_name = xstrfmt(".tmp-%d-pack", (int)getpid());
	packtmp = mkpathdup("%s/%s", packdir, packtmp_name);

	collect_pack_filenames(&existing_nonkept_packs, &existing_kept_packs,
			       &keep_pack_list);

	if (geometric_factor) {
		if (pack_everything)
			die(_("options '%s' and '%s' cannot be used together"), "--geometric", "-A/-a");
		init_pack_geometry(&geometry, &existing_kept_packs);
		split_pack_geometry(geometry, geometric_factor);
	}

	prepare_pack_objects(&cmd, &po_args, packtmp);

	show_progress = !po_args.quiet && isatty(2);

	strvec_push(&cmd.args, "--keep-true-parents");
	if (!pack_kept_objects)
		strvec_push(&cmd.args, "--honor-pack-keep");
	for (i = 0; i < keep_pack_list.nr; i++)
		strvec_pushf(&cmd.args, "--keep-pack=%s",
			     keep_pack_list.items[i].string);
	strvec_push(&cmd.args, "--non-empty");
	if (!geometry) {
		/*
		 * We need to grab all reachable objects, including those that
		 * are reachable from reflogs and the index.
		 *
		 * When repacking into a geometric progression of packs,
		 * however, we ask 'git pack-objects --stdin-packs', and it is
		 * not about packing objects based on reachability but about
		 * repacking all the objects in specified packs and loose ones
		 * (indeed, --stdin-packs is incompatible with these options).
		 */
		strvec_push(&cmd.args, "--all");
		strvec_push(&cmd.args, "--reflog");
		strvec_push(&cmd.args, "--indexed-objects");
	}
	if (repo_has_promisor_remote(the_repository))
		strvec_push(&cmd.args, "--exclude-promisor-objects");
	if (!write_midx) {
		if (write_bitmaps > 0)
			strvec_push(&cmd.args, "--write-bitmap-index");
		else if (write_bitmaps < 0)
			strvec_push(&cmd.args, "--write-bitmap-index-quiet");
	}
	if (use_delta_islands)
		strvec_push(&cmd.args, "--delta-islands");

	if (pack_everything & ALL_INTO_ONE) {
		repack_promisor_objects(&po_args, &names);

		if (existing_nonkept_packs.nr && delete_redundant &&
		    !(pack_everything & PACK_CRUFT)) {
			for_each_string_list_item(item, &names) {
				strvec_pushf(&cmd.args, "--keep-pack=%s-%s.pack",
					     packtmp_name, item->string);
			}
			if (unpack_unreachable) {
				strvec_pushf(&cmd.args,
					     "--unpack-unreachable=%s",
					     unpack_unreachable);
			} else if (pack_everything & LOOSEN_UNREACHABLE) {
				strvec_push(&cmd.args,
					    "--unpack-unreachable");
			} else if (keep_unreachable) {
				strvec_push(&cmd.args, "--keep-unreachable");
				strvec_push(&cmd.args, "--pack-loose-unreachable");
			}
		}
	} else if (geometry) {
		strvec_push(&cmd.args, "--stdin-packs");
		strvec_push(&cmd.args, "--unpacked");
	} else {
		strvec_push(&cmd.args, "--unpacked");
		strvec_push(&cmd.args, "--incremental");
	}

	if (geometry)
		cmd.in = -1;
	else
		cmd.no_stdin = 1;

	ret = start_command(&cmd);
	if (ret)
		goto cleanup;

	if (geometry) {
		FILE *in = xfdopen(cmd.in, "w");
		/*
		 * The resulting pack should contain all objects in packs that
		 * are going to be rolled up, but exclude objects in packs which
		 * are being left alone.
		 */
		for (i = 0; i < geometry->split; i++)
			fprintf(in, "%s\n", pack_basename(geometry->pack[i]));
		for (i = geometry->split; i < geometry->pack_nr; i++)
			fprintf(in, "^%s\n", pack_basename(geometry->pack[i]));
		fclose(in);
	}

	out = xfdopen(cmd.out, "r");
	while (strbuf_getline_lf(&line, out) != EOF) {
		struct string_list_item *item;

		if (line.len != the_hash_algo->hexsz)
			die(_("repack: Expecting full hex object ID lines only from pack-objects."));
		item = string_list_append(&names, line.buf);
		item->util = populate_pack_exts(item->string);
	}
	strbuf_release(&line);
	fclose(out);
	ret = finish_command(&cmd);
	if (ret)
		goto cleanup;

	if (!names.nr && !po_args.quiet)
		printf_ln(_("Nothing new to pack."));

	if (pack_everything & PACK_CRUFT) {
		const char *pack_prefix;
		if (!skip_prefix(packtmp, packdir, &pack_prefix))
			die(_("pack prefix %s does not begin with objdir %s"),
			    packtmp, packdir);
		if (*pack_prefix == '/')
			pack_prefix++;

		if (!cruft_po_args.window)
			cruft_po_args.window = po_args.window;
		if (!cruft_po_args.window_memory)
			cruft_po_args.window_memory = po_args.window_memory;
		if (!cruft_po_args.depth)
			cruft_po_args.depth = po_args.depth;
		if (!cruft_po_args.threads)
			cruft_po_args.threads = po_args.threads;

		cruft_po_args.local = po_args.local;
		cruft_po_args.quiet = po_args.quiet;

		ret = write_cruft_pack(&cruft_po_args, packtmp, pack_prefix,
				       cruft_expiration, &names,
				       &existing_nonkept_packs,
				       &existing_kept_packs);
		if (ret)
			goto cleanup;

		if (delete_redundant && expire_to) {
			/*
			 * If `--expire-to` is given with `-d`, it's possible
			 * that we're about to prune some objects. With cruft
			 * packs, pruning is implicit: any objects from existing
			 * packs that weren't picked up by new packs are removed
			 * when their packs are deleted.
			 *
			 * Generate an additional cruft pack, with one twist:
			 * `names` now includes the name of the cruft pack
			 * written in the previous step. So the contents of
			 * _this_ cruft pack exclude everything contained in the
			 * existing cruft pack (that is, all of the unreachable
			 * objects which are no older than
			 * `--cruft-expiration`).
			 *
			 * To make this work, cruft_expiration must become NULL
			 * so that this cruft pack doesn't actually prune any
			 * objects. If it were non-NULL, this call would always
			 * generate an empty pack (since every object not in the
			 * cruft pack generated above will have an mtime older
			 * than the expiration).
			 */
			ret = write_cruft_pack(&cruft_po_args, expire_to,
					       pack_prefix,
					       NULL,
					       &names,
					       &existing_nonkept_packs,
					       &existing_kept_packs);
			if (ret)
				goto cleanup;
		}
	}

	string_list_sort(&names);

	close_object_store(the_repository->objects);

	/*
	 * Ok we have prepared all new packfiles.
	 */
	for_each_string_list_item(item, &names) {
		struct generated_pack_data *data = item->util;

		for (ext = 0; ext < ARRAY_SIZE(exts); ext++) {
			char *fname;

			fname = mkpathdup("%s/pack-%s%s",
					packdir, item->string, exts[ext].name);

			if (data->tempfiles[ext]) {
				const char *fname_old = get_tempfile_path(data->tempfiles[ext]);
				struct stat statbuffer;

				if (!stat(fname_old, &statbuffer)) {
					statbuffer.st_mode &= ~(S_IWUSR | S_IWGRP | S_IWOTH);
					chmod(fname_old, statbuffer.st_mode);
				}

				if (rename_tempfile(&data->tempfiles[ext], fname))
					die_errno(_("renaming pack to '%s' failed"), fname);
			} else if (!exts[ext].optional)
				die(_("pack-objects did not write a '%s' file for pack %s-%s"),
				    exts[ext].name, packtmp, item->string);
			else if (unlink(fname) < 0 && errno != ENOENT)
				die_errno(_("could not unlink: %s"), fname);

			free(fname);
		}
	}
	/* End of pack replacement. */

	if (delete_redundant && pack_everything & ALL_INTO_ONE) {
		const int hexsz = the_hash_algo->hexsz;
		for_each_string_list_item(item, &existing_nonkept_packs) {
			char *sha1;
			size_t len = strlen(item->string);
			if (len < hexsz)
				continue;
			sha1 = item->string + len - hexsz;
			/*
			 * Mark this pack for deletion, which ensures that this
			 * pack won't be included in a MIDX (if `--write-midx`
			 * was given) and that we will actually delete this pack
			 * (if `-d` was given).
			 */
			if (!string_list_has_string(&names, sha1))
				item->util = (void*)(uintptr_t)((size_t)item->util | DELETE_PACK);
		}
	}

	if (write_midx) {
		struct string_list include = STRING_LIST_INIT_NODUP;
		midx_included_packs(&include, &existing_nonkept_packs,
				    &existing_kept_packs, &names, geometry);

		ret = write_midx_included_packs(&include, geometry,
						refs_snapshot ? get_tempfile_path(refs_snapshot) : NULL,
						show_progress, write_bitmaps > 0);

		if (!ret && write_bitmaps)
			remove_redundant_bitmaps(&include, packdir);

		string_list_clear(&include, 0);

		if (ret)
			goto cleanup;
	}

	reprepare_packed_git(the_repository);

	if (delete_redundant) {
		int opts = 0;
		for_each_string_list_item(item, &existing_nonkept_packs) {
			if (!((uintptr_t)item->util & DELETE_PACK))
				continue;
			remove_redundant_pack(packdir, item->string);
		}

		if (geometry) {
			struct strbuf buf = STRBUF_INIT;

			uint32_t i;
			for (i = 0; i < geometry->split; i++) {
				struct packed_git *p = geometry->pack[i];
				if (string_list_has_string(&names,
							   hash_to_hex(p->hash)))
					continue;

				strbuf_reset(&buf);
				strbuf_addstr(&buf, pack_basename(p));
				strbuf_strip_suffix(&buf, ".pack");

				if ((p->pack_keep) ||
				    (string_list_has_string(&existing_kept_packs,
							    buf.buf)))
					continue;

				remove_redundant_pack(packdir, buf.buf);
			}
			strbuf_release(&buf);
		}
		if (show_progress)
			opts |= PRUNE_PACKED_VERBOSE;
		prune_packed_objects(opts);

		if (!keep_unreachable &&
		    (!(pack_everything & LOOSEN_UNREACHABLE) ||
		     unpack_unreachable) &&
		    is_repository_shallow(the_repository))
			prune_shallow(PRUNE_QUICK);
	}

	if (run_update_server_info)
		update_server_info(0);

	if (git_env_bool(GIT_TEST_MULTI_PACK_INDEX, 0)) {
		unsigned flags = 0;
		if (git_env_bool(GIT_TEST_MULTI_PACK_INDEX_WRITE_BITMAP, 0))
			flags |= MIDX_WRITE_BITMAP | MIDX_WRITE_REV_INDEX;
		write_midx_file(get_object_directory(), NULL, NULL, flags);
	}

cleanup:
	string_list_clear(&names, 1);
	string_list_clear(&existing_nonkept_packs, 0);
	string_list_clear(&existing_kept_packs, 0);
	clear_pack_geometry(geometry);

	return ret;
}
