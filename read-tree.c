#include "cache.h"

static int unpack(unsigned char *sha1)
{
	void *buffer;
	unsigned long size;
	char type[20];

	buffer = read_sha1_file(sha1, type, &size);
	if (!buffer)
		usage("unable to read sha1 file");
	if (strcmp(type, "tree"))
		usage("expected a 'tree' node");
	while (size) {
		int len = strlen(buffer)+1;
		unsigned char *sha1 = buffer + len;
		char *path = strchr(buffer, ' ')+1;
		unsigned int mode;
		if (size < len + 20 || sscanf(buffer, "%o", &mode) != 1)
			usage("corrupt 'tree' file");
		buffer = sha1 + 20;
		size -= len + 20;
		printf("%o %s (%s)\n", mode, path, sha1_to_hex(sha1));
	}
	return 0;
}

int main(int argc, char **argv)
{
	int fd;
	unsigned char sha1[20];

	if (argc != 2)
		usage("read-tree <key>");
	if (get_sha1_hex(argv[1], sha1) < 0)
		usage("read-tree <key>");
	sha1_file_directory = getenv(DB_ENVIRONMENT);
	if (!sha1_file_directory)
		sha1_file_directory = DEFAULT_DB_ENVIRONMENT;
	if (unpack(sha1) < 0)
		usage("unpack failed");
	return 0;
}
