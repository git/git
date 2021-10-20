/*
 * "git walken"
 *
 * Part of the "My First Object Walk" tutorial.
 */
#include "builtin.h"
#include "parse-options.h"
#include "config.h"
#include "revision.h"
#include "list-objects.h"
#include "list-objects-filter-options.h"

static int commit_count;
static int tag_count;
static int blob_count;
static int tree_count;

static void walken_show_commit(struct commit *cmt, void *buf)
{
	commit_count++;
}

static void walken_show_object(struct object *obj, const char *str, void *buf)
{
	switch (obj->type) {
	case OBJ_TREE:
		tree_count++;
		break;
	case OBJ_BLOB:
		blob_count++;
		break;
	case OBJ_TAG:
		tag_count++;
		break;
	case OBJ_COMMIT:
		BUG("unexpected commit object in walken_show_object\n");
	default:
		BUG("unexpected object type %s in walken_show_object\n",
				type_name(obj->type));
	}
}

static void final_rev_info_setup(struct rev_info *rev)
{
//	append_header_grep_pattern(&rev->grep_filter, GREP_HEADER_AUTHOR, "gmail");
//	compile_grep_patterns(&rev->grep_filter);
	get_commit_format("oneline", rev);
	rev->topo_order = 1;
	rev->sort_order = REV_SORT_BY_AUTHOR_DATE;
	rev->reverse = 1;

	add_head_to_pending(rev);
}

static int git_walken_config(const char *var, const char *value, void *cb)
{
	grep_config(var, value, cb);
	return git_default_config(var, value, cb);
}

static void walken_object_walk(struct rev_info *rev)
{
	struct oidset omitted = OIDSET_INIT;
	struct list_objects_filter_options filter_options;

	rev->tree_objects = 1;
	rev->blob_objects = 1;
	rev->tag_objects = 1;
	rev->tree_blobs_in_commit_order = 1;

	if (prepare_revision_walk(rev))
		die(_("revision walk setup failed"));

	struct oidset_iter oit;
	struct object_id *oid = NULL;
	int omitted_count = 0;
	oidset_init(&omitted, 0);

	commit_count = 0;
	tag_count = 0;
	blob_count = 0;
	tree_count = 0;

	if (0) {
		trace_printf(_("Unfiltered object walk.\n"));
		traverse_commit_list(rev, walken_show_commit, walken_show_object, NULL);
	} else {
		trace_printf(
			_("Filtered object walk with filterspec 'tree:1'.\n"));

		puts("about to parse list objects filter");
		parse_list_objects_filter(&filter_options, "blob:none");
		puts("finished parsing list objects filter");

		puts("about to do the traversal");
		traverse_commit_list_filtered(&filter_options, rev,
				walken_show_commit, walken_show_object, NULL, &omitted);
	}

	oidset_iter_init(&omitted, &oit);

	while ((oid = oidset_iter_next(&oit)))
		omitted_count++;

	printf("commits %d\nblobs %d\ntags: %d\ntrees %d\nomitted %d\n", commit_count, blob_count, tag_count, tree_count, omitted_count);
//	printf("commits %d\nblobs %d\ntags: %d\ntrees %d\n", commit_count, blob_count, tag_count, tree_count);
}

static void walken_commit_walk(struct rev_info *rev)
{
	struct commit *commit;
	struct strbuf prettybuf = STRBUF_INIT;

	if (prepare_revision_walk(rev))
		die(_("revision walk setup failed"));

	while ((commit = get_revision(rev))) {
		strbuf_reset(&prettybuf);
		pp_commit_easy(CMIT_FMT_ONELINE, commit, &prettybuf);
		puts(prettybuf.buf);
	}

	strbuf_release(&prettybuf);
}

int cmd_walken(int argc, const char **argv, const char *prefix)
{
	static const char * const walken_usage[] = {
		N_("git walken"),
		NULL,
	};

	struct option options[] = {
		OPT_END()
	};

	struct rev_info rev;

	argc = parse_options(argc, argv, prefix, options, walken_usage, 0);

	git_config(git_walken_config, NULL);

	repo_init_revisions(the_repository, &rev, prefix);

	if (1) {
		add_head_to_pending(&rev);
		walken_object_walk(&rev);
	} else {
		final_rev_info_setup(&rev);
		walken_commit_walk(&rev);
	}


	trace_printf(_("cmd_walken incoming...\n"));

	return 0;
}

