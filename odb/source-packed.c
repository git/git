#include "git-compat-util.h"
#include "abspath.h"
#include "chdir-notify.h"
#include "dir.h"
#include "git-zlib.h"
#include "list-objects-filter-options.h"
#include "mergesort.h"
#include "midx.h"
#include "odb/source-packed.h"
#include "odb/streaming.h"
#include "packfile.h"
#include "pack-bitmap.h"

static int find_pack_entry(struct odb_source_packed *store,
			   const struct object_id *oid,
			   struct pack_entry *e)
{
	struct packfile_list_entry *l;

	odb_source_prepare(&store->base, 0);
	if (store->midx && fill_midx_entry(store->midx, oid, e))
		return 1;

	for (l = store->packs.head; l; l = l->next) {
		struct packed_git *p = l->pack;

		if (!p->multi_pack_index && packfile_fill_entry(p, oid, e)) {
			if (!store->skip_mru_updates)
				packfile_list_prepend(&store->packs, p);
			return 1;
		}
	}

	return 0;
}

static int odb_source_packed_read_object_info(struct odb_source *source,
					      const struct object_id *oid,
					      struct object_info *oi,
					      enum object_info_flags flags)
{
	struct odb_source_packed *packed = odb_source_packed_downcast(source);
	struct pack_entry e;
	int ret;

	/*
	 * In case the first read didn't surface the object, we have to reload
	 * packfiles. This may cause us to discover new packfiles that have
	 * been added since the last time we have prepared the packfile store.
	 */
	if (flags & OBJECT_INFO_SECOND_READ)
		odb_source_prepare(source, ODB_PREPARE_FLUSH_CACHES);

	if (!find_pack_entry(packed, oid, &e))
		return 1;

	/*
	 * We know that the caller doesn't actually need the
	 * information below, so return early.
	 */
	if (!oi)
		return 0;

	ret = packed_object_info(packed, e.p, e.offset, oi);
	if (ret < 0) {
		mark_bad_packed_object(e.p, oid);
		return -1;
	}

	return 0;
}

static int odb_source_packed_read_object_stream(struct odb_read_stream **out,
						struct odb_source *source,
						const struct object_id *oid)
{
	struct odb_source_packed *packed = odb_source_packed_downcast(source);
	struct pack_entry e;

	if (!find_pack_entry(packed, oid, &e))
		return -1;

	return packfile_read_object_stream(out, oid, e.p, e.offset);
}

struct odb_source_packed_for_each_object_wrapper_data {
	struct odb_source_packed *store;
	const struct object_info *request;
	odb_for_each_object_cb cb;
	void *cb_data;
};

static int odb_source_packed_for_each_object_wrapper(const struct object_id *oid,
						     struct packed_git *pack,
						     uint32_t index_pos,
						     void *cb_data)
{
	struct odb_source_packed_for_each_object_wrapper_data *data = cb_data;

	if (data->request) {
		off_t offset = nth_packed_object_offset(pack, index_pos);
		struct object_info oi = *data->request;

		if (packed_object_info_with_index_pos(data->store, pack, offset,
						      &index_pos, &oi) < 0) {
			mark_bad_packed_object(pack, oid);
			return -1;
		}

		return data->cb(oid, &oi, data->cb_data);
	} else {
		return data->cb(oid, NULL, data->cb_data);
	}
}

static int match_hash(unsigned len, const unsigned char *a, const unsigned char *b)
{
	do {
		if (*a != *b)
			return 0;
		a++;
		b++;
		len -= 2;
	} while (len > 1);
	if (len)
		if ((*a ^ *b) & 0xf0)
			return 0;
	return 1;
}

static bool should_exclude_pack(struct packed_git *p, enum odb_for_each_object_flags flags)
{
	if ((flags & ODB_FOR_EACH_OBJECT_LOCAL_ONLY) && !p->pack_local)
		return true;
	if ((flags & ODB_FOR_EACH_OBJECT_PROMISOR_ONLY) &&
	    !p->pack_promisor)
		return true;
	if ((flags & ODB_FOR_EACH_OBJECT_SKIP_IN_CORE_KEPT_PACKS) &&
	    p->pack_keep_in_core)
		return true;
	if ((flags & ODB_FOR_EACH_OBJECT_SKIP_ON_DISK_KEPT_PACKS) &&
	    p->pack_keep)
		return true;
	return false;
}

