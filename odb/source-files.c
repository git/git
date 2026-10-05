#include "git-compat-util.h"
#include "abspath.h"
#include "blob.h"
#include "chdir-notify.h"
#include "config.h"
#include "gettext.h"
#include "hex.h"
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
#include "quote.h"
#include "repack.h"
#include "run-command.h"
#include "strbuf.h"
#include "string-list.h"
#include "strmap.h"
#include "strvec.h"
#include "tree.h"
#include "write-or-die.h"

struct odb_files_dir *odb_files_dir_new(struct object_database *odb,
					const char *path, bool local)
{
	struct odb_files_dir *dir;

	CALLOC_ARRAY(dir, 1);
	dir->abspath = absolute_pathdup(path);
	dir->local = local;
	dir->loose = odb_source_loose_new(odb, path, local);
	dir->packed = odb_source_packed_new(odb, path, local);

	return dir;
}

void odb_files_dir_free(struct odb_files_dir *dir)
{
	if (!dir)
		return;
	odb_source_free(&dir->loose->base);
	odb_source_free(&dir->packed->base);
	free(dir->abspath);
	free(dir);
}

static void odb_source_files_reparent(const char *old_cwd,
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

	chdir_notify_unregister(odb_source_files_reparent, files);

	while (files->dirs) {
		struct odb_files_dir *next = files->dirs->next;
		odb_files_dir_free(files->dirs);
		files->dirs = next;
	}
	hashmap_clear(&files->dirs_by_path);

	odb_source_release(&files->base);
	free(files);
}

static void odb_source_files_close(struct odb_source *source)
{
	struct odb_source_files *files = odb_source_files_downcast(source);

	for (struct odb_files_dir *dir = files->dirs; dir; dir = dir->next) {
		odb_source_close(&dir->loose->base);
		odb_source_close(&dir->packed->base);
	}
}

static int odb_source_files_create_on_disk(struct odb_source *source,
					   const struct odb_create_on_disk_options *opts)
{
	struct lock_file alternates_lock = LOCK_INIT;
	struct strbuf path = STRBUF_INIT;
	struct strset seen = STRSET_INIT;
	struct strbuf line = STRBUF_INIT;
	int ret;

	safe_create_dir(source->odb->repo, source->path, 1);

	strbuf_addf(&path, "%s/pack", source->path);
	safe_create_dir(source->odb->repo, path.buf, 1);

	strbuf_reset(&path);
	strbuf_addf(&path, "%s/info", source->path);
	safe_create_dir(source->odb->repo, path.buf, 1);

	if (opts->alternates && opts->alternates->nr) {
		FILE *alternates, *orig;

		strbuf_reset(&path);
		strbuf_addf(&path, "%s/info/alternates", source->path);

		repo_hold_lock_file_for_update(source->odb->repo, &alternates_lock,
					       path.buf, LOCK_DIE_ON_ERROR);

		alternates = fdopen_lock_file(&alternates_lock, "w");
		if (!alternates) {
			ret = error_errno(_("unable to fdopen alternates lockfile"));
			goto out;
		}

		/*
		 * The alternates file may already exist, e.g. when it has been
		 * seeded from a template directory. Read any preexisting
		 * entries so that we don't end up writing duplicates.
		 */
		orig = fopen(path.buf, "r");
		if (orig) {
			while (strbuf_getline(&line, orig) != EOF) {
				strset_add(&seen, line.buf);
				fprintf(alternates, "%s\n", line.buf);
			}

			if (ferror(orig)) {
				ret = error_errno(_("unable to read alternates file"));
				fclose(orig);
				goto out;
			}

			fclose(orig);
		} else if (errno != ENOENT) {
			ret = error_errno(_("unable to read alternates file"));
			goto out;
		}

		for (size_t i = 0; i < opts->alternates->nr; i++) {
			const char *alternate = opts->alternates->v[i];
			if (!strset_add(&seen, alternate))
				continue;
			fprintf(alternates, "%s\n", alternate);
		}

		if (ferror(alternates)) {
			ret = error_errno(_("unable to write alternates file"));
			goto out;
		}

		if (commit_lock_file(&alternates_lock)) {
			ret = error_errno(_("unable to commit alternates file"));
			goto out;
		}
	}

	/* Reprepare the object database to activate alternates. */
	odb_reprepare(source->odb);

	ret = 0;

out:
	rollback_lock_file(&alternates_lock);
	strbuf_release(&line);
	strbuf_release(&path);
	strset_clear(&seen);
	return ret;
}

