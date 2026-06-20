#include "git-compat-util.h"
#include "abspath.h"
#include "chdir-notify.h"
#include "gettext.h"
#include "hex.h"
#include "loose.h"
#include "object-file.h"
#include "object-file-convert.h"
#include "odb.h"
#include "odb/source-files.h"
#include "odb/source-loose.h"
#include "odb/streaming.h"
#include "oidtree.h"
#include "repository.h"
#include "strbuf.h"

static int append_loose_object(const struct object_id *oid,
			       const char *path UNUSED,
			       void *data)
{
	oidtree_insert(data, oid, NULL);
	return 0;
}

static struct oidtree *odb_source_loose_cache(struct odb_source_loose *loose,
					      const struct object_id *oid)
{
	int subdir_nr = oid->hash[0];
	struct strbuf buf = STRBUF_INIT;
	size_t word_bits = bitsizeof(loose->subdir_seen[0]);
	size_t word_index = subdir_nr / word_bits;
	size_t mask = (size_t)1u << (subdir_nr % word_bits);
	uint32_t *bitmap;

	if (subdir_nr < 0 ||
	    (size_t) subdir_nr >= bitsizeof(loose->subdir_seen))
		BUG("subdir_nr out of range");

	bitmap = &loose->subdir_seen[word_index];
	if (*bitmap & mask)
		return loose->cache;
	if (!loose->cache) {
		ALLOC_ARRAY(loose->cache, 1);
		oidtree_init(loose->cache);
	}
	strbuf_addstr(&buf, loose->base.path);
	for_each_file_in_obj_subdir(subdir_nr, &buf,
				    loose->base.odb->repo->hash_algo,
				    append_loose_object,
				    NULL, NULL,
				    loose->cache);
	*bitmap |= mask;
	strbuf_release(&buf);
	return loose->cache;
}

static int quick_has_loose(struct odb_source_loose *loose,
			   const struct object_id *oid)
{
	return !!oidtree_contains(odb_source_loose_cache(loose, oid), oid);
}

static int read_object_info_from_path(struct odb_source_loose *loose,
				      const char *path,
				      const struct object_id *oid,
				      struct object_info *oi,
				      enum object_info_flags flags)
{
	int ret;
	int fd;
	unsigned long mapsize;
	void *map = NULL;
	git_zstream stream, *stream_to_end = NULL;
	char hdr[MAX_HEADER_LEN];
	size_t size_scratch;
	enum object_type type_scratch;
	struct stat st;

	/*
	 * If we don't care about type or size, then we don't
	 * need to look inside the object at all. Note that we
	 * do not optimize out the stat call, even if the
	 * caller doesn't care about the disk-size, since our
	 * return value implicitly indicates whether the
	 * object even exists.
	 */
	if (!oi || (!oi->typep && !oi->sizep && !oi->contentp)) {
		struct stat st;

		if ((!oi || (!oi->disk_sizep && !oi->mtimep)) && (flags & OBJECT_INFO_QUICK)) {
			ret = quick_has_loose(loose, oid) ? 0 : -1;
			goto out;
		}

		if (lstat(path, &st) < 0) {
			ret = -1;
			goto out;
		}

		if (oi) {
			if (oi->disk_sizep)
				*oi->disk_sizep = st.st_size;
			if (oi->mtimep)
				*oi->mtimep = st.st_mtime;
		}

		ret = 0;
		goto out;
	}

	fd = git_open(path);
	if (fd < 0) {
		if (errno != ENOENT)
			error_errno(_("unable to open loose object %s"), oid_to_hex(oid));
		ret = -1;
		goto out;
	}

	if (fstat(fd, &st)) {
		close(fd);
		ret = -1;
		goto out;
	}

	mapsize = xsize_t(st.st_size);
	if (!mapsize) {
		close(fd);
		ret = error(_("object file %s is empty"), path);
		goto out;
	}

	map = xmmap(NULL, mapsize, PROT_READ, MAP_PRIVATE, fd, 0);
	close(fd);
	if (!map) {
		ret = -1;
		goto out;
	}

	if (oi->disk_sizep)
		*oi->disk_sizep = mapsize;
	if (oi->mtimep)
		*oi->mtimep = st.st_mtime;

	stream_to_end = &stream;

