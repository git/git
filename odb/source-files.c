#include "git-compat-util.h"
#include "abspath.h"
#include "blob.h"
#include "chdir-notify.h"
#include "config.h"
#include "gettext.h"
#include "lockfile.h"
#include "object-file.h"
#include "odb.h"
#include "odb/source.h"
#include "odb/source-files.h"
#include "odb/source-loose.h"
#include "pack-objects.h"
#include "packfile.h"
#include "path.h"
#include "promisor-remote.h"
#include "repack.h"
#include "run-command.h"
#include "strbuf.h"
#include "string-list.h"
#include "strvec.h"
#include "tree.h"
#include "write-or-die.h"

static void odb_source_files_reparent(const char *name UNUSED,
				      const char *old_cwd,
				      const char *new_cwd,
				      void *cb_data)
{
	struct odb_source_files *files = cb_data;
	char *path = reparent_relative_path(old_cwd, new_cwd,
					    files->base.path);
	free(files->base.path);
	files->base.path = path;
}

static void odb_source_files_free(struct odb_source *source)
{
	struct odb_source_files *files = odb_source_files_downcast(source);
	chdir_notify_unregister(NULL, odb_source_files_reparent, files);
	odb_source_free(&files->loose->base);
	odb_source_free(&files->packed->base);
	odb_source_release(&files->base);
	free(files);
}

static void odb_source_files_close(struct odb_source *source)
{
	struct odb_source_files *files = odb_source_files_downcast(source);
	odb_source_close(&files->loose->base);
	odb_source_close(&files->packed->base);
}

static void odb_source_files_prepare(struct odb_source *source,
				     enum odb_prepare_flags flags)
{
	struct odb_source_files *files = odb_source_files_downcast(source);
	odb_source_prepare(&files->loose->base, flags);
	odb_source_prepare(&files->packed->base, flags);
}

static int odb_source_files_read_object_info(struct odb_source *source,
					     const struct object_id *oid,
					     struct object_info *oi,
					     enum object_info_flags flags)
{
	struct odb_source_files *files = odb_source_files_downcast(source);

	if (!odb_source_read_object_info(&files->packed->base, oid, oi, flags) ||
	    !odb_source_read_object_info(&files->loose->base, oid, oi, flags))
		return 0;

	return -1;
}

static int odb_source_files_read_object_stream(struct odb_read_stream **out,
					       struct odb_source *source,
					       const struct object_id *oid)
{
	struct odb_source_files *files = odb_source_files_downcast(source);
	if (!odb_source_read_object_stream(out, &files->packed->base, oid) ||
	    !odb_source_read_object_stream(out, &files->loose->base, oid))
		return 0;
	return -1;
}

static int odb_source_files_for_each_object(struct odb_source *source,
					    const struct object_info *request,
					    odb_for_each_object_cb cb,
					    void *cb_data,
					    const struct odb_for_each_object_options *opts)
{
	struct odb_source_files *files = odb_source_files_downcast(source);
	int ret;

	if (!(opts->flags & ODB_FOR_EACH_OBJECT_PROMISOR_ONLY)) {
		ret = odb_source_for_each_object(&files->loose->base, request, cb, cb_data, opts);
		if (ret)
			return ret;
	}

	ret = odb_source_for_each_object(&files->packed->base, request, cb, cb_data, opts);
	if (ret)
		return ret;

	return 0;
}

static int odb_source_files_count_objects(struct odb_source *source,
					  enum odb_count_objects_flags flags,
					  unsigned long *out)
{
	struct odb_source_files *files = odb_source_files_downcast(source);
	unsigned long count;
	int ret;

	ret = odb_source_count_objects(&files->packed->base, flags, &count);
	if (ret < 0)
		goto out;

	if (!(flags & ODB_COUNT_OBJECTS_APPROXIMATE)) {
		unsigned long loose_count;

		ret = odb_source_count_objects(&files->loose->base, flags, &loose_count);
		if (ret < 0)
			goto out;

		count += loose_count;
	}

	*out = count;
	ret = 0;

out:
	return ret;
}