static int for_each_prefixed_object_in_midx(
	struct odb_source_packed *source,
	struct multi_pack_index *m,
	const struct odb_for_each_object_options *opts,
	struct odb_source_packed_for_each_object_wrapper_data *data)
{
	bool pack_errors = false;
	int ret;

	for (; m; m = m->base_midx) {
		uint32_t num, i, first = 0;
		int len = opts->prefix_hex_len > m->source->base.odb->repo->hash_algo->hexsz ?
			m->source->base.odb->repo->hash_algo->hexsz : opts->prefix_hex_len;

		if (!m->num_objects)
			continue;

		num = m->num_objects + m->num_objects_in_base;

		bsearch_one_midx(opts->prefix, m, &first);

		/*
		 * At this point, "first" is the location of the lowest
		 * object with an object name that could match "opts->prefix".
		 * See if we have 0, 1 or more objects that actually match(es).
		 */
		for (i = first; i < num; i++) {
			const struct object_id *current = NULL;
			struct packed_git *pack;
			struct object_id oid;

			current = nth_midxed_object_oid(&oid, m, i);

			if (!match_hash(len, opts->prefix->hash, current->hash))
				break;

			if (opts->flags || data->request) {
				uint32_t pack_id = nth_midxed_pack_int_id(m, i);

				if (prepare_midx_pack(m, pack_id)) {
					pack_errors = true;
					continue;
				}

				pack = nth_midxed_pack(m, pack_id);
				if (should_exclude_pack(pack, opts->flags))
					continue;
			}

			if (data->request) {
				struct object_info oi = *data->request;
				off_t offset = nth_midxed_offset(m, i);

				ret = packed_object_info(source, pack, offset, &oi);
				if (ret)
					goto out;

				ret = data->cb(&oid, &oi, data->cb_data);
				if (ret)
					goto out;
			} else {
				ret = data->cb(&oid, NULL, data->cb_data);
				if (ret)
					goto out;
			}
		}
	}

	ret = 0;

out:
	if (!ret && pack_errors)
		ret = -1;
	return ret;
}

static int for_each_prefixed_object_in_pack(
	struct odb_source_packed *source,
	struct packed_git *p,
	const struct odb_for_each_object_options *opts,
	struct odb_source_packed_for_each_object_wrapper_data *data)
{
	uint32_t num, i, first = 0;
	int len = opts->prefix_hex_len > p->repo->hash_algo->hexsz ?
		p->repo->hash_algo->hexsz : opts->prefix_hex_len;
	int ret;

	num = p->num_objects;
	bsearch_pack(opts->prefix, p, &first);

	/*
	 * At this point, "first" is the location of the lowest object
	 * with an object name that could match "bin_pfx".  See if we have
	 * 0, 1 or more objects that actually match(es).
	 */
	for (i = first; i < num; i++) {
		struct object_id oid;

		nth_packed_object_id(&oid, p, i);
		if (!match_hash(len, opts->prefix->hash, oid.hash))
			break;

		if (data->request) {
			struct object_info oi = *data->request;
			off_t offset = nth_packed_object_offset(p, i);

			ret = packed_object_info(source, p, offset, &oi);
			if (ret)
				goto out;

			ret = data->cb(&oid, &oi, data->cb_data);
			if (ret)
				goto out;
		} else {
			ret = data->cb(&oid, NULL, data->cb_data);
			if (ret)
				goto out;
		}
	}

	ret = 0;

out:
	return ret;
}

static int odb_source_packed_for_each_prefixed_object(
	struct odb_source_packed *store,
	const struct odb_for_each_object_options *opts,
	struct odb_source_packed_for_each_object_wrapper_data *data)
{
	struct packfile_list_entry *e;
	struct multi_pack_index *m;
	bool pack_errors = false;
	int ret;

	store->skip_mru_updates = true;

	m = get_multi_pack_index(store);
	if (m) {
		ret = for_each_prefixed_object_in_midx(store, m, opts, data);
		if (ret)
			goto out;
	}

