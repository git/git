#include "builtin.h"
#include "config.h"
#include "object-store.h"

static char *create_temp_file(struct object_id *oid)
{
	static char path[50];
	void *buf;
	enum object_type type;
	unsigned long size;
	int fd;

	buf = read_object_file(oid, &type, &size);
	if (!buf || type != OBJ_BLOB)
		die("unable to read blob object %s", oid_to_hex(oid));

	xsnprintf(path, sizeof(path), ".merge_file_XXXXXX");
	fd = xmkstemp(path);
	if (write_in_full(fd, buf, size) < 0)
		die_errno("unable to write temp-file");
	close(fd);
	free(buf);
	return path;
}

int cmd_unpack_file(int argc, const char **argv, const char *prefix)
{
	struct object_id oid;

	if (argc != 2 || !strcmp(argv[1], "-h"))
		usage("git unpack-file <blob>");
	if (get_oid(argv[1], &oid))
		die("Not a valid object name %s", argv[1]);

	git_config(git_default_config, NULL);

	puts(create_temp_file(&oid));
	return 0;
}