static int odb_source_files_find_abbrev_len(struct odb_source *source,
					    const struct object_id *oid,
					    unsigned min_len,
					    unsigned *out)
{
	struct odb_source_files *files = odb_source_files_downcast(source);
	unsigned len = min_len;
	int ret;

	ret = odb_source_find_abbrev_len(&files->packed->base, oid, len, &len);
	if (ret < 0)
		goto out;

	ret = odb_source_find_abbrev_len(&files->loose->base, oid, len, &len);
	if (ret < 0)
		goto out;

	*out = len;
	ret = 0;

out:
	return ret;
}

static int odb_source_files_freshen_object(struct odb_source *source,
					   const struct object_id *oid,
					   const time_t *mtime)
{
	struct odb_source_files *files = odb_source_files_downcast(source);
	if (odb_source_freshen_object(&files->packed->base, oid, mtime) ||
	    odb_source_freshen_object(&files->loose->base, oid, mtime))
		return 1;
	return 0;
}

static int odb_source_files_write_object(struct odb_source *source,
					 const void *buf, size_t len,
					 enum object_type type,
					 const struct object_id *oid,
					 const struct object_id *compat_oid,
					 const time_t *mtime,
					 enum odb_write_object_flags flags)
{
	struct odb_source_files *files = odb_source_files_downcast(source);
	return odb_source_write_object(&files->loose->base, buf, len, type,
				       oid, compat_oid, mtime, flags);
}

static int odb_source_files_write_object_stream(struct odb_source *source,
						struct odb_write_stream *stream,
						size_t len,
						struct object_id *oid)
{
	struct odb_source_files *files = odb_source_files_downcast(source);
	return odb_source_write_object_stream(&files->loose->base, stream, len, oid);
}

static int odb_source_files_begin_transaction(struct odb_source *source,
					      struct odb_transaction **out,
					      enum odb_transaction_flags flags)
{
	return odb_transaction_files_begin(source, out, flags);
}

static int odb_source_files_read_alternates(struct odb_source *source,
					    struct strvec *out)
{
	struct strbuf buf = STRBUF_INIT;
	char *path;

	path = xstrfmt("%s/info/alternates", source->path);
	if (strbuf_read_file(&buf, path, 1024) < 0) {
		warn_on_fopen_errors(path);
		free(path);
		return 0;
	}
	parse_alternates(buf.buf, '\n', source->path, out);

	strbuf_release(&buf);
	free(path);
	return 0;
}

static int odb_source_files_write_alternate(struct odb_source *source,
					    const char *alternate)
{
	struct lock_file lock = LOCK_INIT;
	char *path = xstrfmt("%s/%s", source->path, "info/alternates");
	FILE *in, *out;
	int found = 0;
	int ret;

	repo_hold_lock_file_for_update(source->odb->repo, &lock, path,
				       LOCK_DIE_ON_ERROR);
	out = fdopen_lock_file(&lock, "w");
	if (!out) {
		ret = error_errno(_("unable to fdopen alternates lockfile"));
		goto out;
	}

	in = fopen(path, "r");
	if (in) {
		struct strbuf line = STRBUF_INIT;

		while (strbuf_getline(&line, in) != EOF) {
			if (!strcmp(alternate, line.buf)) {
				found = 1;
				break;
			}
			fprintf_or_die(out, "%s\n", line.buf);
		}

		strbuf_release(&line);
		fclose(in);
	} else if (errno != ENOENT) {
		ret = error_errno(_("unable to read alternates file"));
		goto out;
	}

	if (found) {
		rollback_lock_file(&lock);
	} else {
		fprintf_or_die(out, "%s\n", alternate);
		if (commit_lock_file(&lock)) {
			ret = error_errno(_("unable to move new alternates file into place"));
			goto out;
		}
	}

	ret = 0;

out:
	free(path);
	return ret;
}

static int too_many_loose_objects(struct odb_source_files *files, int limit)
{
	unsigned long loose_count;

	if (limit <= 0)
		return 0;

	if (odb_source_count_objects(&files->loose->base, ODB_COUNT_OBJECTS_APPROXIMATE,
				     &loose_count) < 0)
		return 0;

	/*
	 * This is weird, but stems from legacy behaviour: the GC auto
	 * threshold was always essentially interpreted as if it was rounded up
	 * to the next multiple 256 of, so we retain this behaviour for now.
	 */
	return loose_count > (DIV_ROUND_UP(((unsigned long) limit), 256) * 256);
}