	for (e = packfile_store_get_packs(store); e; e = e->next) {
		if (e->pack->multi_pack_index)
			continue;
		if (should_exclude_pack(e->pack, opts->flags))
			continue;

		if (open_pack_index(e->pack)) {
			pack_errors = true;
			continue;
		}

		if (!e->pack->num_objects)
			continue;

		ret = for_each_prefixed_object_in_pack(store, e->pack, opts, data);
		if (ret)
			goto out;
	}

	ret = 0;

out:
	store->skip_mru_updates = false;
	if (!ret && pack_errors)
		ret = -1;
	return ret;
}

struct bitmapped_for_each_object_data {
	struct odb_source_packed *packed;
	const struct object_info *request;
	const struct odb_for_each_object_options *opts;
	odb_for_each_object_cb cb;
	void *cb_data;
};

static int bitmapped_for_each_object(const struct object_id *oid,
				     enum object_type type UNUSED,
				     int flags UNUSED,
				     uint32_t hash UNUSED,
				     struct packed_git *pack,
				     off_t offset,
				     void *cb_data)
{
	struct bitmapped_for_each_object_data *data = cb_data;

	if (should_exclude_pack(pack, data->opts->flags))
		return 0;

	if (data->request) {
		struct object_info oi = *data->request;
		if (packed_object_info(data->packed, pack, offset, &oi) < 0)
			return -1;
		return data->cb(oid, &oi, data->cb_data);
	}

	return data->cb(oid, NULL, data->cb_data);
}

static int odb_source_packed_for_each_object(struct odb_source *source,
					     const struct object_info *request,
					     odb_for_each_object_cb cb,
					     void *cb_data,
					     const struct odb_for_each_object_options *opts)
{
	struct odb_source_packed *packed = odb_source_packed_downcast(source);
	struct odb_source_packed_for_each_object_wrapper_data data = {
		.store = packed,
		.request = request,
		.cb = cb,
		.cb_data = cb_data,
	};
	struct bitmap_index *bitmap = NULL;
	struct packfile_list_entry *e;
	int pack_errors = 0, ret;

	if (opts->prefix)
		return odb_source_packed_for_each_prefixed_object(packed, opts, &data);

	if (opts->filter &&
	    opts->filter->choice != LOFC_DISABLED &&
	    can_filter_bitmap(opts->filter))
		bitmap = prepare_bitmap_git_for_source(packed);
	if (bitmap) {
		struct bitmapped_for_each_object_data bitmap_data = {
			.packed = packed,
			.request = request,
			.opts = opts,
			.cb = cb,
			.cb_data = cb_data,
		};

		ret = for_each_bitmapped_object(bitmap, opts->filter,
						bitmapped_for_each_object,
						&bitmap_data);
		if (ret)
			goto out;
	}

	packed->skip_mru_updates = true;

	for (e = packfile_store_get_packs(packed); e; e = e->next) {
		struct packed_git *p = e->pack;

		if (should_exclude_pack(p, opts->flags))
			continue;

		/*
		 * Objects covered by the bitmap have already been yielded
		 * above; skip them here to avoid duplicates.
		 */
		if (bitmap && bitmap_index_contains_pack(bitmap, p))
			continue;

		if (open_pack_index(p)) {
			pack_errors = 1;
			continue;
		}

		ret = for_each_object_in_pack(p, odb_source_packed_for_each_object_wrapper,
					      &data, opts->flags);
		if (ret)
			goto out;
	}

	ret = 0;

out:
	packed->skip_mru_updates = false;
	free_bitmap_index(bitmap);

	if (!ret && pack_errors)
		ret = -1;
	return ret;
}

static int odb_source_packed_count_objects(struct odb_source *source,
					   enum odb_count_objects_flags flags UNUSED,
					   unsigned long *out)
{
	struct odb_source_packed *packed = odb_source_packed_downcast(source);
	struct packfile_list_entry *e;
	struct multi_pack_index *m;
	unsigned long count = 0;
	int ret;

	m = get_multi_pack_index(packed);
	if (m)
		count += m->num_objects + m->num_objects_in_base;

	for (e = packfile_store_get_packs(packed); e; e = e->next) {
		if (e->pack->multi_pack_index)
			continue;
		if (open_pack_index(e->pack)) {
			ret = -1;
			goto out;
		}

		count += e->pack->num_objects;
	}

	*out = count;
	ret = 0;

out:
	return ret;
}