	switch (unpack_loose_header(&stream, map, mapsize, hdr, sizeof(hdr))) {
	case ULHR_OK:
		if (!oi->sizep)
			oi->sizep = &size_scratch;
		if (!oi->typep)
			oi->typep = &type_scratch;

		if (parse_loose_header(hdr, oi) < 0) {
			ret = error(_("unable to parse %s header"), oid_to_hex(oid));
			goto corrupt;
		}

		if (*oi->typep < 0)
			die(_("invalid object type"));

		if (oi->contentp) {
			*oi->contentp = unpack_loose_rest(&stream, hdr, *oi->sizep, oid);
			if (!*oi->contentp) {
				ret = -1;
				goto corrupt;
			}
		}

		break;
	case ULHR_BAD:
		ret = error(_("unable to unpack %s header"),
			    oid_to_hex(oid));
		goto corrupt;
	case ULHR_TOO_LONG:
		ret = error(_("header for %s too long, exceeds %d bytes"),
			    oid_to_hex(oid), MAX_HEADER_LEN);
		goto corrupt;
	}

	ret = 0;

corrupt:
	if (ret && (flags & OBJECT_INFO_DIE_IF_CORRUPT))
		die(_("loose object %s (stored in %s) is corrupt"),
		    oid_to_hex(oid), path);

out:
	if (stream_to_end)
		git_inflate_end(stream_to_end);
	if (map)
		munmap(map, mapsize);
	if (oi) {
		if (oi->sizep == &size_scratch)
			oi->sizep = NULL;
		if (oi->typep == &type_scratch)
			oi->typep = NULL;
		if (oi->delta_base_oid)
			oidclr(oi->delta_base_oid, loose->base.odb->repo->hash_algo);
		if (!ret)
			oi->whence = OI_LOOSE;
	}

	return ret;
}

static int odb_source_loose_read_object_info(struct odb_source *source,
					     const struct object_id *oid,
					     struct object_info *oi,
					     enum object_info_flags flags)
{
	struct odb_source_loose *loose = odb_source_loose_downcast(source);
	static struct strbuf buf = STRBUF_INIT;

	/*
	 * The second read shouldn't cause new loose objects to show up, unless
	 * there was a race condition with a secondary process. We don't care
	 * about this case though, so we simply skip reading loose objects a
	 * second time.
	 */
	if (flags & OBJECT_INFO_SECOND_READ)
		return -1;

	odb_loose_path(loose, &buf, oid);
	return read_object_info_from_path(loose, buf.buf, oid, oi, flags);
}

/*
 * Find "oid" as a loose object in given source, open the object and return its
 * file descriptor. Returns the file descriptor on success, negative on failure.
 *
 * The "path" out-parameter will give the path of the object we found (if any).
 * Note that it may point to static storage and is only valid until another
 * call to open_loose_object().
 */
static int open_loose_object(struct odb_source_loose *loose,
			     const struct object_id *oid, const char **path)
{
	static struct strbuf buf = STRBUF_INIT;
	int fd;

	*path = odb_loose_path(loose, &buf, oid);
	fd = git_open(*path);
	if (fd >= 0)
		return fd;

	return -1;
}

static void *odb_source_loose_map_object(struct odb_source_loose *loose,
					 const struct object_id *oid,
					 unsigned long *size)
{
	const char *p;
	int fd = open_loose_object(loose, oid, &p);
	void *map = NULL;
	struct stat st;

	if (fd < 0)
		return NULL;

	if (!fstat(fd, &st)) {
		*size = xsize_t(st.st_size);
		if (!*size) {
			/* mmap() is forbidden on empty files */
			error(_("object file %s is empty"), p);
			goto out;
		}

		map = xmmap(NULL, *size, PROT_READ, MAP_PRIVATE, fd, 0);
	}

out:
	close(fd);
	return map;
}

struct odb_loose_read_stream {
	struct odb_read_stream base;
	git_zstream z;
	enum {
		ODB_LOOSE_READ_STREAM_INUSE,
		ODB_LOOSE_READ_STREAM_DONE,
		ODB_LOOSE_READ_STREAM_ERROR,
	} z_state;
	void *mapped;
	unsigned long mapsize;
	char hdr[32];
	int hdr_avail;
	int hdr_used;
};

