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
	uintmax_t commit_nr;
	uintmax_t tree_nr;
	uintmax_t blob_nr;
	uintmax_t tag_nr;
};

static int emit_block(const char *path, struct oid_array *oids,
		      enum object_type type, void *data)
{
	struct path_walk_test_data *tdata = data;
	const char *typestr;

	switch (type) {
	case OBJ_COMMIT:
		typestr = "COMMIT";
		tdata->commit_nr += oids->nr;
		break;

	case OBJ_TREE:
		typestr = "TREE";
		tdata->tree_nr += oids->nr;
		break;

	case OBJ_BLOB:
		typestr = "BLOB";
		tdata->blob_nr += oids->nr;
		break;

	case OBJ_TAG:
		typestr = "TAG";
		tdata->tag_nr += oids->nr;
		break;

	default:
		BUG("we do not understand this type");
	}

	for (size_t i = 0; i < oids->nr; i++) {
		struct object *o = lookup_unknown_object(the_repository,
							 &oids->oid[i]);
		printf("%s:%s:%s%s\n", typestr, path, oid_to_hex(&oids->oid[i]),
		       o->flags & UNINTERESTING ? ":UNINTERESTING" : "");
	}

	return 0;
}

int cmd__path_walk(int argc, const char **argv)
{
	int res;
	struct rev_info revs = REV_INFO_INIT;
	struct path_walk_info info = PATH_WALK_INFO_INIT;
	struct path_walk_test_data data = { 0 };
	struct option options[] = {
		OPT_BOOL(0, "blobs", &info.blobs,
			 N_("toggle inclusion of blob objects")),
		OPT_BOOL(0, "commits", &info.commits,
			 N_("toggle inclusion of commit objects")),
		OPT_BOOL(0, "tags", &info.tags,
			 N_("toggle inclusion of tag objects")),
		OPT_BOOL(0, "trees", &info.trees,
			 N_("toggle inclusion of tree objects")),
		OPT_BOOL(0, "prune", &info.prune_all_uninteresting,
			 N_("toggle pruning of uninteresting paths")),
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

	printf("commits:%" PRIuMAX "\n"
	       "trees:%" PRIuMAX "\n"
	       "blobs:%" PRIuMAX "\n"
	       "tags:%" PRIuMAX "\n",
	       data.commit_nr, data.tree_nr, data.blob_nr, data.tag_nr);

	return res;
}
