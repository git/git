#define USE_THE_REPOSITORY_VARIABLE

#include "test-tool.h"
#include "object-name.h"
#include "odb.h"
#include "packfile.h"
#include "parse-options.h"
#include "setup.h"

/*
 * Display the path(s), one per line, of the packfile(s) containing
 * the given object.
 *
 * If '--check-count <n>' is passed, then error out if the number of
 * packfiles containing the object is not <n>.
 */

static const char *const find_pack_usage[] = {
	"test-tool find-pack [--check-count <n>] <object>",
	NULL
};

int cmd__find_pack(int argc, const char **argv)
{
	struct object_id oid;
	struct packed_git *p;
	int count = -1, actual_count = 0;
	const char *prefix = setup_git_directory();

	struct option options[] = {
		OPT_INTEGER('c', "check-count", &count, "expected number of packs"),
		OPT_END(),
	};

	argc = parse_options(argc, argv, prefix, options, find_pack_usage, 0);
	if (argc != 1)
		usage(find_pack_usage[0]);

	if (repo_get_oid(the_repository, argv[0], &oid))
		die("cannot parse %s as an object name", argv[0]);

	for (p = get_all_packs(the_repository); p; p = p->next)
		if (find_pack_entry_one(&oid, p)) {
			printf("%s\n", p->pack_name);
			actual_count++;
		}

	if (count > -1 && count != actual_count)
		die("bad packfile count %d instead of %d", actual_count, count);

	return 0;
}
