#include "git-compat-util.h"
#include "object-file.h"
#include "odb.h"
#include "odb/source-inmemory.h"
#include "odb/streaming.h"
#include "repository.h"

static const struct cached_object *find_cached_object(struct odb_source_inmemory *source,
						      const struct object_id *oid)
{
	static const struct cached_object empty_tree = {
		.type = OBJ_TREE,
		.buf = "",
	};
	const struct cached_object_entry *co = source->objects;

	for (size_t i = 0; i < source->objects_nr; i++, co++)
		if (oideq(&co->oid, oid))
			return &co->value;

	if (oid->algo && oideq(oid, hash_algos[oid->algo].empty_tree))
		return &empty_tree;

	return NULL;
}

static int odb_source_inmemory_read_object_info(struct odb_source *source,
						const struct object_id *oid,
						struct object_info *oi,
						enum object_info_flags flags UNUSED)
{
	struct odb_source_inmemory *inmemory = odb_source_inmemory_downcast(source);
	const struct cached_object *object;

	object = find_cached_object(inmemory, oid);
	if (!object)
		return -1;

	if (oi) {
		if (oi->typep)
			*(oi->typep) = object->type;
		if (oi->sizep)
			*(oi->sizep) = object->size;
		if (oi->disk_sizep)
			*(oi->disk_sizep) = 0;
		if (oi->delta_base_oid)
			oidclr(oi->delta_base_oid, source->odb->repo->hash_algo);
		if (oi->contentp)
			*oi->contentp = xmemdupz(object->buf, object->size);
		if (oi->mtimep)
			*oi->mtimep = 0;
		oi->whence = OI_CACHED;
	}

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
	const struct cached_object *object;

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

static int odb_source_inmemory_write_object(struct odb_source *source,
					    const void *buf, unsigned long len,
					    enum object_type type,
					    struct object_id *oid,
					    struct object_id *compat_oid UNUSED,
					    enum odb_write_object_flags flags UNUSED)
{
	struct odb_source_inmemory *inmemory = odb_source_inmemory_downcast(source);
	struct cached_object_entry *object;

	hash_object_file(source->odb->repo->hash_algo, buf, len, type, oid);

	ALLOC_GROW(inmemory->objects, inmemory->objects_nr + 1,
		   inmemory->objects_alloc);
	object = &inmemory->objects[inmemory->objects_nr++];
	object->value.size = len;
	object->value.type = type;
	object->value.buf = xmemdupz(buf, len);
	oidcpy(&object->oid, oid);

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

static void odb_source_inmemory_free(struct odb_source *source)
{
	struct odb_source_inmemory *inmemory = odb_source_inmemory_downcast(source);
	for (size_t i = 0; i < inmemory->objects_nr; i++)
		free((char *) inmemory->objects[i].value.buf);
	free(inmemory->objects);
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
	source->base.write_object = odb_source_inmemory_write_object;
	source->base.write_object_stream = odb_source_inmemory_write_object_stream;

	return source;
}