static ssize_t read_istream_loose(struct odb_read_stream *_st, char *buf, size_t sz)
{
	struct odb_loose_read_stream *st =
		container_of(_st, struct odb_loose_read_stream, base);
	size_t total_read = 0;

	switch (st->z_state) {
	case ODB_LOOSE_READ_STREAM_DONE:
		return 0;
	case ODB_LOOSE_READ_STREAM_ERROR:
		return -1;
	default:
		break;
	}

	if (st->hdr_used < st->hdr_avail) {
		size_t to_copy = st->hdr_avail - st->hdr_used;
		if (sz < to_copy)
			to_copy = sz;
		memcpy(buf, st->hdr + st->hdr_used, to_copy);
		st->hdr_used += to_copy;
		total_read += to_copy;
	}

	while (total_read < sz) {
		int status;

		st->z.next_out = (unsigned char *)buf + total_read;
		st->z.avail_out = sz - total_read;
		status = git_inflate(&st->z, Z_FINISH);

		total_read = st->z.next_out - (unsigned char *)buf;

		if (status == Z_STREAM_END) {
			git_inflate_end(&st->z);
			st->z_state = ODB_LOOSE_READ_STREAM_DONE;
			break;
		}
		if (status != Z_OK && (status != Z_BUF_ERROR || total_read < sz)) {
			git_inflate_end(&st->z);
			st->z_state = ODB_LOOSE_READ_STREAM_ERROR;
			return -1;
		}
	}
	return total_read;
}

static int close_istream_loose(struct odb_read_stream *_st)
{
	struct odb_loose_read_stream *st =
		container_of(_st, struct odb_loose_read_stream, base);

	if (st->z_state == ODB_LOOSE_READ_STREAM_INUSE)
		git_inflate_end(&st->z);
	munmap(st->mapped, st->mapsize);
	return 0;
}

static int odb_source_loose_read_object_stream(struct odb_read_stream **out,
					       struct odb_source *source,
					       const struct object_id *oid)
{
	struct odb_source_loose *loose = odb_source_loose_downcast(source);
	struct object_info oi = OBJECT_INFO_INIT;
	struct odb_loose_read_stream *st;
	unsigned long mapsize;
	void *mapped;

	mapped = odb_source_loose_map_object(loose, oid, &mapsize);
	if (!mapped)
		return -1;

	/*
	 * Note: we must allocate this structure early even though we may still
	 * fail. This is because we need to initialize the zlib stream, and it
	 * is not possible to copy the stream around after the fact because it
	 * has self-referencing pointers.
	 */
	CALLOC_ARRAY(st, 1);

	switch (unpack_loose_header(&st->z, mapped, mapsize, st->hdr,
				    sizeof(st->hdr))) {
	case ULHR_OK:
		break;
	case ULHR_BAD:
	case ULHR_TOO_LONG:
		goto error;
	}

	oi.sizep = &st->base.size;
	oi.typep = &st->base.type;

	if (parse_loose_header(st->hdr, &oi) < 0 || st->base.type < 0)
		goto error;

	st->mapped = mapped;
	st->mapsize = mapsize;
	st->hdr_used = strlen(st->hdr) + 1;
	st->hdr_avail = st->z.total_out;
	st->z_state = ODB_LOOSE_READ_STREAM_INUSE;
	st->base.close = close_istream_loose;
	st->base.read = read_istream_loose;

	*out = &st->base;

	return 0;
error:
	git_inflate_end(&st->z);
	munmap(mapped, mapsize);
	free(st);
	return -1;
}

struct for_each_object_wrapper_data {
	struct odb_source_loose *loose;
	const struct object_info *request;
	odb_for_each_object_cb cb;
	void *cb_data;
};

static int for_each_object_wrapper_cb(const struct object_id *oid,
				      const char *path,
				      void *cb_data)
{
	struct for_each_object_wrapper_data *data = cb_data;

	if (data->request) {
		struct object_info oi = *data->request;

		if (read_object_info_from_path(data->loose, path, oid, &oi, 0) < 0)
			return -1;

		return data->cb(oid, &oi, data->cb_data);
	} else {
		return data->cb(oid, NULL, data->cb_data);
	}
}

static int for_each_prefixed_object_wrapper_cb(const struct object_id *oid,
					       void *node_data UNUSED,
					       void *cb_data)
{
	struct for_each_object_wrapper_data *data = cb_data;
	if (data->request) {
		struct object_info oi = *data->request;

		if (odb_source_read_object_info(&data->loose->base,
						oid, &oi, 0) < 0)
			return -1;

		return data->cb(oid, &oi, data->cb_data);
	} else {
		return data->cb(oid, NULL, data->cb_data);
	}
}