static int extend_abbrev_len(const struct object_id *a,
			     const struct object_id *b,
			     unsigned *out)
{
	unsigned len = oid_common_prefix_hexlen(a, b);
	if (len != hash_algos[a->algo].hexsz && len >= *out)
		*out = len + 1;
	return 0;
}

static void find_abbrev_len_for_midx(struct multi_pack_index *m,
				     const struct object_id *oid,
				     unsigned min_len,
				     unsigned *out)
{
	unsigned len = min_len;

	for (; m; m = m->base_midx) {
		int match = 0;
		uint32_t num, first = 0;
		struct object_id found_oid;

		if (!m->num_objects)
			continue;

		num = m->num_objects + m->num_objects_in_base;
		match = bsearch_one_midx(oid, m, &first);

		/*
		 * first is now the position in the packfile where we
		 * would insert the object ID if it does not exist (or the
		 * position of the object ID if it does exist). Hence, we
		 * consider a maximum of two objects nearby for the
		 * abbreviation length.
		 */

		if (!match) {
			if (nth_midxed_object_oid(&found_oid, m, first))
				extend_abbrev_len(&found_oid, oid, &len);
		} else if (first < num - 1) {
			if (nth_midxed_object_oid(&found_oid, m, first + 1))
				extend_abbrev_len(&found_oid, oid, &len);
		}
		if (first > 0) {
			if (nth_midxed_object_oid(&found_oid, m, first - 1))
				extend_abbrev_len(&found_oid, oid, &len);
		}
	}

	*out = len;
}

static void find_abbrev_len_for_pack(struct packed_git *p,
				     const struct object_id *oid,
				     unsigned min_len,
				     unsigned *out)
{
	int match;
	uint32_t num, first = 0;
	struct object_id found_oid;
	unsigned len = min_len;

	num = p->num_objects;
	match = bsearch_pack(oid, p, &first);

	/*
	 * first is now the position in the packfile where we would insert
	 * the object ID if it does not exist (or the position of mad->hash if
	 * it does exist). Hence, we consider a maximum of two objects
	 * nearby for the abbreviation length.
	 */
	if (!match) {
		if (!nth_packed_object_id(&found_oid, p, first))
			extend_abbrev_len(&found_oid, oid, &len);
	} else if (first < num - 1) {
		if (!nth_packed_object_id(&found_oid, p, first + 1))
			extend_abbrev_len(&found_oid, oid, &len);
	}
	if (first > 0) {
		if (!nth_packed_object_id(&found_oid, p, first - 1))
			extend_abbrev_len(&found_oid, oid, &len);
	}

	*out = len;
}

static int odb_source_packed_find_abbrev_len(struct odb_source *source,
					     const struct object_id *oid,
					     unsigned min_len,
					     unsigned *out)
{
	struct odb_source_packed *packed = odb_source_packed_downcast(source);
	struct packfile_list_entry *e;
	struct multi_pack_index *m;

	m = get_multi_pack_index(packed);
	if (m)
		find_abbrev_len_for_midx(m, oid, min_len, &min_len);

	for (e = packfile_store_get_packs(packed); e; e = e->next) {
		if (e->pack->multi_pack_index)
			continue;
		if (open_pack_index(e->pack) || !e->pack->num_objects)
			continue;

		find_abbrev_len_for_pack(e->pack, oid, min_len, &min_len);
	}

	*out = min_len;
	return 0;
}

static int odb_source_packed_freshen_object(struct odb_source *source,
					    const struct object_id *oid)
{
	struct odb_source_packed *packed = odb_source_packed_downcast(source);
	struct pack_entry e;

	if (!find_pack_entry(packed, oid, &e))
		return 0;
	if (e.p->is_cruft)
		return 0;
	if (e.p->freshened)
		return 1;
	if (utime(e.p->pack_name, NULL))
		return 0;
	e.p->freshened = 1;

	return 1;
}

