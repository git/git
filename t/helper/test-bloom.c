<<<<<<< HEAD
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
=======
#include "git-compat-util.h"
#include "bloom.h"
#include "test-tool.h"
#include "commit.h"

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
}
>>>>>>> upstream/maint

static void print_bloom_filter(struct bloom_filter *filter) {
	int i;

	if (!filter) {
		printf("No filter.\n");
		return;
	}
<<<<<<< HEAD
	printf("Filter_Length:%d\n", filter->len);
	printf("Filter_Data:");
	for (i = 0; i < filter->len; i++){
=======
	printf("Filter_Length:%d\n", (int)filter->len);
	printf("Filter_Data:");
	for (i = 0; i < filter->len; i++) {
>>>>>>> upstream/maint
		printf("%02x|", filter->data[i]);
	}
	printf("\n");
}

<<<<<<< HEAD
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

=======
>>>>>>> upstream/maint
static void get_bloom_filter_for_commit(const struct object_id *commit_oid)
{
	struct commit *c;
	struct bloom_filter *filter;
	setup_git_directory();
	c = lookup_commit(the_repository, commit_oid);
	filter = get_bloom_filter(the_repository, c, 1);
	print_bloom_filter(filter);
}

<<<<<<< HEAD
int cmd__bloom(int argc, const char **argv)
{
    if (!strcmp(argv[1], "generate_filter")) {
=======
static const char *bloom_usage = "\n"
"  test-tool bloom get_murmur3 <string>\n"
"  test-tool bloom generate_filter <string> [<string>...]\n"
"  test-tool get_filter_for_commit <commit-hex>\n";

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
>>>>>>> upstream/maint
		struct bloom_filter filter;
		int i = 2;
		filter.len =  (settings.bits_per_entry + BITS_PER_WORD - 1) / BITS_PER_WORD;
		filter.data = xcalloc(filter.len, sizeof(unsigned char));

<<<<<<< HEAD
		if (!argv[2]){
			die("at least one input string expected");
		}
=======
		if (argc - 1 < i)
			usage(bloom_usage);
>>>>>>> upstream/maint

		while (argv[i]) {
			add_string_to_filter(argv[i], &filter);
			i++;
		}

		print_bloom_filter(&filter);
	}

	if (!strcmp(argv[1], "get_filter_for_commit")) {
		struct object_id oid;
		const char *end;
<<<<<<< HEAD
		if (parse_oid_hex(argv[2], &oid, &end))
			die("cannot parse oid '%s'", argv[2]);
		load_bloom_filters();
=======
		if (argc < 3)
			usage(bloom_usage);
		if (parse_oid_hex(argv[2], &oid, &end))
			die("cannot parse oid '%s'", argv[2]);
		init_bloom_filters();
>>>>>>> upstream/maint
		get_bloom_filter_for_commit(&oid);
	}

	return 0;
}
