#include "cache.h"
#include "blob.h"

static char *create_temp_file(unsigned char *sha1)
{
	static char path[50];
	void *buf;
	char type[100];
	unsigned long size;
	int fd;

	buf = read_sha1_file(sha1, type, &size);
	if (!buf || strcmp(type, blob_type))
		die("unable to read blob object %s", sha1_to_hex(sha1));

	strcpy(path, ".merge_file_XXXXXX");
	fd = mkstemp(path);
	if (fd < 0)
		die("unable to create temp-file");
	if (write_in_full(fd, buf, size) != size)
		die("unable to write temp-file");
	close(fd);
	return path;
}

int main(int argc, char **argv)
{
	unsigned char sha1[20];

	if (argc != 2)
		usage("git-unpack-file <sha1>");
	if (get_sha1(argv[1], sha1))
		die("Not a valid object name %s", argv[1]);

	setup_git_directory();
	git_config(git_default_config);

	puts(create_temp_file(sha1));
	return 0;
}