/*
 * NEEDSWORK: we're using "core.ignoreCase" to deduplicate alternates that
 * _may_ be the same. This requires quite a bit of boilerplate for dubious
 * benefit:
 *
 *   - Duplicating alternates should really only lead to regressed performance.
 *
 *   - We don't properly resolve symlinks or mointpoints, so we may still end
 *     up duplicating alternates.
 *
 *   - The value may be lying, in which case we might deduplicate alternates
 *     that are in fact not mapping to the same directory.
 *
 * We should investigate whether we can remove this whole mechanism outright.
 */
static int odb_files_dir_paths_cmp(struct odb_source_files *files,
				   const char *a, const char *b)
{
	if (files->dirs_paths_icase < 0) {
		int icase = 0;
		repo_config_get_bool(files->base.odb->repo, "core.ignorecase", &icase);
		files->dirs_paths_icase = icase;
	}

	return files->dirs_paths_icase ? strcasecmp(a, b) : strcmp(a, b);
}

static int odb_files_dir_by_path_cmp(const void *cb_data,
				     const struct hashmap_entry *entry,
				     const struct hashmap_entry *entry_or_key,
				     const void *keydata)
{
	struct odb_source_files *files = (struct odb_source_files *)cb_data;
	const struct odb_files_dir *dir = container_of(entry, const struct odb_files_dir, by_path_entry);
	const char *path = keydata;

	if (!path)
		path = container_of(entry_or_key, const struct odb_files_dir, by_path_entry)->abspath;

	return odb_files_dir_paths_cmp(files, dir->abspath, path);
}

/*
 * Return non-zero iff the path is usable as an alternate object directory.
 */
static bool odb_files_dir_is_usable(struct odb_source_files *files,
				    const char *path)
{
	struct strbuf normalized_objdir = STRBUF_INIT;
	struct hashmap_entry key;
	bool usable = false;

	strbuf_realpath(&normalized_objdir, files->dirs->abspath, 1);

	/* Detect cases where alternate disappeared */
	if (!is_directory(path)) {
		error(_("object directory %s does not exist; "
			"check .git/objects/info/alternates"),
		      path);
		goto out;
	}

	/*
	 * Prevent the common mistake of listing the same
	 * thing twice, or object directory itself.
	 */
	if (!hashmap_get_size(&files->dirs_by_path)) {
		assert(!files->dirs->next);
		hashmap_entry_init(&files->dirs->by_path_entry,
				   strihash(files->dirs->abspath));
		hashmap_add(&files->dirs_by_path, &files->dirs->by_path_entry);
	}

	if (!odb_files_dir_paths_cmp(files, path, normalized_objdir.buf))
		goto out;

	hashmap_entry_init(&key, strihash(path));
	if (hashmap_get(&files->dirs_by_path, &key, path))
		goto out;

	usable = true;

out:
	strbuf_release(&normalized_objdir);
	return usable;
}

static void parse_alternates(const char *string,
			     int sep,
			     const char *relative_base,
			     struct strvec *out)
{
	struct strbuf pathbuf = STRBUF_INIT;
	struct strbuf buf = STRBUF_INIT;

	if (!string || !*string)
		return;

	while (*string) {
		const char *end;

		strbuf_reset(&buf);
		strbuf_reset(&pathbuf);

		if (*string == '#') {
			/* comment; consume up to next separator */
			end = strchrnul(string, sep);
		} else if (*string == '"' && !unquote_c_style(&buf, string, &end)) {
			/*
			 * quoted path; unquote_c_style has copied the
			 * data for us and set "end". Broken quoting (e.g.,
			 * an entry that doesn't end with a quote) falls
			 * back to the unquoted case below.
			 */
		} else {
			/* normal, unquoted path */
			end = strchrnul(string, sep);
			strbuf_add(&buf, string, end - string);
		}

		if (*end)
			end++;
		string = end;

		if (!buf.len)
			continue;

		if (!is_absolute_path(buf.buf) && relative_base) {
			strbuf_realpath(&pathbuf, relative_base, 1);
			strbuf_addch(&pathbuf, '/');
		}
		strbuf_addbuf(&pathbuf, &buf);

		strbuf_reset(&buf);
		if (!strbuf_realpath(&buf, pathbuf.buf, 0)) {
			error(_("unable to normalize alternate object path: %s"),
			      pathbuf.buf);
			continue;
		}

		/*
		 * The trailing slash after the directory name is given by
		 * this function at the end. Remove duplicates.
		 */
		while (buf.len && buf.buf[buf.len - 1] == '/')
			strbuf_setlen(&buf, buf.len - 1);

		strvec_push(out, buf.buf);
	}

