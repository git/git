#include "git-compat-util.h"
#include "bloom.h"
#include "test-tool.h"

struct bloom_filter_settings settings = DEFAULT_BLOOM_FILTER_SETTINGS;

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
}

static void print_bloom_filter(struct bloom_filter *filter) {
	int i;

	if (!filter) {
		printf("No filter.\n");
		return;
	}
	printf("Filter_Length:%d\n", (int)filter->len);
	printf("Filter_Data:");
	for (i = 0; i < filter->len; i++){
		printf("%02x|", filter->data[i]);
	}
	printf("\n");
}

int cmd__bloom(int argc, const char **argv)
{
	if (!strcmp(argv[1], "get_murmur3")) {
		uint32_t hashed = murmur3_seeded(0, argv[2], strlen(argv[2]));
		printf("Murmur3 Hash with seed=0:0x%08x\n", hashed);
	}

    if (!strcmp(argv[1], "generate_filter")) {
		struct bloom_filter filter;
		int i = 2;
		filter.len =  (settings.bits_per_entry + BITS_PER_WORD - 1) / BITS_PER_WORD;
		filter.data = xcalloc(filter.len, sizeof(unsigned char));

		if (!argv[2]){
			die("at least one input string expected");
		}

		while (argv[i]) {
			add_string_to_filter(argv[i], &filter);
			i++;
		}

		print_bloom_filter(&filter);
	}

	return 0;
}