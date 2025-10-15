#define USE_THE_REPOSITORY_VARIABLE
#define DISABLE_SIGN_COMPARE_WARNINGS

#include "builtin.h"
#include "config.h"
#include "environment.h"
#include "gettext.h"
#include "hex.h"
#include "parse-options.h"
#include "path.h"
#include "run-command.h"
#include "server-info.h"
#include "strbuf.h"
#include "string-list.h"
#include "strvec.h"
#include "midx.h"
#include "packfile.h"
#include "prune-packed.h"
#include "odb.h"
#include "promisor-remote.h"
#include "repack.h"
#include "shallow.h"
#include "pack.h"
#include "pack-bitmap.h"
#include "refs.h"
#include "list-objects-filter-options.h"

#define ALL_INTO_ONE 1
#define LOOSEN_UNREACHABLE 2
#define PACK_CRUFT 4

#define DELETE_PACK 1
#define RETAIN_PACK 2

static int pack_everything;
static int pack_kept_objects = -1;
static int write_bitmaps = -1;
static int use_delta_islands;
static int run_update_server_info = 1;
static char *packdir, *packtmp_name, *packtmp;
static int midx_must_contain_cruft = 1;

static const char *const git_repack_usage[] = {
	N_("git repack [-a] [-A] [-d] [-f] [-F] [-l] [-n] [-q] [-b] [-m]\n"
	   "[--window=<n>] [--depth=<n>] [--threads=<n>] [--keep-pack=<pack-name>]\n"
	   "[--write-midx] [--name-hash-version=<n>] [--path-walk]"),
	NULL
};

static const char incremental_bitmap_conflict_error[] = N_(
"Incremental repacks are incompatible with bitmap indexes.  Use\n"
"--no-write-bitmap-index or disable the pack.writeBitmaps configuration."
);

struct repack_config_ctx {
	struct pack_objects_args *po_args;
	struct pack_objects_args *cruft_po_args;
};