static struct packed_git *find_base_packs(struct odb_source_files *files,
					  struct string_list *packs,
					  unsigned long limit)
{
	struct packfile_list_entry *e;
	struct packed_git *base = NULL;

	for (e = packfile_store_get_packs(files->packed); e; e = e->next) {
		if (e->pack->is_cruft)
			continue;
		if (limit) {
			if ((uintmax_t) e->pack->pack_size >= limit)
				string_list_append(packs, e->pack->pack_name);
		} else if (!base || base->pack_size < e->pack->pack_size) {
			base = e->pack;
		}
	}

	if (base)
		string_list_append(packs, base->pack_name);

	return base;
}

static int too_many_packs(struct odb_source_files *files, int gc_auto_pack_limit)
{
	struct packfile_list_entry *e;
	int cnt = 0;

	if (gc_auto_pack_limit <= 0)
		return 0;

	for (e = packfile_store_get_packs(files->packed); e; e = e->next) {
		if (e->pack->pack_keep)
			continue;
		/*
		 * Perhaps check the size of the pack and count only
		 * very small ones here?
		 */
		cnt++;
	}
	return gc_auto_pack_limit < cnt;
}

static uint64_t total_ram(void)
{
#if defined(HAVE_SYSINFO)
	struct sysinfo si;

	if (!sysinfo(&si)) {
		uint64_t total = si.totalram;

		if (si.mem_unit > 1)
			total *= (uint64_t)si.mem_unit;
		return total;
	}
#elif defined(HAVE_BSD_SYSCTL) && (defined(HW_MEMSIZE) || defined(HW_PHYSMEM) || defined(HW_PHYSMEM64))
	uint64_t physical_memory;
	int mib[2];
	size_t length;

	mib[0] = CTL_HW;
# if defined(HW_MEMSIZE)
	mib[1] = HW_MEMSIZE;
# elif defined(HW_PHYSMEM64)
	mib[1] = HW_PHYSMEM64;
# else
	mib[1] = HW_PHYSMEM;
# endif
	length = sizeof(physical_memory);
	if (!sysctl(mib, 2, &physical_memory, &length, NULL, 0)) {
		if (length == 4) {
			uint32_t mem;

			if (!sysctl(mib, 2, &mem, &length, NULL, 0))
				physical_memory = mem;
		}
		return physical_memory;
	}
#elif defined(GIT_WINDOWS_NATIVE)
	MEMORYSTATUSEX memInfo;

	memInfo.dwLength = sizeof(MEMORYSTATUSEX);
	if (GlobalMemoryStatusEx(&memInfo))
		return memInfo.ullTotalPhys;
#endif
	return 0;
}

static uint64_t estimate_repack_memory(struct odb_source_files *files,
				       struct packed_git *pack)
{
	unsigned long max_delta_cache_size = DEFAULT_DELTA_CACHE_SIZE;
	unsigned long delta_base_cache_limit = DEFAULT_DELTA_BASE_CACHE_LIMIT;
	unsigned long nr_objects;
	size_t os_cache, heap;

	if (odb_source_count_objects(&files->base, ODB_COUNT_OBJECTS_APPROXIMATE,
				     &nr_objects) < 0)
		return 0;

	if (!pack || !nr_objects)
		return 0;

	repo_config_get_ulong(files->base.odb->repo, "pack.deltacachesize",
			      &max_delta_cache_size);
	repo_config_get_ulong(files->base.odb->repo, "core.deltabasecachelimit",
			      &delta_base_cache_limit);

	/*
	 * First we have to scan through at least one pack.
	 * Assume enough room in OS file cache to keep the entire pack
	 * or we may accidentally evict data of other processes from
	 * the cache.
	 */
	os_cache = pack->pack_size + pack->index_size;
	/* then pack-objects needs lots more for book keeping */
	heap = sizeof(struct object_entry) * nr_objects;
	/*
	 * internal rev-list --all --objects takes up some memory too,
	 * let's say half of it is for blobs
	 */
	heap += sizeof(struct blob) * nr_objects / 2;
	/*
	 * and the other half is for trees (commits and tags are
	 * usually insignificant)
	 */
	heap += sizeof(struct tree) * nr_objects / 2;
	/* and then obj_hash[], underestimated in fact */
	heap += sizeof(struct object *) * nr_objects;
	/* revindex is used also */
	heap += (sizeof(off_t) + sizeof(uint32_t)) * nr_objects;
	/*
	 * read_sha1_file() (either at delta calculation phase, or
	 * writing phase) also fills up the delta base cache
	 */
	heap += delta_base_cache_limit;
	/* and of course pack-objects has its own delta cache */
	heap += max_delta_cache_size;