static int odb_source_packed_write_object(struct odb_source *source UNUSED,
					  const void *buf UNUSED,
					  size_t len UNUSED,
					  enum object_type type UNUSED,
					  struct object_id *oid UNUSED,
					  struct object_id *compat_oid UNUSED,
					  unsigned flags UNUSED)
{
	return error("packed backend cannot write objects");
}

static int odb_source_packed_write_object_stream(struct odb_source *source UNUSED,
						 struct odb_write_stream *stream UNUSED,
						 size_t len UNUSED,
						 struct object_id *oid UNUSED)
{
	return error("packed backend cannot write object streams");
}

static int odb_source_packed_begin_transaction(struct odb_source *source UNUSED,
					       struct odb_transaction **out UNUSED,
					       enum odb_transaction_flags flags UNUSED)
{
	return error("packed backend cannot begin transactions");
}

static int odb_source_packed_read_alternates(struct odb_source *source UNUSED,
					     struct strvec *out UNUSED)
{
	return 0;
}

static int odb_source_packed_write_alternate(struct odb_source *source UNUSED,
					     const char *alternate UNUSED)
{
	return error("packed backend cannot write alternates");
}

void (*report_garbage)(unsigned seen_bits, const char *path);

static void report_helper(const struct string_list *list,
			  int seen_bits, int first, int last)
{
	if (seen_bits == (PACKDIR_FILE_PACK|PACKDIR_FILE_IDX))
		return;

	for (; first < last; first++)
		report_garbage(seen_bits, list->items[first].string);
}

static void report_pack_garbage(struct string_list *list)
{
	int baselen = -1, first = 0, seen_bits = 0;

	if (!report_garbage)
		return;

	string_list_sort(list);

	for (size_t i = 0; i < list->nr; i++) {
		const char *path = list->items[i].string;
		if (baselen != -1 &&
		    strncmp(path, list->items[first].string, baselen)) {
			report_helper(list, seen_bits, first, i);
			baselen = -1;
			seen_bits = 0;
		}
		if (baselen == -1) {
			const char *dot = strrchr(path, '.');
			if (!dot) {
				report_garbage(PACKDIR_FILE_GARBAGE, path);
				continue;
			}
			baselen = dot - path + 1;
			first = i;
		}
		if (!strcmp(path + baselen, "pack"))
			seen_bits |= 1;
		else if (!strcmp(path + baselen, "idx"))
			seen_bits |= 2;
	}
	report_helper(list, seen_bits, first, list->nr);
}

struct prepare_pack_data {
	struct odb_source_packed *source;
	struct string_list *garbage;
};

static void prepare_pack(const char *full_name, size_t full_name_len,
			 const char *file_name, void *_data)
{
	struct prepare_pack_data *data = (struct prepare_pack_data *)_data;
	size_t base_len = full_name_len;

	if (strip_suffix_mem(full_name, &base_len, ".idx") &&
	    !(data->source->midx &&
	      midx_contains_pack(data->source->midx, file_name))) {
		char *trimmed_path = xstrndup(full_name, full_name_len);
		packfile_store_load_pack(data->source,
					 trimmed_path, data->source->base.local);
		free(trimmed_path);
	}

	if (!report_garbage)
		return;

	if (!strcmp(file_name, "multi-pack-index") ||
	    !strcmp(file_name, "multi-pack-index.d"))
		return;
	if (starts_with(file_name, "multi-pack-index") &&
	    (ends_with(file_name, ".bitmap") || ends_with(file_name, ".rev")))
		return;
	if (ends_with(file_name, ".idx") ||
	    ends_with(file_name, ".rev") ||
	    ends_with(file_name, ".pack") ||
	    ends_with(file_name, ".bitmap") ||
	    ends_with(file_name, ".keep") ||
	    ends_with(file_name, ".promisor") ||
	    ends_with(file_name, ".mtimes"))
		string_list_append(data->garbage, full_name);
	else
		report_garbage(PACKDIR_FILE_GARBAGE, full_name);
}

static void prepare_packed_git_one(struct odb_source_packed *source)
{
	struct string_list garbage = STRING_LIST_INIT_DUP;
	struct prepare_pack_data data = {
		.source = source,
		.garbage = &garbage,
	};

	for_each_file_in_pack_dir(source->base.path, prepare_pack, &data);

	report_pack_garbage(data.garbage);
	string_list_clear(data.garbage, 0);
}

