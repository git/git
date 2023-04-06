#include "test-tool.h"
#include "bloom.h"
#include "hex.h"
#include "commit.h"
#include "setup.h"

static struct bloom_filter_settings settings = DEFAULT_BLOOM_FILTER_SETTINGS;

static void add_string_to_filter(const char *data, struct bloom_filter *filter) {
		struct bloom_key key;
		int i;

		fill_bloom_key(data, strlen(data), &key, &settings);
		printf("Hashes:");
		for (i = 0; i < settings.num_hashes; i++){
			printf("0x%08x|", key.hashes[i]);
		}
		printf("\n");
		add_key_to_filter(&key, filter, &settings);
		clear_bloom_key(&key);
}

static void print_bloom_filter(struct bloom_filter *filter) {
	int i;

	if (!filter) {
		printf("No filter.\n");
		return;
	}
	printf("Filter_Length:%d\n", (int)filter->len);
	printf("Filter_Data:");
	for (i = 0; i < filter->len; i++) {
		printf("%02x|", filter->data[i]);
	}
	printf("\n");
}

static void get_bloom_filter_for_commit(const struct object_id *commit_oid)
{
	struct commit *c;
	struct bloom_filter *filter;
	setup_git_directory();
	c = lookup_commit(the_repository, commit_oid);
	filter = get_or_compute_bloom_filter(the_repository, c, 1,
					     &settings,
					     NULL);
	print_bloom_filter(filter);
}

static const char *bloom_usage = "\n"
"  test-tool bloom get_murmur3 <string>\n"
"  test-tool bloom generate_filter <string> [<string>...]\n"
"  test-tool bloom get_filter_for_commit <commit-hex>\n";

int cmd__bloom(int argc, const char **argv)
{
	setup_git_directory();

	if (argc < 2)
		usage(bloom_usage);

	if (!strcmp(argv[1], "get_murmur3")) {
		uint32_t hashed;
		if (argc < 3)
			usage(bloom_usage);
		hashed = murmur3_seeded(0, argv[2], strlen(argv[2]));
		printf("Murmur3 Hash with seed=0:0x%08x\n", hashed);
	}

	if (!strcmp(argv[1], "generate_filter")) {
		struct bloom_filter filter;
		int i = 2;
		filter.len =  (settings.bits_per_entry + BITS_PER_WORD - 1) / BITS_PER_WORD;
		CALLOC_ARRAY(filter.data, filter.len);

		if (argc - 1 < i)
			usage(bloom_usage);

		while (argv[i]) {
			add_string_to_filter(argv[i], &filter);
			i++;
		}

		print_bloom_filter(&filter);
		free(filter.data);
	}

	if (!strcmp(argv[1], "get_filter_for_commit")) {
		struct object_id oid;
		const char *end;
		if (argc < 3)
			usage(bloom_usage);
		if (parse_oid_hex(argv[2], &oid, &end))
			die("cannot parse oid '%s'", argv[2]);
		init_bloom_filters();
		get_bloom_filter_for_commit(&oid);
	}

	return 0;
}