	return os_cache + heap;
}

static int keep_one_pack(struct string_list_item *item, void *data)
{
	struct strvec *args = data;
	strvec_pushf(args, "--keep-pack=%s", basename(item->string));
	return 0;
}

static void add_repack_all_option(struct repository *repo,
				  const struct odb_optimize_options *opts,
				  struct string_list *keep_pack,
				  struct strvec *args)
{
	char *repack_filter = NULL;
	char *repack_filter_to = NULL;

	repo_config_get_string(repo, "gc.repackfilter", &repack_filter);
	repo_config_get_string(repo, "gc.repackfilterto", &repack_filter_to);

	if (opts->prune_expire && !strcmp(opts->prune_expire, "now") &&
	    !(opts->cruft_packs && opts->expire_to))
		strvec_push(args, "-a");
	else if (opts->cruft_packs) {
		strvec_push(args, "--cruft");
		if (opts->prune_expire)
			strvec_pushf(args, "--cruft-expiration=%s", opts->prune_expire);
		if (opts->max_cruft_size)
			strvec_pushf(args, "--max-cruft-size=%lu",
				     opts->max_cruft_size);
		if (opts->expire_to)
			strvec_pushf(args, "--expire-to=%s", opts->expire_to);
	} else {
		strvec_push(args, "-A");
		if (opts->prune_expire)
			strvec_pushf(args, "--unpack-unreachable=%s", opts->prune_expire);
	}

	if (keep_pack)
		for_each_string_list(keep_pack, keep_one_pack, args);

	if (repack_filter && *repack_filter)
		strvec_pushf(args, "--filter=%s", repack_filter);
	if (repack_filter_to && *repack_filter_to)
		strvec_pushf(args, "--filter-to=%s", repack_filter_to);

	free(repack_filter);
	free(repack_filter_to);
}

static void add_repack_incremental_option(struct strvec *args)
{
	strvec_push(args, "--no-write-bitmap-index");
}

bool odb_source_files_optimize_required(struct odb_source *source,
					const struct odb_optimize_options *opts)
{
	struct odb_source_files *files = odb_source_files_downcast(source);
	struct repository *repo = source->odb->repo;

	switch (opts->strategy) {
	case ODB_OPTIMIZE_INCREMENTAL: {
		int gc_auto_threshold = 6700;
		int gc_auto_pack_limit = 50;

		repo_config_get_int(repo, "gc.auto", &gc_auto_threshold);
		repo_config_get_int(repo, "gc.autopacklimit", &gc_auto_pack_limit);

		/*
		 * Setting gc.auto to 0 or negative can disable the
		 * automatic gc.
		 */
		if (gc_auto_threshold <= 0)
			return false;
		if (!too_many_packs(files, gc_auto_pack_limit) &&
		    !too_many_loose_objects(files, gc_auto_threshold))
			return false;

		return true;
	}
	case ODB_OPTIMIZE_GEOMETRIC: {
		struct pack_geometry geometry = {
			.split_factor = 2,
		};
		struct pack_objects_args po_args = {
			.local = 1,
		};
		struct existing_packs existing_packs = EXISTING_PACKS_INIT;
		struct string_list kept_packs = STRING_LIST_INIT_DUP;
		int auto_value = 100;
		bool ret;

		repo_config_get_int(repo, "maintenance.geometric-repack.auto",
				    &auto_value);
		if (!auto_value)
			return false;
		if (auto_value < 0)
			return true;

		repo_config_get_int(repo, "maintenance.geometric-repack.splitFactor",
				    &geometry.split_factor);

		existing_packs.repo = repo;
		existing_packs_collect(&existing_packs, &kept_packs);
		pack_geometry_init(&geometry, &existing_packs, &po_args);
		pack_geometry_split(&geometry);

		/*
		 * When we'd merge at least two packs with one another we always
		 * perform the repack.
		 */
		if (geometry.split) {
			ret = true;
			goto out;
		}

		/*
		 * Otherwise, we estimate the number of loose objects to determine
		 * whether we want to create a new packfile or not.
		 */
		if (too_many_loose_objects(files, auto_value)) {
			ret = true;
			goto out;
		}

		ret = false;

	out:
		existing_packs_release(&existing_packs);
		pack_geometry_release(&geometry);
		return ret;
	}
	default:
		BUG("unknown maintenance strategy '%d'", opts->strategy);
	}
}

