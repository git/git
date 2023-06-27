#include "test-tool.h"
#include "object-name.h"
#include "object-store.h"
#include "packfile.h"
#include "setup.h"

/*
 * Display the path(s), one per line, of the packfile(s) containing
 * the given object.
 */

static const char *find_pack_usage = "\n"
"  test-tool find-pack <object>";


int cmd__find_pack(int argc, const char **argv)
{
	struct object_id oid;
	struct packed_git *p;

	setup_git_directory();

	if (argc != 2)
		usage(find_pack_usage);

	if (repo_get_oid(the_repository, argv[1], &oid))
		die("cannot parse %s as an object name", argv[1]);

	for (p = get_all_packs(the_repository); p; p = p->next) {
		if (find_pack_entry_one(oid.hash, p))
			printf("%s\n", p->pack_name);
	}

	return 0;
}
