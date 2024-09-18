#define USE_THE_REPOSITORY_VARIABLE

#include "test-tool.h"
#include "environment.h"
#include "hex.h"
#include "object-name.h"
#include "object.h"
#include "pretty.h"
#include "revision.h"
#include "setup.h"
#include "parse-options.h"
#include "path-walk.h"
#include "oid-array.h"

static const char * const path_walk_usage[] = {
	N_("test-tool path-walk <options> -- <revision-options>"),
	NULL
};

struct path_walk_test_data {
	uintmax_t tree_nr;
	uintmax_t blob_nr;
};

static int emit_block(const char *path, struct oid_array *oids,
		      enum object_type type, void *data)
{
	struct path_walk_test_data *tdata = data;
	const char *typestr;

	switch (type) {
	case OBJ_TREE:
		typestr = "TREE";
		tdata->tree_nr += oids->nr;
		break;

	case OBJ_BLOB:
		typestr = "BLOB";
		tdata->blob_nr += oids->nr;
		break;

	default:
		BUG("we do not understand this type");
	}

	for (size_t i = 0; i < oids->nr; i++)
		printf("%s:%s:%s\n", typestr, path, oid_to_hex(&oids->oid[i]));

	return 0;
}

int cmd__path_walk(int argc, const char **argv)
{
	int res;
	struct rev_info revs = REV_INFO_INIT;
	struct path_walk_info info = PATH_WALK_INFO_INIT;
	struct path_walk_test_data data = { 0 };
	struct option options[] = {
		OPT_END(),
	};

	initialize_repository(the_repository);
	setup_git_directory();
	revs.repo = the_repository;

	argc = parse_options(argc, argv, NULL,
			     options, path_walk_usage,
			     PARSE_OPT_KEEP_UNKNOWN_OPT | PARSE_OPT_KEEP_ARGV0);

	if (argc > 1)
		setup_revisions(argc, argv, &revs, NULL);
	else
		usage(path_walk_usage[0]);

	info.revs = &revs;
	info.path_fn = emit_block;
	info.path_fn_data = &data;

	res = walk_objects_by_path(&info);

	printf("trees:%" PRIuMAX "\n"
	       "blobs:%" PRIuMAX "\n",
	       data.tree_nr, data.blob_nr);

	return res;
}