int odb_source_files_optimize(struct odb_source *source,
			      const struct odb_optimize_options *opts)
{
	struct odb_source_files *files = odb_source_files_downcast(source);
	struct repository *repo = source->odb->repo;
	struct child_process repack_cmd = CHILD_PROCESS_INIT;
	unsigned long big_pack_threshold = 0;
	int gc_auto_threshold = 6700;
	int gc_auto_pack_limit = 50;
	int ret;

	repo_config_get_int(repo, "gc.auto", &gc_auto_threshold);
	repo_config_get_int(repo, "gc.autopacklimit", &gc_auto_pack_limit);
	repo_config_get_ulong(repo, "gc.bigpackthreshold", &big_pack_threshold);

	if (repo->repository_format_precious_objects)
		return 0;

	repack_cmd.git_cmd = 1;
	repack_cmd.odb_to_close = repo->objects;

	strvec_pushl(&repack_cmd.args, "repack", "-d", "-l", NULL);
	if (opts->flags & ODB_OPTIMIZE_NO_REUSE_DELTAS)
		strvec_push(&repack_cmd.args, "-f");
	if (opts->depth > 0)
		strvec_pushf(&repack_cmd.args, "--depth=%d", opts->depth);
	if (opts->window > 0)
		strvec_pushf(&repack_cmd.args, "--window=%d", opts->window);
	if (!(opts->flags & ODB_OPTIMIZE_VERBOSE))
		strvec_push(&repack_cmd.args, "-q");

	/*
	 * There's three cases we need to consider:
	 *
	 *   - If we're invoked without `--auto` we'll need to perform a full
	 *     repack.
	 *
	 *   - If we're invoked with `--auto` and there's too many packs, then
	 *     we perform a full repack, as well.
	 *
	 *   - Otherwise we perform an incremental repack.
	 */
	switch (opts->strategy) {
	case ODB_OPTIMIZE_INCREMENTAL:
		if (!(opts->flags & ODB_OPTIMIZE_AUTO)) {
			struct string_list keep_pack = STRING_LIST_INIT_NODUP;

			if (opts->keep_largest_pack != -1) {
				if (opts->keep_largest_pack)
					find_base_packs(files, &keep_pack, 0);
			} else if (big_pack_threshold) {
				find_base_packs(files, &keep_pack, big_pack_threshold);
			}

			add_repack_all_option(repo, opts, &keep_pack, &repack_cmd.args);
			string_list_clear(&keep_pack, 0);
		} else {
			if (too_many_packs(files, gc_auto_pack_limit)) {
				struct string_list keep_pack = STRING_LIST_INIT_NODUP;

				if (big_pack_threshold) {
					find_base_packs(files, &keep_pack, big_pack_threshold);
					if (keep_pack.nr >= (unsigned long) gc_auto_pack_limit) {
						string_list_clear(&keep_pack, 0);
						find_base_packs(files, &keep_pack, 0);
					}
				} else {
					struct packed_git *p = find_base_packs(files, &keep_pack, 0);
					uint64_t mem_have, mem_want;

					mem_have = total_ram();
					mem_want = estimate_repack_memory(files, p);

					/*
					 * Only allow 1/2 of memory for pack-objects, leave
					 * the rest for the OS and other processes in the
					 * system.
					 */
					if (!mem_have || mem_want < mem_have / 2)
						string_list_clear(&keep_pack, 0);
				}

				add_repack_all_option(repo, opts, &keep_pack, &repack_cmd.args);
				string_list_clear(&keep_pack, 0);
			} else {
				add_repack_incremental_option(&repack_cmd.args);
			}
		}

		break;
	case ODB_OPTIMIZE_GEOMETRIC: {
		struct pack_geometry geometry = {
			.split_factor = 2,
		};
		struct pack_objects_args po_args = {
			.local = 1,
		};
		struct existing_packs existing_packs = EXISTING_PACKS_INIT;
		struct string_list kept_packs = STRING_LIST_INIT_DUP;

		repo_config_get_int(repo, "maintenance.geometric-repack.splitFactor",
				    &geometry.split_factor);

		existing_packs.repo = repo;
		existing_packs_collect(&existing_packs, &kept_packs);
		pack_geometry_init(&geometry, &existing_packs, &po_args);
		pack_geometry_split(&geometry);

		if (geometry.split < geometry.pack_nr) {
			strvec_pushf(&repack_cmd.args, "--geometric=%d",
				     geometry.split_factor);
		} else {
			add_repack_all_option(repo, opts, NULL, &repack_cmd.args);
		}
		if (repo->settings.core_multi_pack_index)
			strvec_push(&repack_cmd.args, "--write-midx");

		existing_packs_release(&existing_packs);
		pack_geometry_release(&geometry);
		break;
	}
	default:
		die("unknown maintenance strategy '%d'", opts->strategy);
	}

	if (run_command(&repack_cmd)) {
		ret = error("failed to run %s", repack_cmd.args.v[0]);
		goto out;
	}

	/* Geometric repacking uses cruft packs, so we don't have to prune separately. */
	if (opts->strategy != ODB_OPTIMIZE_GEOMETRIC && opts->prune_expire) {
		struct child_process prune_cmd = CHILD_PROCESS_INIT;

		strvec_pushl(&prune_cmd.args, "prune", "--expire", NULL);
		/* run `git prune` even if using cruft packs */
		strvec_push(&prune_cmd.args, opts->prune_expire);
		if (!(opts->flags & ODB_OPTIMIZE_VERBOSE))
			strvec_push(&prune_cmd.args, "--no-progress");
		if (repo_has_promisor_remote(repo))
			strvec_push(&prune_cmd.args,
				    "--exclude-promisor-objects");
		prune_cmd.git_cmd = 1;

		if (run_command(&prune_cmd)) {
			ret = error("failed to run %s", prune_cmd.args.v[0]);
			goto out;
		}
	}

	if (opts->flags & ODB_OPTIMIZE_AUTO && too_many_loose_objects(files, gc_auto_threshold))
		warning(_("There are too many unreachable loose objects; "
			"run 'git prune' to remove them."));

	ret = 0;

out:
	return ret;
}

