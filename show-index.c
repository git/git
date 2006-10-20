#include "cache.h"

int main(int argc, char **argv)
{
	int i;
	unsigned nr;
	unsigned int entry[6];
	static unsigned int top_index[256];

	if (fread(top_index, sizeof(top_index), 1, stdin) != 1)
		die("unable to read index");
	nr = 0;
	for (i = 0; i < 256; i++) {
		unsigned n = ntohl(top_index[i]);
		if (n < nr)
			die("corrupt index file");
		nr = n;
	}
	for (i = 0; i < nr; i++) {
		unsigned offset;

		if (fread(entry, 24, 1, stdin) != 1)
			die("unable to read entry %u/%u", i, nr);
		offset = ntohl(entry[0]);
		printf("%u %s\n", offset, sha1_to_hex((void *)(entry+1)));
	}
	return 0;
}
