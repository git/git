#include "builtin.h"
#include "cache.h"
#include "pack.h"
#include "parse-options.h"

static const char *const show_index_usage[] = {
	"git show-index [--object-format=<hash-algorithm>]",
	NULL
};

int cmd_show_index(int argc, const char **argv, const char *prefix)
{
	int i;
	unsigned nr;
	unsigned int version;
	static unsigned int top_index[256];
	unsigned hashsz;
	const char *hash_name = NULL;
	int hash_algo;
	const struct option show_index_options[] = {
		OPT_STRING(0, "object-format", &hash_name, N_("hash-algorithm"),
			   N_("specify the hash algorithm to use")),
		OPT_END()
	};

	argc = parse_options(argc, argv, prefix, show_index_options, show_index_usage, 0);

	if (hash_name) {
		hash_algo = hash_algo_by_name(hash_name);
		if (hash_algo == GIT_HASH_UNKNOWN)
			die(_("Unknown hash algorithm"));
		repo_set_hash_algo(the_repository, hash_algo);
	}

	hashsz = the_hash_algo->rawsz;

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
			unsigned int offset, entry[(GIT_MAX_RAWSZ + 4) / sizeof(unsigned int)];

			if (fread(entry, 4 + hashsz, 1, stdin) != 1)
				die("unable to read entry %u/%u", i, nr);
			offset = ntohl(entry[0]);
			printf("%u %s\n", offset, hash_to_hex((void *)(entry+1)));
		}
	} else {
		unsigned off64_nr = 0;
		struct {
			struct object_id oid;
			uint32_t crc;
			uint32_t off;
		} *entries;
		ALLOC_ARRAY(entries, nr);
		for (i = 0; i < nr; i++) {
			if (fread(entries[i].oid.hash, hashsz, 1, stdin) != 1)
				die("unable to read sha1 %u/%u", i, nr);
			entries[i].oid.algo = hash_algo_by_ptr(the_hash_algo);
		}
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
			       oid_to_hex(&entries[i].oid),
			       ntohl(entries[i].crc));
		}
		free(entries);
	}
	return 0;
}
