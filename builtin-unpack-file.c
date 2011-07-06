#include "cache.h"
#include "blob.h"
#include "exec_cmd.h"

static char *create_temp_file(unsigned char *sha1)
{
	static char path[50];
	void *buf;
	enum object_type type;
	unsigned long size;
	int fd;

	buf = read_sha1_file(sha1, &type, &size);
	if (!buf || type != OBJ_BLOB)
		die("unable to read blob object %s", sha1_to_hex(sha1));

	strcpy(path, ".merge_file_XXXXXX");
	fd = xmkstemp(path);
	if (write_in_full(fd, buf, size) != size)
		die_errno("unable to write temp-file");
	close(fd);
	return path;
}

int cmd_unpack_file(int argc, const char **argv, const char *prefix)
{
	unsigned char sha1[20];

	if (argc != 2 || !strcmp(argv[1], "-h"))
		usage("git unpack-file <sha1>");
	if (get_sha1(argv[1], sha1))
		die("Not a valid object name %s", argv[1]);

	git_config(git_default_config, NULL);

	puts(create_temp_file(sha1));
	return 0;
}