struct odb_source_files *odb_source_files_new(struct object_database *odb,
					      const char *path,
					      bool local)
{
	struct odb_source_files *files;

	CALLOC_ARRAY(files, 1);
	odb_source_init(&files->base, odb, ODB_SOURCE_FILES, path, local);
	files->loose = odb_source_loose_new(odb, path, local);
	files->packed = odb_source_packed_new(odb, path, local);

	files->base.free = odb_source_files_free;
	files->base.close = odb_source_files_close;
	files->base.prepare = odb_source_files_prepare;
	files->base.read_object_info = odb_source_files_read_object_info;
	files->base.read_object_stream = odb_source_files_read_object_stream;
	files->base.for_each_object = odb_source_files_for_each_object;
	files->base.count_objects = odb_source_files_count_objects;
	files->base.find_abbrev_len = odb_source_files_find_abbrev_len;
	files->base.freshen_object = odb_source_files_freshen_object;
	files->base.write_object = odb_source_files_write_object;
	files->base.write_object_stream = odb_source_files_write_object_stream;
	files->base.begin_transaction = odb_source_files_begin_transaction;
	files->base.read_alternates = odb_source_files_read_alternates;
	files->base.write_alternate = odb_source_files_write_alternate;
	files->base.optimize = odb_source_files_optimize;
	files->base.optimize_required = odb_source_files_optimize_required;

	/*
	 * Ideally, we would only ever store absolute paths in the source. This
	 * is not (yet) possible though because we access and assume relative
	 * paths in the primary ODB source in some user-facing functionality.
	 */
	if (!is_absolute_path(path))
		chdir_notify_register(NULL, odb_source_files_reparent, files);

	return files;
}
