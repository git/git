#define USE_THE_REPOSITORY_VARIABLE

#include "test-tool.h"
#include "dir.h"
#include "environment.h"
#include "hex.h"
#include "object-name.h"
#include "object.h"
#include "pretty.h"
#include "revision.h"
#include "setup.h"
#include "parse-options.h"
#include "strbuf.h"
#include "path-walk.h"
#include "oid-array.h"

static const char * const path_walk_usage[] = {
	N_("test-tool path-walk <options> -- <revision-options>"),
	NULL
};

struct path_walk_test_data {
	uintmax_t batch_nr;

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

	if (type == OBJ_TREE)
		tdata->tree_nr += oids->nr;
	else if (type == OBJ_BLOB)
		tdata->blob_nr += oids->nr;
	else if (type == OBJ_COMMIT)
		tdata->commit_nr += oids->nr;
	else if (type == OBJ_TAG)
		tdata->tag_nr += oids->nr;
	else
		BUG("we do not understand this type");

	typestr = type_name(type);

	/* This should never be output during tests. */
	if (!oids->nr)
		printf("%"PRIuMAX":%s:%s:EMPTY\n",
		       tdata->batch_nr, typestr, path);

	for (size_t i = 0; i < oids->nr; i++) {
		struct object *o = lookup_unknown_object(the_repository,
							 &oids->oid[i]);
		printf("%"PRIuMAX":%s:%s:%s%s\n",
		       tdata->batch_nr, typestr, path,
		       oid_to_hex(&oids->oid[i]),
		       o->flags & UNINTERESTING ? ":UNINTERESTING" : "");
	}

	tdata->batch_nr++;
	return 0;
}

int cmd__path_walk(int argc, const char **argv)
{
	int res, stdin_pl = 0;
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
		OPT_BOOL(0, "edge-aggressive", &info.edge_aggressive,
			 N_("toggle aggressive edge walk")),
		OPT_BOOL(0, "stdin-pl", &stdin_pl,
			 N_("read a pattern list over stdin")),
		OPT_END(),
	};

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

	if (stdin_pl) {
		struct strbuf in = STRBUF_INIT;
		CALLOC_ARRAY(info.pl, 1);

		info.pl->use_cone_patterns = 1;

		strbuf_fread(&in, 2048, stdin);
		add_patterns_from_buffer(in.buf, in.len, "", 0, info.pl);
		strbuf_release(&in);
	}

	res = walk_objects_by_path(&info);

	printf("commits:%" PRIuMAX "\n"
	       "trees:%" PRIuMAX "\n"
	       "blobs:%" PRIuMAX "\n"
	       "tags:%" PRIuMAX "\n",
	       data.commit_nr, data.tree_nr, data.blob_nr, data.tag_nr);

	if (info.pl) {
		clear_pattern_list(info.pl);
		free(info.pl);
	}

	release_revisions(&revs);
	return res;
}