	strbuf_release(&pathbuf);
	strbuf_release(&buf);
}

static int read_alternates(const char *object_dir, struct strvec *out)
{
	struct strbuf buf = STRBUF_INIT;
	char *path;

	path = xstrfmt("%s/info/alternates", object_dir);
	if (strbuf_read_file(&buf, path, 1024) < 0) {
		warn_on_fopen_errors(path);
		free(path);
		return 0;
	}
	parse_alternates(buf.buf, '\n', object_dir, out);

	strbuf_release(&buf);
	free(path);
	return 0;
}

static void odb_add_alternate_recursively(struct odb_source_files *files,
					  const char *path,
					  int depth)
{
	struct odb_files_dir *alternate;
	struct strvec alternates = STRVEC_INIT;

	if (!odb_files_dir_is_usable(files, path))
		goto out;

	alternate = odb_files_dir_new(files->base.odb, path, false);

	/* add the alternate entry */
	*files->dirs_tail = alternate;
	files->dirs_tail = &(alternate->next);

	hashmap_entry_init(&alternate->by_path_entry, strihash(alternate->abspath));
	if (hashmap_get(&files->dirs_by_path, &alternate->by_path_entry,
			alternate->abspath))
		BUG("object directory must not yet exist");
	hashmap_add(&files->dirs_by_path, &alternate->by_path_entry);

	/* recursively add alternates */
	read_alternates(alternate->abspath, &alternates);
	if (alternates.nr && depth + 1 > 5) {
		error(_("%s: ignoring alternate object stores, nesting too deep"),
		      path);
	} else {
		for (size_t i = 0; i < alternates.nr; i++)
			odb_add_alternate_recursively(files, alternates.v[i], depth + 1);
	}

 out:
	strvec_clear(&alternates);
}

static void odb_prepare_alternates(struct odb_source_files *files,
				   const char *alternate_db)
{
	struct strvec alternates = STRVEC_INIT;

	parse_alternates(alternate_db, PATH_SEP, NULL, &alternates);
	read_alternates(files->dirs->abspath, &alternates);

	for (size_t i = 0; i < alternates.nr; i++)
		odb_add_alternate_recursively(files, alternates.v[i], 0);

	strvec_clear(&alternates);
}

static void odb_source_files_prepare(struct odb_source *source,
				     enum odb_prepare_flags flags)
{
	struct odb_source_files *files = odb_source_files_downcast(source);

	/*
	 * Reprepare alternates, in case the alternates file was modified
	 * during the course of this process. This only _adds_ directories to
	 * the linked list, so existing directories will continue to exist
	 * for the lifetime of the process.
	 */
	if (flags & ODB_PREPARE_FLUSH_CACHES)
		odb_prepare_alternates(files, NULL);

	for (struct odb_files_dir *dir = files->dirs; dir; dir = dir->next) {
		odb_source_prepare(&dir->loose->base, flags);
		odb_source_prepare(&dir->packed->base, flags);
	}
}

static enum odb_read_status odb_source_files_read_object_info(struct odb_source *source,
							      const struct object_id *oid,
							      struct object_info *oi,
							      enum object_info_flags flags,
							      struct strbuf *errmsg)
{
	struct odb_source_files *files = odb_source_files_downcast(source);
	enum odb_read_status status = ODB_READ_NOT_FOUND;

