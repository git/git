#include "cache.h"
#include "pack.h"

static const char show_index_usage[] =
"git show-index < <packed archive index>";

int main(int argc, char **argv)
{
	int i;
	unsigned nr;
	unsigned int version;
	static unsigned int top_index[256];

	if (argc != 1)
		usage(show_index_usage);
	if (fread(top_index, 2 * 4, 1, stdin) != 1)
		die("unable to read header");
	if (top_index[0] == htonl(PACK_IDX_SIGNATURE)) {
		version = ntohl(top_index[1]);
		if (version < 2 || version > 2)
			die("unknown index version");
		if (fread(top_index, 256 * 4, 1, stdin) != 1)
			die("unable to read index");
	} else {
		version = 1;
		if (fread(&top_index[2], 254 * 4, 1, stdin) != 1)
			die("unable to read index");
	}
	nr = 0;
	for (i = 0; i < 256; i++) {
		unsigned n = ntohl(top_index[i]);
		if (n < nr)
			die("corrupt index file");
		nr = n;
	}
	if (version == 1) {
		for (i = 0; i < nr; i++) {
			unsigned int offset, entry[6];

			if (fread(entry, 4 + 20, 1, stdin) != 1)
				die("unable to read entry %u/%u", i, nr);
			offset = ntohl(entry[0]);
			printf("%u %s\n", offset, sha1_to_hex((void *)(entry+1)));
		}
	} else {
		unsigned off64_nr = 0;
		struct {
			unsigned char sha1[20];
			uint32_t crc;
			uint32_t off;
		} *entries = xmalloc(nr * sizeof(entries[0]));
		for (i = 0; i < nr; i++)
			if (fread(entries[i].sha1, 20, 1, stdin) != 1)
				die("unable to read sha1 %u/%u", i, nr);
		for (i = 0; i < nr; i++)
			if (fread(&entries[i].crc, 4, 1, stdin) != 1)
				die("unable to read crc %u/%u", i, nr);
		for (i = 0; i < nr; i++)
			if (fread(&entries[i].off, 4, 1, stdin) != 1)
				die("unable to read 32b offset %u/%u", i, nr);
		for (i = 0; i < nr; i++) {
			uint64_t offset;
			uint32_t off = ntohl(entries[i].off);
			if (!(off & 0x80000000)) {
				offset = off;
			} else {
				uint32_t off64[2];
				if ((off & 0x7fffffff) != off64_nr)
					die("inconsistent 64b offset index");
				if (fread(off64, 8, 1, stdin) != 1)
					die("unable to read 64b offset %u", off64_nr);
				offset = (((uint64_t)ntohl(off64[0])) << 32) |
						     ntohl(off64[1]);
				off64_nr++;
			}
			printf("%" PRIuMAX " %s (%08"PRIx32")\n",
			       (uintmax_t) offset,
			       sha1_to_hex(entries[i].sha1),
			       ntohl(entries[i].crc));
		}
		free(entries);
	}
	return 0;
}
