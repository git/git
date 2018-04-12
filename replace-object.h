#ifndef REPLACE_OBJECT_H
#define REPLACE_OBJECT_H

struct replace_object {
	struct oidmap_entry original;
	struct object_id replacement;
};

#endif /* REPLACE_OBJECT_H */
