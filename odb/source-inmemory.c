#include "git-compat-util.h"
#include "object-file.h"
#include "odb.h"
#include "odb/source-inmemory.h"
#include "odb/streaming.h"
#include "oidtree.h"
#include "repository.h"

struct inmemory_object {
	enum object_type type;
	const void *buf;
	unsigned long size;
};

static const struct inmemory_object *find_cached_object(struct odb_source_inmemory *source,
							const struct object_id *oid)
{
	static const struct inmemory_object empty_tree = {
		.type = OBJ_TREE,
		.buf = "",
	};
	const struct inmemory_object *object;

	if (source->objects) {
		object = oidtree_get(source->objects, oid);
		if (object)
			return object;
	}

	if (oid->algo && oideq(oid, hash_algos[oid->algo].empty_tree))
		return &empty_tree;

	return NULL;
}

static void populate_object_info(struct odb_source_inmemory *source,
				 struct object_info *oi,
				 const struct inmemory_object *object)
{
	if (!oi)
		return;

	if (oi->typep)
		*(oi->typep) = object->type;
	if (oi->sizep)
		*(oi->sizep) = object->size;
	if (oi->disk_sizep)
		*(oi->disk_sizep) = 0;
	if (oi->delta_base_oid)
		oidclr(oi->delta_base_oid, source->base.odb->repo->hash_algo);
	if (oi->contentp)
		*oi->contentp = xmemdupz(object->buf, object->size);
	if (oi->mtimep)
		*oi->mtimep = 0;
	oi->whence = OI_CACHED;
}

static int odb_source_inmemory_read_object_info(struct odb_source *source,
						const struct object_id *oid,
						struct object_info *oi,
						enum object_info_flags flags UNUSED)
{
	struct odb_source_inmemory *inmemory = odb_source_inmemory_downcast(source);
	const struct inmemory_object *object;

	object = find_cached_object(inmemory, oid);
	if (!object)
		return -1;

	populate_object_info(inmemory, oi, object);
	return 0;
}

struct odb_read_stream_inmemory {
	struct odb_read_stream base;
	const unsigned char *buf;
	size_t offset;
};

static ssize_t odb_read_stream_inmemory_read(struct odb_read_stream *stream,
					     char *buf, size_t buf_len)
{
	struct odb_read_stream_inmemory *inmemory =
		container_of(stream, struct odb_read_stream_inmemory, base);
	size_t bytes = buf_len;

	if (buf_len > inmemory->base.size - inmemory->offset)
		bytes = inmemory->base.size - inmemory->offset;

	memcpy(buf, inmemory->buf + inmemory->offset, bytes);
	inmemory->offset += bytes;

	return bytes;
}

static int odb_read_stream_inmemory_close(struct odb_read_stream *stream UNUSED)
{
	return 0;
}

static int odb_source_inmemory_read_object_stream(struct odb_read_stream **out,
						  struct odb_source *source,
						  const struct object_id *oid)
{
	struct odb_source_inmemory *inmemory = odb_source_inmemory_downcast(source);
	struct odb_read_stream_inmemory *stream;
	const struct inmemory_object *object;

	object = find_cached_object(inmemory, oid);
	if (!object)
		return -1;

	CALLOC_ARRAY(stream, 1);
	stream->base.read = odb_read_stream_inmemory_read;
	stream->base.close = odb_read_stream_inmemory_close;
	stream->base.size = object->size;
	stream->base.type = object->type;
	stream->buf = object->buf;

	*out = &stream->base;
	return 0;
}

struct odb_source_inmemory_for_each_object_data {
	struct odb_source_inmemory *inmemory;
	const struct object_info *request;
	odb_for_each_object_cb cb;
	void *cb_data;
};

static int odb_source_inmemory_for_each_object_cb(const struct object_id *oid,
						  void *node_data, void *cb_data)
{
	struct odb_source_inmemory_for_each_object_data *data = cb_data;
	struct inmemory_object *object = node_data;

	if (data->request) {
		struct object_info oi = *data->request;
		populate_object_info(data->inmemory, &oi, object);
		return data->cb(oid, &oi, data->cb_data);
	} else {
		return data->cb(oid, NULL, data->cb_data);
	}
}

