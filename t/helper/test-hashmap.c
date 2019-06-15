#include "test-tool.h"
#include "git-compat-util.h"
#include "hashmap.h"
#include "strbuf.h"

struct test_entry
{
	struct hashmap_entry ent;
	/* key and value as two \0-terminated strings */
	char key[FLEX_ARRAY];
};

static const char *get_value(const struct test_entry *e)
{
	return e->key + strlen(e->key) + 1;
}

static int test_entry_cmp(const void *cmp_data,
			  const void *entry,
			  const void *entry_or_key,
			  const void *keydata)
{
	const int ignore_case = cmp_data ? *((int *)cmp_data) : 0;
	const struct test_entry *e1 = entry;
	const struct test_entry *e2 = entry_or_key;
	const char *key = keydata;

	if (ignore_case)
		return strcasecmp(e1->key, key ? key : e2->key);
	else
		return strcmp(e1->key, key ? key : e2->key);
}

static struct test_entry *alloc_test_entry(unsigned int hash,
					   char *key, char *value)
{
	size_t klen = strlen(key);
	size_t vlen = strlen(value);
	struct test_entry *entry = xmalloc(st_add4(sizeof(*entry), klen, vlen, 2));
	hashmap_entry_init(entry, hash);
	memcpy(entry->key, key, klen + 1);
	memcpy(entry->key + klen + 1, value, vlen + 1);
	return entry;
}

#define HASH_METHOD_FNV 0
#define HASH_METHOD_I 1
#define HASH_METHOD_IDIV10 2
#define HASH_METHOD_0 3
#define HASH_METHOD_X2 4
#define TEST_SPARSE 8
#define TEST_ADD 16
#define TEST_SIZE 100000

static unsigned int hash(unsigned int method, unsigned int i, const char *key)
{
	unsigned int hash = 0;
	switch (method & 3)
	{
	case HASH_METHOD_FNV:
		hash = strhash(key);
		break;
	case HASH_METHOD_I:
		hash = i;
		break;
	case HASH_METHOD_IDIV10:
		hash = i / 10;
		break;
	case HASH_METHOD_0:
		hash = 0;
		break;
	}

	if (method & HASH_METHOD_X2)
		hash = 2 * hash;
	return hash;
}

/*
 * Test performance of hashmap.[ch]
 * Usage: time echo "perfhashmap method rounds" | test-tool hashmap
 */
static void perf_hashmap(unsigned int method, unsigned int rounds)
{
	struct hashmap map;
	char buf[16];
	struct test_entry **entries;
	unsigned int *hashes;
	unsigned int i, j;

	ALLOC_ARRAY(entries, TEST_SIZE);
	ALLOC_ARRAY(hashes, TEST_SIZE);
	for (i = 0; i < TEST_SIZE; i++) {
		xsnprintf(buf, sizeof(buf), "%i", i);
		entries[i] = alloc_test_entry(0, buf, "");
		hashes[i] = hash(method, i, entries[i]->key);
	}

	if (method & TEST_ADD) {
		/* test adding to the map */
		for (j = 0; j < rounds; j++) {
			hashmap_init(&map, test_entry_cmp, NULL, 0);

			/* add entries */
			for (i = 0; i < TEST_SIZE; i++) {
				hashmap_entry_init(entries[i], hashes[i]);
				hashmap_add(&map, entries[i]);
			}

			hashmap_free(&map, 0);
		}
	} else {
		/* test map lookups */
		hashmap_init(&map, test_entry_cmp, NULL, 0);

		/* fill the map (sparsely if specified) */
		j = (method & TEST_SPARSE) ? TEST_SIZE / 10 : TEST_SIZE;
		for (i = 0; i < j; i++) {
			hashmap_entry_init(entries[i], hashes[i]);
			hashmap_add(&map, entries[i]);
		}

		for (j = 0; j < rounds; j++) {
			for (i = 0; i < TEST_SIZE; i++) {
				hashmap_get_from_hash(&map, hashes[i],
						      entries[i]->key);
			}
		}

		hashmap_free(&map, 0);
	}
}

