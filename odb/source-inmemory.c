#include "git-compat-util.h"
#include "odb.h"
#include "odb/source-inmemory.h"
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

	return source;
}