static int odb_source_loose_for_each_object(struct odb_source *source,
					    const struct object_info *request,
					    odb_for_each_object_cb cb,
					    void *cb_data,
					    const struct odb_for_each_object_options *opts)
{
	struct odb_source_loose *loose = odb_source_loose_downcast(source);
	struct for_each_object_wrapper_data data = {
		.loose = loose,
		.request = request,
		.cb = cb,
		.cb_data = cb_data,
	};

	/* There are no loose promisor objects, so we can return immediately. */
	if ((opts->flags & ODB_FOR_EACH_OBJECT_PROMISOR_ONLY))
		return 0;
	if ((opts->flags & ODB_FOR_EACH_OBJECT_LOCAL_ONLY) && !source->local)
		return 0;

	if (opts->prefix)
		return oidtree_each(odb_source_loose_cache(loose, opts->prefix),
				    opts->prefix, opts->prefix_hex_len,
				    for_each_prefixed_object_wrapper_cb, &data);

	return for_each_loose_file_in_source(source, for_each_object_wrapper_cb,
					     NULL, NULL, &data);
}

struct find_abbrev_len_data {
	const struct object_id *oid;
	unsigned len;
};

static int find_abbrev_len_cb(const struct object_id *oid,
			      struct object_info *oi UNUSED,
			      void *cb_data)
{
	struct find_abbrev_len_data *data = cb_data;
	unsigned len = oid_common_prefix_hexlen(oid, data->oid);
	if (len != hash_algos[oid->algo].hexsz && len >= data->len)
		data->len = len + 1;
	return 0;
}

static int odb_source_loose_find_abbrev_len(struct odb_source *source,
					    const struct object_id *oid,
					    unsigned min_len,
					    unsigned *out)
{
	struct odb_source_loose *loose = odb_source_loose_downcast(source);
	struct odb_for_each_object_options opts = {
		.prefix = oid,
		.prefix_hex_len = min_len,
	};
	struct find_abbrev_len_data data = {
		.oid = oid,
		.len = min_len,
	};
	int ret;

	ret = odb_source_for_each_object(&loose->base, NULL, find_abbrev_len_cb,
					 &data, &opts);
	*out = data.len;

	return ret;
}

static int count_loose_object(const struct object_id *oid UNUSED,
			      struct object_info *oi UNUSED,
			      void *payload)
{
	unsigned long *count = payload;
	(*count)++;
	return 0;
}

static int odb_source_loose_count_objects(struct odb_source *source,
					  enum odb_count_objects_flags flags,
					  unsigned long *out)
{
	struct odb_source_loose *loose = odb_source_loose_downcast(source);
	const unsigned hexsz = source->odb->repo->hash_algo->hexsz - 2;
	char *path = NULL;
	DIR *dir = NULL;
	int ret;

	if (flags & ODB_COUNT_OBJECTS_APPROXIMATE) {
		unsigned long count = 0;
		struct dirent *ent;

		path = xstrfmt("%s/17", source->path);

		dir = opendir(path);
		if (!dir) {
			if (errno == ENOENT) {
				*out = 0;
				ret = 0;
				goto out;
			}

			ret = error_errno("cannot open object shard '%s'", path);
			goto out;
		}

		while ((ent = readdir(dir)) != NULL) {
			if (strspn(ent->d_name, "0123456789abcdef") != hexsz ||
			    ent->d_name[hexsz] != '\0')
				continue;
			count++;
		}

		*out = count * 256;
		ret = 0;
	} else {
		struct odb_for_each_object_options opts = { 0 };
		*out = 0;
		ret = odb_source_for_each_object(&loose->base, NULL, count_loose_object,
						 out, &opts);
	}

out:
	if (dir)
		closedir(dir);
	free(path);
	return ret;
}

static int odb_source_loose_freshen_object(struct odb_source *source,
					   const struct object_id *oid)
{
	struct odb_source_loose *loose = odb_source_loose_downcast(source);
	static struct strbuf path = STRBUF_INIT;
	odb_loose_path(loose, &path, oid);
	return !!check_and_freshen_file(path.buf, 1);
}

static int odb_source_loose_write_object(struct odb_source *source,
					 const void *buf, size_t len,
					 enum object_type type, struct object_id *oid,
					 struct object_id *compat_oid_in,
					 enum odb_write_object_flags flags)
{
	struct odb_source_loose *loose = odb_source_loose_downcast(source);
	const struct git_hash_algo *algo = source->odb->repo->hash_algo;
	const struct git_hash_algo *compat = source->odb->repo->compat_hash_algo;
	struct object_id compat_oid;
	char hdr[MAX_HEADER_LEN];
	size_t hdrlen = sizeof(hdr);