DEFINE_LIST_SORT(static, sort_packs, struct packfile_list_entry, next);

static int sort_pack(const struct packfile_list_entry *a,
		     const struct packfile_list_entry *b)
{
	int st;

	/*
	 * Local packs tend to contain objects specific to our
	 * variant of the project than remote ones.  In addition,
	 * remote ones could be on a network mounted filesystem.
	 * Favor local ones for these reasons.
	 */
	st = a->pack->pack_local - b->pack->pack_local;
	if (st)
		return -st;

	/*
	 * Younger packs tend to contain more recent objects,
	 * and more recent objects tend to get accessed more
	 * often.
	 */
	if (a->pack->mtime < b->pack->mtime)
		return 1;
	else if (a->pack->mtime == b->pack->mtime)
		return 0;
	return -1;
}

static void odb_source_packed_prepare(struct odb_source *source,
				      enum odb_prepare_flags flags)
{
	struct odb_source_packed *packed = odb_source_packed_downcast(source);

	if (flags & ODB_PREPARE_FLUSH_CACHES)
		packed->initialized = false;
	if (packed->initialized)
		return;

	prepare_multi_pack_index_one(packed);
	prepare_packed_git_one(packed);

	sort_packs(&packed->packs.head, sort_pack);
	for (struct packfile_list_entry *e = packed->packs.head; e; e = e->next)
		if (!e->next)
			packed->packs.tail = e;

	packed->initialized = true;
}

static void odb_source_packed_reparent(const char *name UNUSED,
				       const char *old_cwd,
				       const char *new_cwd,
				       void *cb_data)
{
	struct odb_source_packed *packed = cb_data;
	char *path = reparent_relative_path(old_cwd, new_cwd,
					    packed->base.path);
	free(packed->base.path);
	packed->base.path = path;
}

static void odb_source_packed_close(struct odb_source *source)
{
	struct odb_source_packed *packed = odb_source_packed_downcast(source);

	for (struct packfile_list_entry *e = packed->packs.head; e; e = e->next) {
		if (e->pack->do_not_close)
			BUG("want to close pack marked 'do-not-close'");
		close_pack(e->pack);
	}
	if (packed->midx)
		close_midx(packed->midx);
	packed->midx = NULL;
}

static void odb_source_packed_free(struct odb_source *source)
{
	struct odb_source_packed *packed = odb_source_packed_downcast(source);

	chdir_notify_unregister(NULL, odb_source_packed_reparent, packed);

	for (struct packfile_list_entry *e = packed->packs.head; e; e = e->next)
		free(e->pack);
	packfile_list_clear(&packed->packs);

	strmap_clear(&packed->packs_by_path, 0);
	odb_source_release(&packed->base);
	free(packed);
}

struct odb_source_packed *odb_source_packed_new(struct object_database *odb,
						const char *path,
						bool local)
{
	struct odb_source_packed *packed;

	CALLOC_ARRAY(packed, 1);
	odb_source_init(&packed->base, odb, ODB_SOURCE_PACKED, path, local);
	strmap_init(&packed->packs_by_path);

	packed->base.free = odb_source_packed_free;
	packed->base.close = odb_source_packed_close;
	packed->base.prepare = odb_source_packed_prepare;
	packed->base.read_object_info = odb_source_packed_read_object_info;
	packed->base.read_object_stream = odb_source_packed_read_object_stream;
	packed->base.for_each_object = odb_source_packed_for_each_object;
	packed->base.count_objects = odb_source_packed_count_objects;
	packed->base.find_abbrev_len = odb_source_packed_find_abbrev_len;
	packed->base.freshen_object = odb_source_packed_freshen_object;
	packed->base.write_object = odb_source_packed_write_object;
	packed->base.write_object_stream = odb_source_packed_write_object_stream;
	packed->base.begin_transaction = odb_source_packed_begin_transaction;
	packed->base.read_alternates = odb_source_packed_read_alternates;
	packed->base.write_alternate = odb_source_packed_write_alternate;

	if (!is_absolute_path(path))
		chdir_notify_register(NULL, odb_source_packed_reparent, packed);

	return packed;
}
