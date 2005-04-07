#include "cache.h"

int main(int argc, char **argv)
{
	unsigned char sha1[20];
	char type[20];
	void *buf;
	unsigned long size;
	char template[] = "temp_git_file_XXXXXX";
	int fd;

	if (argc != 2 || get_sha1_hex(argv[1], sha1))
		usage("cat-file: cat-file <sha1>");
	buf = read_sha1_file(sha1, type, &size);
	if (!buf)
		exit(1);
	fd = mkstemp(template);
	if (fd < 0)
		usage("unable to create tempfile");
	if (write(fd, buf, size) != size)
		strcpy(type, "bad");
	printf("%s: %s\n", template, type);
}