static int odb_source_inmemory_for_each_object(struct odb_source *source,
					       const struct object_info *request,
					       odb_for_each_object_cb cb,
					       void *cb_data,
					       const struct odb_for_each_object_options *opts)
{
	struct odb_source_inmemory *inmemory = odb_source_inmemory_downcast(source);
	struct odb_source_inmemory_for_each_object_data payload = {
		.inmemory = inmemory,
		.request = request,
		.cb = cb,
		.cb_data = cb_data,
	};
	struct object_id null_oid = { 0 };

	if ((opts->flags & ODB_FOR_EACH_OBJECT_PROMISOR_ONLY) ||
	    (opts->flags & ODB_FOR_EACH_OBJECT_LOCAL_ONLY && !source->local))
		return 0;
	if (!inmemory->objects)
		return 0;

	return oidtree_each(inmemory->objects,
			    opts->prefix ? opts->prefix : &null_oid, opts->prefix_hex_len,
			    odb_source_inmemory_for_each_object_cb, &payload);
}

static int odb_source_inmemory_write_object(struct odb_source *source,
					    const void *buf, unsigned long len,
					    enum object_type type,
					    struct object_id *oid,
					    struct object_id *compat_oid UNUSED,
					    enum odb_write_object_flags flags UNUSED)
{
	struct odb_source_inmemory *inmemory = odb_source_inmemory_downcast(source);
	struct inmemory_object *object;

	hash_object_file(source->odb->repo->hash_algo, buf, len, type, oid);

	if (!inmemory->objects) {
		CALLOC_ARRAY(inmemory->objects, 1);
		oidtree_init(inmemory->objects);
	} else if (oidtree_contains(inmemory->objects, oid)) {
		return 0;
	}

	CALLOC_ARRAY(object, 1);
	object->size = len;
	object->type = type;
	object->buf = xmemdupz(buf, len);

	oidtree_insert(inmemory->objects, oid, object);

	return 0;
}

static int odb_source_inmemory_write_object_stream(struct odb_source *source,
						   struct odb_write_stream *stream,
						   size_t len,
						   struct object_id *oid)
{
	char buf[16384];
	size_t total_read = 0;
	char *data;
	int ret;

	CALLOC_ARRAY(data, len);
	while (!stream->is_finished) {
		ssize_t bytes_read;

		bytes_read = odb_write_stream_read(stream, buf, sizeof(buf));
		if (total_read + bytes_read > len) {
			ret = error("object stream yielded more bytes than expected");
			goto out;
		}

		memcpy(data + total_read, buf, bytes_read);
		total_read += bytes_read;
	}

	if (total_read != len) {
		ret = error("object stream yielded less bytes than expected");
		goto out;
	}

	ret = odb_source_inmemory_write_object(source, data, len, OBJ_BLOB, oid,
					       NULL, 0);
	if (ret < 0)
		goto out;

out:
	free(data);
	return ret;
}

static int inmemory_object_free(const struct object_id *oid UNUSED,
				void *node_data,
				void *cb_data UNUSED)
{
	struct inmemory_object *object = node_data;
	free((void *) object->buf);
	free(object);
	return 0;
}

static void odb_source_inmemory_free(struct odb_source *source)
{
	struct odb_source_inmemory *inmemory = odb_source_inmemory_downcast(source);

	if (inmemory->objects) {
		struct object_id null_oid = { 0 };

		oidtree_each(inmemory->objects, &null_oid, 0,
			     inmemory_object_free, NULL);
		oidtree_clear(inmemory->objects);
		free(inmemory->objects);
	}

	free(inmemory->base.path);
	free(inmemory);
}

struct odb_source_inmemory *odb_source_inmemory_new(struct object_database *odb)
{
	struct odb_source_inmemory *source;

	CALLOC_ARRAY(source, 1);
	odb_source_init(&source->base, odb, ODB_SOURCE_INMEMORY, "source", false);

	source->base.free = odb_source_inmemory_free;
	source->base.read_object_info = odb_source_inmemory_read_object_info;
	source->base.read_object_stream = odb_source_inmemory_read_object_stream;
	source->base.for_each_object = odb_source_inmemory_for_each_object;
	source->base.write_object = odb_source_inmemory_write_object;
	source->base.write_object_stream = odb_source_inmemory_write_object_stream;

	return source;
}