static int repack_config(const char *var, const char *value,
			 const struct config_context *ctx, void *cb)
{
	struct repack_config_ctx *repack_ctx = cb;
	struct pack_objects_args *po_args = repack_ctx->po_args;
	struct pack_objects_args *cruft_po_args = repack_ctx->cruft_po_args;
	if (!strcmp(var, "repack.usedeltabaseoffset")) {
		po_args->delta_base_offset = git_config_bool(var, value);
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
	if (!strcmp(var, "repack.cruftwindow")) {
		free(cruft_po_args->window);
		return git_config_string(&cruft_po_args->window, var, value);
	}
	if (!strcmp(var, "repack.cruftwindowmemory")) {
		free(cruft_po_args->window_memory);
		return git_config_string(&cruft_po_args->window_memory, var, value);
	}
	if (!strcmp(var, "repack.cruftdepth")) {
		free(cruft_po_args->depth);
		return git_config_string(&cruft_po_args->depth, var, value);
	}
	if (!strcmp(var, "repack.cruftthreads")) {
		free(cruft_po_args->threads);
		return git_config_string(&cruft_po_args->threads, var, value);
	}
	if (!strcmp(var, "repack.midxmustcontaincruft")) {
		midx_must_contain_cruft = git_config_bool(var, value);
		return 0;
	}
	return git_default_config(var, value, ctx, cb);
}

static int midx_has_unknown_packs(char **midx_pack_names,
				  size_t midx_pack_names_nr,
				  struct string_list *include,
				  struct pack_geometry *geometry,
				  struct existing_packs *existing)
{
	size_t i;

	string_list_sort(include);

	for (i = 0; i < midx_pack_names_nr; i++) {
		const char *pack_name = midx_pack_names[i];

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
				struct existing_packs *existing,
				char **midx_pack_names,
				size_t midx_pack_names_nr,
				struct string_list *names,
				struct pack_geometry *geometry)
{
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

	if (midx_must_contain_cruft ||
	    midx_has_unknown_packs(midx_pack_names, midx_pack_names_nr,
				   include, geometry, existing)) {
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

static int write_midx_included_packs(struct string_list *include,
				     struct pack_geometry *geometry,
				     struct string_list *names,
				     const char *refs_snapshot,
				     int show_progress, int write_bitmaps)
{
	struct child_process cmd = CHILD_PROCESS_INIT;
	struct string_list_item *item;
	struct packed_git *preferred = pack_geometry_preferred_pack(geometry);
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

	if (preferred)
		strvec_pushf(&cmd.args, "--preferred-pack=%s",
			     pack_basename(preferred));
	else if (names->nr) {
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
		for_each_string_list_item(item, names) {
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

static int finish_pack_objects_cmd(const struct git_hash_algo *algop,
				   struct child_process *cmd,
				   struct string_list *names,
				   int local)
{
	FILE *out;
	struct strbuf line = STRBUF_INIT;

	out = xfdopen(cmd->out, "r");
	while (strbuf_getline_lf(&line, out) != EOF) {
		struct string_list_item *item;

		if (line.len != algop->hexsz)
			die(_("repack: Expecting full hex object ID lines only "
			      "from pack-objects."));
		/*
		 * Avoid putting packs written outside of the repository in the
		 * list of names.
		 */
		if (local) {
			item = string_list_append(names, line.buf);
			item->util = generated_pack_populate(line.buf, packtmp);
		}
	}
	fclose(out);

	strbuf_release(&line);

	return finish_command(cmd);
}

static int write_filtered_pack(const struct pack_objects_args *args,
			       const char *destination,
			       const char *pack_prefix,
			       struct existing_packs *existing,
			       struct string_list *names)
{
	struct child_process cmd = CHILD_PROCESS_INIT;
	struct string_list_item *item;
	FILE *in;
	int ret;
	const char *caret;
	const char *scratch;
	int local = skip_prefix(destination, packdir, &scratch);

	prepare_pack_objects(&cmd, args, destination);

	strvec_push(&cmd.args, "--stdin-packs");

	if (!pack_kept_objects)
		strvec_push(&cmd.args, "--honor-pack-keep");
	for_each_string_list_item(item, &existing->kept_packs)
		strvec_pushf(&cmd.args, "--keep-pack=%s", item->string);

	cmd.in = -1;

	ret = start_command(&cmd);
	if (ret)
		return ret;

	/*
	 * Here 'names' contains only the pack(s) that were just
	 * written, which is exactly the packs we want to keep. Also
	 * 'existing_kept_packs' already contains the packs in
	 * 'keep_pack_list'.
	 */
	in = xfdopen(cmd.in, "w");
	for_each_string_list_item(item, names)
		fprintf(in, "^%s-%s.pack\n", pack_prefix, item->string);
	for_each_string_list_item(item, &existing->non_kept_packs)
		fprintf(in, "%s.pack\n", item->string);
	for_each_string_list_item(item, &existing->cruft_packs)
		fprintf(in, "%s.pack\n", item->string);
	caret = pack_kept_objects ? "" : "^";
	for_each_string_list_item(item, &existing->kept_packs)
		fprintf(in, "%s%s.pack\n", caret, item->string);
	fclose(in);

	return finish_pack_objects_cmd(existing->repo->hash_algo, &cmd, names,
				       local);
}

static void combine_small_cruft_packs(FILE *in, size_t combine_cruft_below_size,
				      struct existing_packs *existing)
{
	struct packfile_store *packs = existing->repo->objects->packfiles;
	struct packed_git *p;
	struct strbuf buf = STRBUF_INIT;
	size_t i;

	for (p = packfile_store_get_all_packs(packs); p; p = p->next) {
		if (!(p->is_cruft && p->pack_local))
			continue;

		strbuf_reset(&buf);
		strbuf_addstr(&buf, pack_basename(p));
		strbuf_strip_suffix(&buf, ".pack");

		if (!string_list_has_string(&existing->cruft_packs, buf.buf))
			continue;

		if (p->pack_size < combine_cruft_below_size) {
			fprintf(in, "-%s\n", pack_basename(p));
		} else {
			existing_packs_retain_cruft(existing, p);
			fprintf(in, "%s\n", pack_basename(p));
		}
	}

	for (i = 0; i < existing->non_kept_packs.nr; i++)
		fprintf(in, "-%s.pack\n",
			existing->non_kept_packs.items[i].string);

	strbuf_release(&buf);
}

static int write_cruft_pack(const struct pack_objects_args *args,
			    const char *destination,
			    const char *pack_prefix,
			    const char *cruft_expiration,
			    unsigned long combine_cruft_below_size,
			    struct string_list *names,
			    struct existing_packs *existing)
{
	struct child_process cmd = CHILD_PROCESS_INIT;
	struct string_list_item *item;
	FILE *in;
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
	if (combine_cruft_below_size && !cruft_expiration) {
		combine_small_cruft_packs(in, combine_cruft_below_size,
					  existing);
	} else {
		for_each_string_list_item(item, &existing->non_kept_packs)
			fprintf(in, "-%s.pack\n", item->string);
		for_each_string_list_item(item, &existing->cruft_packs)
			fprintf(in, "-%s.pack\n", item->string);
	}
	for_each_string_list_item(item, &existing->kept_packs)
		fprintf(in, "%s.pack\n", item->string);
	fclose(in);

	return finish_pack_objects_cmd(existing->repo->hash_algo, &cmd, names,
				       local);
}

static const char *find_pack_prefix(const char *packdir, const char *packtmp)
{
	const char *pack_prefix;
	if (!skip_prefix(packtmp, packdir, &pack_prefix))
		die(_("pack prefix %s does not begin with objdir %s"),
		    packtmp, packdir);
	if (*pack_prefix == '/')
		pack_prefix++;
	return pack_prefix;
}

int cmd_repack(int argc,
	       const char **argv,
	       const char *prefix,
	       struct repository *repo)
{
	struct child_process cmd = CHILD_PROCESS_INIT;
	struct string_list_item *item;
	struct string_list names = STRING_LIST_INIT_DUP;
	struct existing_packs existing = EXISTING_PACKS_INIT;
	struct pack_geometry geometry = { 0 };
	struct tempfile *refs_snapshot = NULL;
	int i, ret;
	int show_progress;
	char **midx_pack_names = NULL;
	size_t midx_pack_names_nr = 0;

	/* variables to be filled by option parsing */
	struct repack_config_ctx config_ctx;
	int delete_redundant = 0;
	const char *unpack_unreachable = NULL;
	int keep_unreachable = 0;
	struct string_list keep_pack_list = STRING_LIST_INIT_NODUP;
	struct pack_objects_args po_args = PACK_OBJECTS_ARGS_INIT;
	struct pack_objects_args cruft_po_args = PACK_OBJECTS_ARGS_INIT;
	int write_midx = 0;
	const char *cruft_expiration = NULL;
	const char *expire_to = NULL;
	const char *filter_to = NULL;
	const char *opt_window = NULL;
	const char *opt_window_memory = NULL;
	const char *opt_depth = NULL;
	const char *opt_threads = NULL;
	unsigned long combine_cruft_below_size = 0ul;

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
				N_("with --cruft, expire objects older than this")),
		OPT_UNSIGNED(0, "combine-cruft-below-size",
			     &combine_cruft_below_size,
			     N_("with --cruft, only repack cruft packs smaller than this")),
		OPT_UNSIGNED(0, "max-cruft-size", &cruft_po_args.max_pack_size,
			     N_("with --cruft, limit the size of new cruft packs")),
		OPT_BOOL('d', NULL, &delete_redundant,
				N_("remove redundant packs, and run git-prune-packed")),
		OPT_BOOL('f', NULL, &po_args.no_reuse_delta,
				N_("pass --no-reuse-delta to git-pack-objects")),
		OPT_BOOL('F', NULL, &po_args.no_reuse_object,
				N_("pass --no-reuse-object to git-pack-objects")),
		OPT_INTEGER(0, "name-hash-version", &po_args.name_hash_version,
				N_("specify the name hash version to use for grouping similar objects by path")),
		OPT_BOOL(0, "path-walk", &po_args.path_walk,
				N_("pass --path-walk to git-pack-objects")),
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
		OPT_STRING(0, "window", &opt_window, N_("n"),
				N_("size of the window used for delta compression")),
		OPT_STRING(0, "window-memory", &opt_window_memory, N_("bytes"),
				N_("same as the above, but limit memory size instead of entries count")),
		OPT_STRING(0, "depth", &opt_depth, N_("n"),
				N_("limits the maximum delta depth")),
		OPT_STRING(0, "threads", &opt_threads, N_("n"),
				N_("limits the maximum number of threads")),
		OPT_UNSIGNED(0, "max-pack-size", &po_args.max_pack_size,
			     N_("maximum size of each packfile")),
		OPT_PARSE_LIST_OBJECTS_FILTER(&po_args.filter_options),
		OPT_BOOL(0, "pack-kept-objects", &pack_kept_objects,
				N_("repack objects in packs marked with .keep")),
		OPT_STRING_LIST(0, "keep-pack", &keep_pack_list, N_("name"),
				N_("do not repack this pack")),
		OPT_INTEGER('g', "geometric", &geometry.split_factor,
			    N_("find a geometric progression with factor <N>")),
		OPT_BOOL('m', "write-midx", &write_midx,
			   N_("write a multi-pack index of the resulting packs")),
		OPT_STRING(0, "expire-to", &expire_to, N_("dir"),
			   N_("pack prefix to store a pack containing pruned objects")),
		OPT_STRING(0, "filter-to", &filter_to, N_("dir"),
			   N_("pack prefix to store a pack containing filtered out objects")),
		OPT_END()
	};

	list_objects_filter_init(&po_args.filter_options);

	memset(&config_ctx, 0, sizeof(config_ctx));
	config_ctx.po_args = &po_args;
	config_ctx.cruft_po_args = &cruft_po_args;

	repo_config(repo, repack_config, &config_ctx);

	argc = parse_options(argc, argv, prefix, builtin_repack_options,
				git_repack_usage, 0);

	po_args.window = xstrdup_or_null(opt_window);
	po_args.window_memory = xstrdup_or_null(opt_window_memory);
	po_args.depth = xstrdup_or_null(opt_depth);
	po_args.threads = xstrdup_or_null(opt_threads);

	if (delete_redundant && repo->repository_format_precious_objects)
		die(_("cannot delete packs in a precious-objects repo"));

	die_for_incompatible_opt3(unpack_unreachable || (pack_everything & LOOSEN_UNREACHABLE), "-A",
				  keep_unreachable, "-k/--keep-unreachable",
				  pack_everything & PACK_CRUFT, "--cruft");

	if (pack_everything & PACK_CRUFT)
		pack_everything |= ALL_INTO_ONE;

	if (write_bitmaps < 0) {
		if (!write_midx &&
		    (!(pack_everything & ALL_INTO_ONE) || !is_bare_repository()))
			write_bitmaps = 0;
	}
	if (pack_kept_objects < 0)
		pack_kept_objects = write_bitmaps > 0 && !write_midx;

	if (write_bitmaps && !(pack_everything & ALL_INTO_ONE) && !write_midx)
		die(_(incremental_bitmap_conflict_error));

	if (write_bitmaps && po_args.local &&
	    odb_has_alternates(repo->objects)) {
		/*
		 * When asked to do a local repack, but we have
		 * packfiles that are inherited from an alternate, then
		 * we cannot guarantee that the multi-pack-index would
		 * have full coverage of all objects. We thus disable
		 * writing bitmaps in that case.
		 */
		warning(_("disabling bitmap writing, as some objects are not being packed"));
		write_bitmaps = 0;
	}

	if (write_midx && write_bitmaps) {
		struct strbuf path = STRBUF_INIT;

		strbuf_addf(&path, "%s/%s_XXXXXX",
			    repo_get_object_directory(repo),
			    "bitmap-ref-tips");

		refs_snapshot = xmks_tempfile(path.buf);
		midx_snapshot_refs(repo, refs_snapshot);

		strbuf_release(&path);
	}

	packdir = mkpathdup("%s/pack", repo_get_object_directory(repo));
	packtmp_name = xstrfmt(".tmp-%d-pack", (int)getpid());
	packtmp = mkpathdup("%s/%s", packdir, packtmp_name);

	existing.repo = repo;
	existing_packs_collect(&existing, &keep_pack_list);

	if (geometry.split_factor) {
		if (pack_everything)
			die(_("options '%s' and '%s' cannot be used together"), "--geometric", "-A/-a");
		pack_geometry_init(&geometry, &existing, &po_args,
				   pack_kept_objects);
		pack_geometry_split(&geometry);
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
	if (!geometry.split_factor) {
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
	if (repo_has_promisor_remote(repo))
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
		repack_promisor_objects(repo, &po_args, &names, packtmp);

		if (existing_packs_has_non_kept(&existing) &&
		    delete_redundant &&
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
			}
		}

		if (keep_unreachable && delete_redundant &&
		    !(pack_everything & PACK_CRUFT))
			strvec_push(&cmd.args, "--pack-loose-unreachable");
	} else if (geometry.split_factor) {
		if (midx_must_contain_cruft)
			strvec_push(&cmd.args, "--stdin-packs");
		else
			strvec_push(&cmd.args, "--stdin-packs=follow");
		strvec_push(&cmd.args, "--unpacked");
	} else {
		strvec_push(&cmd.args, "--unpacked");
		strvec_push(&cmd.args, "--incremental");
	}

	if (po_args.filter_options.choice)
		strvec_pushf(&cmd.args, "--filter=%s",
			     expand_list_objects_filter_spec(&po_args.filter_options));
	else if (filter_to)
		die(_("option '%s' can only be used along with '%s'"), "--filter-to", "--filter");

	if (geometry.split_factor)
		cmd.in = -1;
	else
		cmd.no_stdin = 1;

	ret = start_command(&cmd);
	if (ret)
		goto cleanup;

	if (geometry.split_factor) {
		FILE *in = xfdopen(cmd.in, "w");
		/*
		 * The resulting pack should contain all objects in packs that
		 * are going to be rolled up, but exclude objects in packs which
		 * are being left alone.
		 */
		for (i = 0; i < geometry.split; i++)
			fprintf(in, "%s\n", pack_basename(geometry.pack[i]));
		for (i = geometry.split; i < geometry.pack_nr; i++)
			fprintf(in, "^%s\n", pack_basename(geometry.pack[i]));
		fclose(in);
	}

	ret = finish_pack_objects_cmd(repo->hash_algo, &cmd, &names, 1);
	if (ret)
		goto cleanup;

	if (!names.nr) {
		if (!po_args.quiet)
			printf_ln(_("Nothing new to pack."));
		/*
		 * If we didn't write any new packs, the non-cruft packs
		 * may refer to once-unreachable objects in the cruft
		 * pack(s).
		 *
		 * If there isn't already a MIDX, the one we write
		 * must include the cruft pack(s), in case the
		 * non-cruft pack(s) refer to once-cruft objects.
		 *
		 * If there is already a MIDX, we can punt here, since
		 * midx_has_unknown_packs() will make the decision for
		 * us.
		 */
		if (!get_multi_pack_index(repo->objects->sources))
			midx_must_contain_cruft = 1;
	}

	if (pack_everything & PACK_CRUFT) {
		const char *pack_prefix = find_pack_prefix(packdir, packtmp);

		if (!cruft_po_args.window)
			cruft_po_args.window = xstrdup_or_null(po_args.window);
		if (!cruft_po_args.window_memory)
			cruft_po_args.window_memory = xstrdup_or_null(po_args.window_memory);
		if (!cruft_po_args.depth)
			cruft_po_args.depth = xstrdup_or_null(po_args.depth);
		if (!cruft_po_args.threads)
			cruft_po_args.threads = xstrdup_or_null(po_args.threads);
		if (!cruft_po_args.max_pack_size)
			cruft_po_args.max_pack_size = po_args.max_pack_size;

		cruft_po_args.local = po_args.local;
		cruft_po_args.quiet = po_args.quiet;
		cruft_po_args.delta_base_offset = po_args.delta_base_offset;

		ret = write_cruft_pack(&cruft_po_args, packtmp, pack_prefix,
				       cruft_expiration,
				       combine_cruft_below_size, &names,
				       &existing);
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
			 *
			 * Pretend we don't have a `--combine-cruft-below-size`
			 * argument, since we're not selectively combining
			 * anything based on size to generate the limbo cruft
			 * pack, but rather removing all cruft packs from the
			 * main repository regardless of size.
			 */
			ret = write_cruft_pack(&cruft_po_args, expire_to,
					       pack_prefix,
					       NULL,
					       0ul,
					       &names,
					       &existing);
			if (ret)
				goto cleanup;
		}
	}

	if (po_args.filter_options.choice) {
		if (!filter_to)
			filter_to = packtmp;

		ret = write_filtered_pack(&po_args,
					  filter_to,
					  find_pack_prefix(packdir, packtmp),
					  &existing,
					  &names);
		if (ret)
			goto cleanup;
	}

	string_list_sort(&names);

	if (get_multi_pack_index(repo->objects->sources)) {
		struct multi_pack_index *m =
			get_multi_pack_index(repo->objects->sources);

		ALLOC_ARRAY(midx_pack_names,
			    m->num_packs + m->num_packs_in_base);

		for (; m; m = m->base_midx)
			for (uint32_t i = 0; i < m->num_packs; i++)
				midx_pack_names[midx_pack_names_nr++] =
					xstrdup(m->pack_names[i]);
	}

	close_object_store(repo->objects);

	/*
	 * Ok we have prepared all new packfiles.
	 */
	for_each_string_list_item(item, &names)
		generated_pack_install(item->util, item->string, packdir,
				       packtmp);
	/* End of pack replacement. */

	if (delete_redundant && pack_everything & ALL_INTO_ONE)
		existing_packs_mark_for_deletion(&existing, &names);

	if (write_midx) {
		struct string_list include = STRING_LIST_INIT_DUP;
		midx_included_packs(&include, &existing, midx_pack_names,
				    midx_pack_names_nr, &names, &geometry);

		ret = write_midx_included_packs(&include, &geometry, &names,
						refs_snapshot ? get_tempfile_path(refs_snapshot) : NULL,
						show_progress, write_bitmaps > 0);

		if (!ret && write_bitmaps)
			remove_redundant_bitmaps(&include, packdir);

		string_list_clear(&include, 0);

		if (ret)
			goto cleanup;
	}

	odb_reprepare(repo->objects);

	if (delete_redundant) {
		int opts = 0;
		existing_packs_remove_redundant(&existing, packdir);

		if (geometry.split_factor)
			pack_geometry_remove_redundant(&geometry, &names,
						       &existing, packdir);
		if (show_progress)
			opts |= PRUNE_PACKED_VERBOSE;
		prune_packed_objects(opts);

		if (!keep_unreachable &&
		    (!(pack_everything & LOOSEN_UNREACHABLE) ||
		     unpack_unreachable) &&
		    is_repository_shallow(repo))
			prune_shallow(PRUNE_QUICK);
	}

	if (run_update_server_info)
		update_server_info(repo, 0);

	if (git_env_bool(GIT_TEST_MULTI_PACK_INDEX, 0)) {
		unsigned flags = 0;
		if (git_env_bool(GIT_TEST_MULTI_PACK_INDEX_WRITE_INCREMENTAL, 0))
			flags |= MIDX_WRITE_INCREMENTAL;
		write_midx_file(repo->objects->sources,
				NULL, NULL, flags);
	}

cleanup:
	string_list_clear(&keep_pack_list, 0);
	string_list_clear(&names, 1);
	existing_packs_release(&existing);
	pack_geometry_release(&geometry);
	for (size_t i = 0; i < midx_pack_names_nr; i++)
		free(midx_pack_names[i]);
	free(midx_pack_names);
	pack_objects_args_release(&po_args);
	pack_objects_args_release(&cruft_po_args);

	return ret;
}
