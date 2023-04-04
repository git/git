#include "test-tool.h"
#include "cache.h"
#include "hex.h"
#include "oidmap.h"
#include "setup.h"
#include "strbuf.h"

/* key is an oid and value is a name (could be a refname for example) */
struct test_entry {
	struct oidmap_entry entry;
	char name[FLEX_ARRAY];
};

#define DELIM " \t\r\n"

/*
 * Read stdin line by line and print result of commands to stdout:
 *
 * hash oidkey -> sha1hash(oidkey)
 * put oidkey namevalue -> NULL / old namevalue
 * get oidkey -> NULL / namevalue
 * remove oidkey -> NULL / old namevalue
 * iterate -> oidkey1 namevalue1\noidkey2 namevalue2\n...
 *
 */
int cmd__oidmap(int argc, const char **argv)
{
	struct strbuf line = STRBUF_INIT;
	struct oidmap map = OIDMAP_INIT;

	setup_git_directory();

	/* init oidmap */
	oidmap_init(&map, 0);

	/* process commands from stdin */
	while (strbuf_getline(&line, stdin) != EOF) {
		char *cmd, *p1 = NULL, *p2 = NULL;
		struct test_entry *entry;
		struct object_id oid;

		/* break line into command and up to two parameters */
		cmd = strtok(line.buf, DELIM);
		/* ignore empty lines */
		if (!cmd || *cmd == '#')
			continue;

		p1 = strtok(NULL, DELIM);
		if (p1)
			p2 = strtok(NULL, DELIM);

		if (!strcmp("put", cmd) && p1 && p2) {

			if (repo_get_oid(the_repository, p1, &oid)) {
				printf("Unknown oid: %s\n", p1);
				continue;
			}

			/* create entry with oid_key = p1, name_value = p2 */
			FLEX_ALLOC_STR(entry, name, p2);
			oidcpy(&entry->entry.oid, &oid);

			/* add / replace entry */
			entry = oidmap_put(&map, entry);

			/* print and free replaced entry, if any */
			puts(entry ? entry->name : "NULL");
			free(entry);

		} else if (!strcmp("get", cmd) && p1) {

			if (repo_get_oid(the_repository, p1, &oid)) {
				printf("Unknown oid: %s\n", p1);
				continue;
			}

			/* lookup entry in oidmap */
			entry = oidmap_get(&map, &oid);

			/* print result */
			puts(entry ? entry->name : "NULL");

		} else if (!strcmp("remove", cmd) && p1) {

			if (repo_get_oid(the_repository, p1, &oid)) {
				printf("Unknown oid: %s\n", p1);
				continue;
			}

			/* remove entry from oidmap */
			entry = oidmap_remove(&map, &oid);

			/* print result and free entry*/
			puts(entry ? entry->name : "NULL");
			free(entry);

		} else if (!strcmp("iterate", cmd)) {

			struct oidmap_iter iter;
			oidmap_iter_init(&map, &iter);
			while ((entry = oidmap_iter_next(&iter)))
				printf("%s %s\n", oid_to_hex(&entry->entry.oid), entry->name);

		} else {

			printf("Unknown command %s\n", cmd);

		}
	}

	strbuf_release(&line);
	oidmap_free(&map, 1);
	return 0;
}