#define DELIM " \t\r\n"

/*
 * Read stdin line by line and print result of commands to stdout:
 *
 * hash key -> strhash(key) memhash(key) strihash(key) memihash(key)
 * put key value -> NULL / old value
 * get key -> NULL / value
 * remove key -> NULL / old value
 * iterate -> key1 value1\nkey2 value2\n...
 * size -> tablesize numentries
 *
 * perfhashmap method rounds -> test hashmap.[ch] performance
 */
int cmd__hashmap(int argc, const char **argv)
{
	struct strbuf line = STRBUF_INIT;
	struct hashmap map;
	int icase;

	/* init hash map */
	icase = argc > 1 && !strcmp("ignorecase", argv[1]);
	hashmap_init(&map, test_entry_cmp, &icase, 0);

	/* process commands from stdin */
	while (strbuf_getline(&line, stdin) != EOF) {
		char *cmd, *p1 = NULL, *p2 = NULL;
		unsigned int hash = 0;
		struct test_entry *entry;

		/* break line into command and up to two parameters */
		cmd = strtok(line.buf, DELIM);
		/* ignore empty lines */
		if (!cmd || *cmd == '#')
			continue;

		p1 = strtok(NULL, DELIM);
		if (p1) {
			hash = icase ? strihash(p1) : strhash(p1);
			p2 = strtok(NULL, DELIM);
		}

		if (!strcmp("add", cmd) && p1 && p2) {

			/* create entry with key = p1, value = p2 */
			entry = alloc_test_entry(hash, p1, p2);

			/* add to hashmap */
			hashmap_add(&map, entry);

		} else if (!strcmp("put", cmd) && p1 && p2) {

			/* create entry with key = p1, value = p2 */
			entry = alloc_test_entry(hash, p1, p2);

			/* add / replace entry */
			entry = hashmap_put(&map, entry);

			/* print and free replaced entry, if any */
			puts(entry ? get_value(entry) : "NULL");
			free(entry);

		} else if (!strcmp("get", cmd) && p1) {

			/* lookup entry in hashmap */
			entry = hashmap_get_from_hash(&map, hash, p1);

			/* print result */
			if (!entry)
				puts("NULL");
			while (entry) {
				puts(get_value(entry));
				entry = hashmap_get_next(&map, entry);
			}

		} else if (!strcmp("remove", cmd) && p1) {

			/* setup static key */
			struct hashmap_entry key;
			hashmap_entry_init(&key, hash);

			/* remove entry from hashmap */
			entry = hashmap_remove(&map, &key, p1);

			/* print result and free entry*/
			puts(entry ? get_value(entry) : "NULL");
			free(entry);

		} else if (!strcmp("iterate", cmd)) {

			struct hashmap_iter iter;
			hashmap_iter_init(&map, &iter);
			while ((entry = hashmap_iter_next(&iter)))
				printf("%s %s\n", entry->key, get_value(entry));

		} else if (!strcmp("size", cmd)) {

			/* print table sizes */
			printf("%u %u\n", map.tablesize,
			       hashmap_get_size(&map));

		} else if (!strcmp("intern", cmd) && p1) {

			/* test that strintern works */
			const char *i1 = strintern(p1);
			const char *i2 = strintern(p1);
			if (strcmp(i1, p1))
				printf("strintern(%s) returns %s\n", p1, i1);
			else if (i1 == p1)
				printf("strintern(%s) returns input pointer\n", p1);
			else if (i1 != i2)
				printf("strintern(%s) != strintern(%s)", i1, i2);
			else
				printf("%s\n", i1);

		} else if (!strcmp("perfhashmap", cmd) && p1 && p2) {

			perf_hashmap(atoi(p1), atoi(p2));

		} else {

			printf("Unknown command %s\n", cmd);

		}
	}

	strbuf_release(&line);
	hashmap_free(&map, 1);
	return 0;
}
