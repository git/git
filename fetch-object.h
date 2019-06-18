#ifndef FETCH_OBJECT_H
#define FETCH_OBJECT_H

struct object_id;

void fetch_objects(const char *remote_name, const struct object_id *oids,
		   int oid_nr);

#endif