	/*
	 * Reading an object may fail even though the object exists, for
	 * example because it is corrupt. Report this failure to the caller in
	 * case none of the directories was able to read the object, and
	 * prefer the first such error in case multiple reads have failed.
	 */
	for (struct odb_files_dir *dir = files->dirs; dir; dir = dir->next) {
		enum odb_read_status ret;

		ret = odb_source_read_object_info(&dir->packed->base, oid, oi, flags,
						  status == ODB_READ_NOT_FOUND ? errmsg : NULL);
		if (!ret)
			return 0;
		if (ret != ODB_READ_NOT_FOUND && status == ODB_READ_NOT_FOUND)
			status = ret;

		ret = odb_source_read_object_info(&dir->loose->base, oid, oi, flags,
						  status == ODB_READ_NOT_FOUND ? errmsg : NULL);
		if (!ret)
			return 0;
		if (ret != ODB_READ_NOT_FOUND && status == ODB_READ_NOT_FOUND)
			status = ret;
	}

	return status;
}

static int odb_source_files_read_object_stream(struct odb_stream **out,
					       struct odb_source *source,
					       const struct object_id *oid)
{
	struct odb_source_files *files = odb_source_files_downcast(source);

	for (struct odb_files_dir *dir = files->dirs; dir; dir = dir->next)
		if (!odb_source_read_object_stream(out, &dir->packed->base, oid) ||
		    !odb_source_read_object_stream(out, &dir->loose->base, oid))
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

	for (struct odb_files_dir *dir = files->dirs; dir; dir = dir->next) {
		if (opts->flags & ODB_FOR_EACH_OBJECT_LOCAL_ONLY && !dir->local)
			continue;

		if (!(opts->flags & ODB_FOR_EACH_OBJECT_PROMISOR_ONLY)) {
			ret = odb_source_for_each_object(&dir->loose->base, request, cb, cb_data, opts);
			if (ret)
				return ret;
		}

		ret = odb_source_for_each_object(&dir->packed->base, request, cb, cb_data, opts);
		if (ret)
			return ret;
	}

	return 0;
}