	/* Generate compat_oid */
	if (compat) {
		if (compat_oid_in)
			oidcpy(&compat_oid, compat_oid_in);
		else if (type == OBJ_BLOB)
			hash_object_file(compat, buf, len, type, &compat_oid);
		else {
			struct strbuf converted = STRBUF_INIT;
			convert_object_file(source->odb->repo, &converted, algo, compat,
					    buf, len, type, 0);
			hash_object_file(compat, converted.buf, converted.len,
					 type, &compat_oid);
			strbuf_release(&converted);
		}
	}

	/* Normally if we have it in the pack then we do not bother writing
	 * it out into .git/objects/??/?{38} file.
	 */
	write_object_file_prepare(algo, buf, len, type, oid, hdr, &hdrlen);
	if (odb_freshen_object(source->odb, oid))
		return 0;
	if (write_loose_object(loose, oid, hdr, hdrlen, buf, len, 0, flags))
		return -1;
	if (compat)
		return repo_add_loose_object_map(loose, oid, &compat_oid);
	return 0;
}

static int odb_source_loose_write_object_stream(struct odb_source *source,
						struct odb_write_stream *in_stream,
						size_t len,
						struct object_id *oid)
{
	/*
	 * TODO: the implementation should be moved here, see the comment on
	 * the called function in "object-file.h".
	 */
	struct odb_source_loose *loose = odb_source_loose_downcast(source);
	return odb_source_loose_write_stream(loose, in_stream, len, oid);
}

static int odb_source_loose_begin_transaction(struct odb_source *source UNUSED,
					      struct odb_transaction **out UNUSED)
{
	/* TODO: this is a known omission that we'll want to address eventually. */
	return error("loose source does not support transactions");
}

static int odb_source_loose_read_alternates(struct odb_source *source UNUSED,
					    struct strvec *out UNUSED)
{
	return 0;
}

static int odb_source_loose_write_alternate(struct odb_source *source UNUSED,
					    const char *alternate UNUSED)
{
	return error("loose source does not support alternates");
}

static void odb_source_loose_clear_cache(struct odb_source_loose *loose)
{
	oidtree_clear(loose->cache);
	FREE_AND_NULL(loose->cache);
	memset(&loose->subdir_seen, 0,
	       sizeof(loose->subdir_seen));
}

static void odb_source_loose_reprepare(struct odb_source *source)
{
	struct odb_source_loose *loose = odb_source_loose_downcast(source);
	odb_source_loose_clear_cache(loose);
}

static void odb_source_loose_close(struct odb_source *source UNUSED)
{
	/* Nothing to do. */
}

static void odb_source_loose_reparent(const char *name UNUSED,
				      const char *old_cwd,
				      const char *new_cwd,
				      void *cb_data)
{
	struct odb_source_loose *loose = cb_data;
	char *path = reparent_relative_path(old_cwd, new_cwd,
					    loose->base.path);
	free(loose->base.path);
	loose->base.path = path;
}

static void odb_source_loose_free(struct odb_source *source)
{
	struct odb_source_loose *loose = odb_source_loose_downcast(source);
	odb_source_loose_clear_cache(loose);
	loose_object_map_clear(&loose->map);
	chdir_notify_unregister(NULL, odb_source_loose_reparent, loose);
	odb_source_release(&loose->base);
	free(loose);
}

struct odb_source_loose *odb_source_loose_new(struct object_database *odb,
					      const char *path,
					      bool local)
{
	struct odb_source_loose *loose;

	CALLOC_ARRAY(loose, 1);
	odb_source_init(&loose->base, odb, ODB_SOURCE_LOOSE, path, local);

	loose->base.free = odb_source_loose_free;
	loose->base.close = odb_source_loose_close;
	loose->base.reprepare = odb_source_loose_reprepare;
	loose->base.read_object_info = odb_source_loose_read_object_info;
	loose->base.read_object_stream = odb_source_loose_read_object_stream;
	loose->base.for_each_object = odb_source_loose_for_each_object;
	loose->base.find_abbrev_len = odb_source_loose_find_abbrev_len;
	loose->base.count_objects = odb_source_loose_count_objects;
	loose->base.freshen_object = odb_source_loose_freshen_object;
	loose->base.write_object = odb_source_loose_write_object;
	loose->base.write_object_stream = odb_source_loose_write_object_stream;
	loose->base.begin_transaction = odb_source_loose_begin_transaction;
	loose->base.read_alternates = odb_source_loose_read_alternates;
	loose->base.write_alternate = odb_source_loose_write_alternate;

	if (!is_absolute_path(loose->base.path))
		chdir_notify_register(NULL, odb_source_loose_reparent, loose);

	return loose;
}
