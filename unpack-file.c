#include "cache.h"

static char *create_temp_file(unsigned char *sha1)
{
	static char path[50];
	void *buf;
	char type[100];
	unsigned long size;
	int fd;

	buf = read_sha1_file(sha1, type, &size);
	if (!buf || strcmp(type, "blob"))
		die("unable to read blob object %s", sha1_to_hex(sha1));

	strcpy(path, ".merge_file_XXXXXX");
	fd = mkstemp(path);
	if (fd < 0)
		die("unable to create temp-file");
	if (write(fd, buf, size) != size)
		die("unable to write temp-file");
	close(fd);
	return path;
}

int main(int argc, char **argv)
{
	unsigned char sha1[20];

	if (argc != 2 || get_sha1(argv[1], sha1))
		usage("git-unpack-file <sha1>");

	puts(create_temp_file(sha1));
	return 0;
}