static int odb_source_files_count_objects(struct odb_source *source,
					  enum odb_count_objects_flags flags,
					  unsigned long *out)
{
	struct odb_source_files *files = odb_source_files_downcast(source);
	unsigned long count = 0;
	int ret;

	for (struct odb_files_dir *dir = files->dirs; dir; dir = dir->next) {
		unsigned long dir_count;

		ret = odb_source_count_objects(&dir->packed->base, flags, &dir_count);
		if (ret < 0)
			goto out;
		count += dir_count;

		if (!(flags & ODB_COUNT_OBJECTS_APPROXIMATE)) {
			ret = odb_source_count_objects(&dir->loose->base, flags, &dir_count);
			if (ret < 0)
				goto out;
			count += dir_count;
		}
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
	int ret = 0;

	for (struct odb_files_dir *dir = files->dirs; dir; dir = dir->next) {
		ret = odb_source_find_abbrev_len(&dir->packed->base, oid, len, &len);
		if (ret < 0)
			goto out;

		ret = odb_source_find_abbrev_len(&dir->loose->base, oid, len, &len);
		if (ret < 0)
			goto out;
	}

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

	for (struct odb_files_dir *dir = files->dirs; dir; dir = dir->next)
		if (odb_source_freshen_object(&dir->packed->base, oid, mtime) ||
		    odb_source_freshen_object(&dir->loose->base, oid, mtime))
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
	return odb_source_write_object(&files->dirs->loose->base, buf, len, type,
				       oid, compat_oid, mtime, flags);
}

static int odb_source_files_write_object_stream(struct odb_source *source,
						struct odb_stream *stream,
						struct object_id *oid)
{
	struct odb_source_files *files = odb_source_files_downcast(source);
	return odb_source_write_object_stream(&files->dirs->loose->base, stream, oid);
}

static int odb_source_files_begin_transaction(struct odb_source *source,
					      struct odb_transaction **out,
					      enum odb_transaction_flags flags)
{
	return odb_transaction_files_begin(source, out, flags);
}

static int too_many_loose_objects(struct odb_source_files *files, int limit)
{
	unsigned long loose_count;

	if (limit <= 0)
		return 0;

	if (odb_source_count_objects(&files->dirs->loose->base, ODB_COUNT_OBJECTS_APPROXIMATE,
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

	for (e = packfile_store_get_packs(files->dirs->packed); e; e = e->next) {
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

	for (e = packfile_store_get_packs(files->dirs->packed); e; e = e->next) {
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
		int auto_value = 6700;
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

struct odb_pack_generator_files {
	struct odb_pack_generator base;
	struct child_process cp;
};

static int odb_pack_generator_files_finish(struct odb_pack_generator *_generator)
{
	struct odb_pack_generator_files *generator =
		(struct odb_pack_generator_files *)_generator;
	int ret;

	ret = finish_command(&generator->cp);
	free(generator);

	if (ret) {
		/*
		 * On failure, pack-objects is expected to have written a
		 * useful error message to its standard error stream already.
		 * Death by signal is worth mentioning, though, with the
		 * exception of SIGPIPE: that is a normal occurrence when the
		 * consumer of the pack hangs up.
		 */
		if (ret > 128 && ret - 128 == SIGPIPE)
			return -1;
		if (ret > 128)
			error(_("pack-objects died of signal %d"), ret - 128);
		return -1;
	}

	return 0;
}

static int odb_source_files_generate_pack(struct odb_source *source UNUSED,
					  struct odb_pack_generator **out,
					  const struct odb_generate_pack_options *opts)
{
	struct odb_pack_generator_files *generator;
	struct child_process *cp;
	FILE *in;

	CALLOC_ARRAY(generator, 1);
	child_process_init(&generator->cp);
	cp = &generator->cp;

	/*
	 * The hook is expected to spawn "$hook git pack-objects <args...>"
	 * and to behave like git-pack-objects(1) would have. This can for
	 * example be used to serve precomputed packfiles.
	 */
	if (opts->pack_objects_hook) {
		strvec_push(&cp->args, opts->pack_objects_hook);
		strvec_push(&cp->args, "git");
		cp->use_shell = 1;
	} else {
		cp->git_cmd = 1;
	}

	/*
	 * The caller-provided shallow boundary overrides any shallow state
	 * that the repository itself may have, so the shallow file needs to
	 * be neutralized.
	 */
	if (opts->shallows.nr) {
		strvec_push(&cp->args, "--shallow-file");
		strvec_push(&cp->args, "");
	}
	strvec_push(&cp->args, "pack-objects");
	strvec_push(&cp->args, "--revs");
	strvec_push(&cp->args, "--stdout");
	if (opts->thin)
		strvec_push(&cp->args, "--thin");
	if (opts->shallow)
		strvec_push(&cp->args, "--shallow");
	if (opts->ofs_delta)
		strvec_push(&cp->args, "--delta-base-offset");
	if (opts->include_tag)
		strvec_push(&cp->args, "--include-tag");
	if (opts->missing_allow_promisor)
		strvec_push(&cp->args, "--missing=allow-promisor");
	if (opts->disable_bitmaps)
		strvec_push(&cp->args, "--no-use-bitmap-index");
	switch (opts->progress) {
	case ODB_GENERATE_PACK_PROGRESS_NONE:
		strvec_push(&cp->args, "--quiet");
		break;
	case ODB_GENERATE_PACK_PROGRESS_STANDARD:
		strvec_push(&cp->args, "--progress");
		break;
	case ODB_GENERATE_PACK_PROGRESS_VERBOSE:
		strvec_push(&cp->args, "--all-progress");
		break;
	default:
		BUG("unknown progress option %d", opts->progress);
	}
	if (opts->filter_spec)
		strvec_pushf(&cp->args, "--filter=%s", opts->filter_spec);
	if (opts->uri_protocols)
		for (size_t i = 0; i < opts->uri_protocols->nr; i++)
			strvec_pushf(&cp->args, "--uri-protocol=%s",
				     opts->uri_protocols->items[i].string);

	cp->in = -1;
	cp->out = opts->pack_fd;
	cp->err = opts->progress_fd;
	cp->clean_on_exit = 1;

	if (start_command(cp)) {
		free(generator);
		return error(_("could not spawn pack-objects"));
	}

	/*
	 * Feed the objects to pack-objects. This is safe to do synchronously
	 * because pack-objects consumes all of its standard input before it
	 * starts to generate the pack.
	 */
	in = xfdopen(cp->in, "w");
	for (size_t i = 0; i < opts->shallows.nr; i++)
		fprintf(in, "--shallow %s\n", oid_to_hex(&opts->shallows.oid[i]));
	for (size_t i = 0; i < opts->wants.nr; i++)
		fprintf(in, "%s\n", oid_to_hex(&opts->wants.oid[i]));
	fprintf(in, "--not\n");
	for (size_t i = 0; i < opts->haves.nr; i++)
		fprintf(in, "%s\n", oid_to_hex(&opts->haves.oid[i]));
	fprintf(in, "\n");
	fflush(in);
	if (ferror(in)) {
		error(_("error writing to pack-objects"));
		fclose(in);
		if (opts->pack_fd < 0)
			close(cp->out);
		if (opts->progress_fd < 0)
			close(cp->err);
		finish_command(cp);
		free(generator);
		return -1;
	}
	fclose(in);

	generator->base.out = opts->pack_fd < 0 ? cp->out : -1;
	generator->base.err = opts->progress_fd < 0 ? cp->err : -1;
	generator->base.finish = odb_pack_generator_files_finish;

	*out = &generator->base;
	return 0;
}

static int odb_source_files_fsck(struct odb_source *source,
				 struct odb_fsck_options *opts)
{
	struct odb_source_files *files = odb_source_files_downcast(source);
	int ret = 0;

	for (struct odb_files_dir *dir = files->dirs; dir; dir = dir->next) {
		if (!(opts->flags & ODB_FSCK_FULL) && !dir->local)
			continue;

		ret |= odb_source_fsck(&dir->loose->base, opts);
		ret |= odb_source_fsck(&dir->packed->base, opts);
	}

	return ret;
}

struct odb_files_dir *odb_source_files_find_dir(struct object_database *odb, const char *obj_dir)
{
	struct odb_source_files *files = odb_source_files_downcast(odb->source);
	char *obj_dir_real = real_pathdup(obj_dir, 1);
	struct strbuf odb_path_real = STRBUF_INIT;
	struct odb_files_dir *dir;

	for (dir = files->dirs; dir; dir = dir->next) {
		strbuf_realpath(&odb_path_real, dir->abspath, 1);
		if (!strcmp(obj_dir_real, odb_path_real.buf))
			break;
	}

	free(obj_dir_real);
	strbuf_release(&odb_path_real);
	return dir;
}

struct odb_source_files *odb_source_files_new(struct object_database *odb,
					      enum odb_new_flags flags)
{
	struct odb_source_files *files;
	char *object_dir = NULL;
	char *alternates = NULL;

	if (flags & ODB_NEW_HONOR_ENV) {
		object_dir = xstrdup_or_null(getenv(DB_ENVIRONMENT));
		alternates = xstrdup_or_null(getenv(ALTERNATE_DB_ENVIRONMENT));
	}
	if (!object_dir)
		object_dir = xstrfmt("%s/objects", odb->repo->commondir);

	CALLOC_ARRAY(files, 1);
	odb_source_init(&files->base, odb, ODB_SOURCE_FILES, object_dir, true);

	hashmap_init(&files->dirs_by_path, odb_files_dir_by_path_cmp, files, 0);
	files->dirs_paths_icase = -1;

	files->dirs = odb_files_dir_new(odb, object_dir, true);
	files->dirs_tail = &files->dirs->next;

	files->base.free = odb_source_files_free;
	files->base.close = odb_source_files_close;
	files->base.create_on_disk = odb_source_files_create_on_disk;
	files->base.prepare = odb_source_files_prepare;
	files->base.fsck = odb_source_files_fsck;
	files->base.read_object_info = odb_source_files_read_object_info;
	files->base.read_object_stream = odb_source_files_read_object_stream;
	files->base.for_each_object = odb_source_files_for_each_object;
	files->base.count_objects = odb_source_files_count_objects;
	files->base.find_abbrev_len = odb_source_files_find_abbrev_len;
	files->base.freshen_object = odb_source_files_freshen_object;
	files->base.write_object = odb_source_files_write_object;
	files->base.write_object_stream = odb_source_files_write_object_stream;
	files->base.begin_transaction = odb_source_files_begin_transaction;
	files->base.optimize = odb_source_files_optimize;
	files->base.optimize_required = odb_source_files_optimize_required;
	files->base.generate_pack = odb_source_files_generate_pack;

	/*
	 * Ideally, we would only ever store absolute paths in the source. This
	 * is not (yet) possible though because we access and assume relative
	 * paths in the primary ODB source in some user-facing functionality.
	 */
	if (!is_absolute_path(object_dir))
		chdir_notify_register(odb_source_files_reparent, files);

	odb_prepare_alternates(files, alternates);

	free(object_dir);
	free(alternates);
	return files;
}
