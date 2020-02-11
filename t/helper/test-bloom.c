#include "test-tool.h"
#include "git-compat-util.h"
#include "bloom.h"
#include "test-tool.h"
#include "cache.h"
#include "commit-graph.h"
#include "commit.h"
#include "config.h"
#include "object-store.h"
#include "object.h"
#include "repository.h"
#include "tree.h"

struct bloom_filter_settings settings = DEFAULT_BLOOM_FILTER_SETTINGS;

static void print_bloom_filter(struct bloom_filter *filter) {
	int i;

	if (!filter) {
		printf("No filter.\n");
		return;
	}
	printf("Filter_Length:%d\n", filter->len);
	printf("Filter_Data:");
	for (i = 0; i < filter->len; i++){
		printf("%02x|", filter->data[i]);
	}
	printf("\n");
}

static void add_string_to_filter(const char *data, struct bloom_filter *filter) {
		struct bloom_key key;
		int i;

		fill_bloom_key(data, strlen(data), &key, &settings);
		printf("Hashes:");
		for (i = 0; i < settings.num_hashes; i++){
			printf("%08x|", key.hashes[i]);
		}
		printf("\n");
		add_key_to_filter(&key, filter, &settings);
}

static void get_bloom_filter_for_commit(const struct object_id *commit_oid)
{
	struct commit *c;
	struct bloom_filter *filter;
	setup_git_directory();
	c = lookup_commit(the_repository, commit_oid);
	filter = get_bloom_filter(the_repository, c, 1);
	print_bloom_filter(filter);
}

int cmd__bloom(int argc, const char **argv)
{
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

	if (!strcmp(argv[1], "get_filter_for_commit")) {
		struct object_id oid;
		const char *end;
		if (parse_oid_hex(argv[2], &oid, &end))
			die("cannot parse oid '%s'", argv[2]);
		load_bloom_filters();
		get_bloom_filter_for_commit(&oid);
	}

	return 0;
}
