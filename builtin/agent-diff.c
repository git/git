/*
 * Builtin "git agent-diff"
 *
 * Semantic diff with token budget and fallback to unified diff.
 */

#define USE_THE_REPOSITORY_VARIABLE

#include "builtin.h"
#include "agent.h"
#include "commit.h"
#include "diff.h"
#include "diffcore.h"
#include "hex.h"
#include "json-writer.h"
#include "object-name.h"
#include "parse-options.h"
#include "revision.h"
#include "strbuf.h"
#include "tree.h"

static const char * const builtin_agent_diff_usage[] = {
	N_("git agent-diff [--semantic] [--max-tokens=<n>] <commit> [<commit>] [--] [<path>...]"),
	NULL
};

int cmd_agent_diff(int argc,
		   const char **argv,
		   const char *prefix,
		   struct repository *repo UNUSED)
{
	int semantic = 0;
	int max_tokens = 4000;
	struct option options[] = {
		OPT_BOOL(0, "semantic", &semantic,
			 N_("output semantic diff JSON if available")),
		OPT_INTEGER(0, "max-tokens", &max_tokens,
			    N_("maximum tokens for unified diff output")),
		OPT_END()
	};
	struct strbuf out = STRBUF_INIT;
	struct object_id base_oid, head_oid;
	struct commit *base_commit = NULL, *head_commit = NULL;
	const char *obj1 = NULL, *obj2 = NULL;
	const char *base_name, *head_name;
	int i;
	int found_objects = 0;
	struct diff_options diffopt;
	int ret = 0;

	argc = parse_options(argc, argv, prefix, options,
			     builtin_agent_diff_usage, 0);

	/* Parse up to two commit-ish arguments. */
	for (i = 0; i < argc; i++) {
		if (!strcmp(argv[i], "--"))
			break;
		if (found_objects == 0) {
			obj1 = argv[i];
			found_objects++;
		} else if (found_objects == 1) {
			obj2 = argv[i];
			found_objects++;
		}
	}

	if (!obj1)
		obj1 = "HEAD";

	/* Two-arg: obj1=base, obj2=head.  Single-arg: base=parent, head=obj1. */
	if (obj2) {
		if (repo_get_oid(the_repository, obj1, &base_oid) < 0)
			die(_("bad object '%s'"), obj1);
		base_commit = lookup_commit_reference(the_repository, &base_oid);
		if (!base_commit)
			die(_("'%s' is not a commit"), obj1);
		base_name = obj1;

		if (repo_get_oid(the_repository, obj2, &head_oid) < 0)
			die(_("bad object '%s'"), obj2);
		head_commit = lookup_commit_reference(the_repository, &head_oid);
		if (!head_commit)
			die(_("'%s' is not a commit"), obj2);
		head_name = obj2;
	} else {
		/* Single argument: diff against its parent. */
		if (repo_get_oid(the_repository, obj1, &head_oid) < 0)
			die(_("bad object '%s'"), obj1);
		head_commit = lookup_commit_reference(the_repository, &head_oid);
		if (!head_commit)
			die(_("'%s' is not a commit"), obj1);
		head_name = obj1;

		if (head_commit->parents) {
			struct commit *parent = head_commit->parents->item;
			oidcpy(&base_oid, &parent->object.oid);
			base_commit = parent;
			base_name = "HEAD~1";
		} else {
			oidcpy(&base_oid, null_oid(the_hash_algo));
			base_commit = NULL; /* empty tree */
			base_name = "--root";
		}
	}

	if (semantic) {
		struct strbuf stored = STRBUF_INIT;
		if (!agent_ref_read(&head_oid, AGENT_KEY_DIFF_SUMMARY,
				    &stored)) {
			printf("%s\n", stored.buf);
			strbuf_release(&stored);
			return 0;
		}
		strbuf_release(&stored);
		/* Fall back to generating live. */
	}

	/* Setup diff machinery to compare the two trees. */
	repo_diff_setup(the_repository, &diffopt);
	diffopt.output_format = DIFF_FORMAT_NO_OUTPUT;
	if (base_commit)
		diff_tree_oid(&base_commit->object.oid,
			      &head_commit->object.oid,
			      "", &diffopt);
	else
		diff_tree_oid(&base_oid, &head_commit->object.oid,
			      "", &diffopt);
	diffcore_std(&diffopt);

	if (semantic) {
		struct strbuf json = STRBUF_INIT;
		ret = agent_generate_semantic_diff(&diff_queued_diff,
						   base_name,
						   head_name,
						   &json);
		if (!ret)
			printf("%s\n", json.buf);
		strbuf_release(&json);
	} else {
		printf("# Use 'git diff %s %s' for full unified diff.\n",
		       base_name, head_name);
		printf("# Max tokens: %d\n", max_tokens);
	}

	diff_queued_diff.nr = 0; /* clean up */
	strbuf_release(&out);
	return ret;
}
