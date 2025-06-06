#define USE_THE_REPOSITORY_VARIABLE
#include "builtin.h"
#include "config.h"
#include "gettext.h"
#include "hex.h"
#include "refs/refs-internal.h"
#include "object-name.h"
#include "odb.h"
#include "object.h"
#include "string-list.h"
#include "parse-options.h"

static const char * const show_ref_usage[] = {
	N_("git show-ref [--head] [-d | --dereference]\n"
	   "             [-s | --hash[=<n>]] [--abbrev[=<n>]] [--branches] [--tags]\n"
	   "             [--] [<pattern>...]"),
	N_("git show-ref --verify [-q | --quiet] [-d | --dereference]\n"
	   "             [-s | --hash[=<n>]] [--abbrev[=<n>]]\n"
	   "             [--] [<ref>...]"),
	N_("git show-ref --exclude-existing[=<pattern>]"),
	N_("git show-ref --exists <ref>"),
	NULL
};

struct show_one_options {
	int quiet;
	int hash_only;
	int abbrev;
	int deref_tags;
};

static void show_one(const struct show_one_options *opts,
		     const char *refname, const struct object_id *oid)
{
	const char *hex;
	struct object_id peeled;

	if (!odb_has_object(the_repository->objects, oid,
			    HAS_OBJECT_RECHECK_PACKED | HAS_OBJECT_FETCH_PROMISOR))
		die("git show-ref: bad ref %s (%s)", refname,
		    oid_to_hex(oid));

	if (opts->quiet)
		return;

	hex = repo_find_unique_abbrev(the_repository, oid, opts->abbrev);
	if (opts->hash_only)
		printf("%s\n", hex);
	else
		printf("%s %s\n", hex, refname);

	if (!opts->deref_tags)
		return;

	if (!peel_iterated_oid(the_repository, oid, &peeled)) {
		hex = repo_find_unique_abbrev(the_repository, &peeled, opts->abbrev);
		printf("%s %s^{}\n", hex, refname);
	}
}

struct show_ref_data {
	const struct show_one_options *show_one_opts;
	const char **patterns;
	int found_match;
	int show_head;
};

static int show_ref(const char *refname, const char *referent UNUSED, const struct object_id *oid,
		    int flag UNUSED, void *cbdata)
{
	struct show_ref_data *data = cbdata;

	if (data->show_head && !strcmp(refname, "HEAD"))
		goto match;

	if (data->patterns) {
		int reflen = strlen(refname);
		const char **p = data->patterns, *m;
		while ((m = *p++) != NULL) {
			int len = strlen(m);
			if (len > reflen)
				continue;
			if (memcmp(m, refname + reflen - len, len))
				continue;
			if (len == reflen)
				goto match;
			if (refname[reflen - len - 1] == '/')
				goto match;
		}
		return 0;
	}

match:
	data->found_match++;

	show_one(data->show_one_opts, refname, oid);

	return 0;
}

static int add_existing(const char *refname,
			const char *referent UNUSED,
			const struct object_id *oid UNUSED,
			int flag UNUSED, void *cbdata)
{
	struct string_list *list = (struct string_list *)cbdata;
	string_list_insert(list, refname);
	return 0;
}

struct exclude_existing_options {
	/*
	 * We need an explicit `enabled` field because it is perfectly valid
	 * for `pattern` to be `NULL` even if `--exclude-existing` was given.
	 */
	int enabled;
	const char *pattern;
};

/*
 * read "^(?:<anything>\s)?<refname>(?:\^\{\})?$" from the standard input,
 * and
 * (1) strip "^{}" at the end of line if any;
 * (2) ignore if match is provided and does not head-match refname;
 * (3) warn if refname is not a well-formed refname and skip;
 * (4) ignore if refname is a ref that exists in the local repository;
 * (5) otherwise output the line.
 */
static int cmd_show_ref__exclude_existing(const struct exclude_existing_options *opts)
{
	struct string_list existing_refs = STRING_LIST_INIT_DUP;
	char buf[1024];
	int patternlen = opts->pattern ? strlen(opts->pattern) : 0;

	refs_for_each_ref(get_main_ref_store(the_repository), add_existing,
			  &existing_refs);
	while (fgets(buf, sizeof(buf), stdin)) {
		char *ref;
		int len = strlen(buf);

		if (len > 0 && buf[len - 1] == '\n')
			buf[--len] = '\0';
		if (3 <= len && !strcmp(buf + len - 3, "^{}")) {
			len -= 3;
			buf[len] = '\0';
		}
		for (ref = buf + len; buf < ref; ref--)
			if (isspace(ref[-1]))
				break;
		if (opts->pattern) {
			int reflen = buf + len - ref;
			if (reflen < patternlen)
				continue;
			if (strncmp(ref, opts->pattern, patternlen))
				continue;
		}
		if (check_refname_format(ref, 0)) {
			warning("ref '%s' ignored", ref);
			continue;
		}
		if (!string_list_has_string(&existing_refs, ref)) {
			printf("%s\n", buf);
		}
	}

	string_list_clear(&existing_refs, 0);
	return 0;
}

static int cmd_show_ref__verify(const struct show_one_options *show_one_opts,
				const char **refs)
{
	if (!refs || !*refs)
		die("--verify requires a reference");

	while (*refs) {
		struct object_id oid;

		if ((starts_with(*refs, "refs/") || refname_is_safe(*refs)) &&
		    !refs_read_ref(get_main_ref_store(the_repository), *refs, &oid)) {
			show_one(show_one_opts, *refs, &oid);
		}
		else if (!show_one_opts->quiet)
			die("'%s' - not a valid ref", *refs);
		else
			return 1;
		refs++;
	}

	return 0;
}

struct patterns_options {
	int show_head;
	int branches_only;
	int tags_only;
};

static int cmd_show_ref__patterns(const struct patterns_options *opts,
				  const struct show_one_options *show_one_opts,
				  const char **patterns)
{
	struct show_ref_data show_ref_data = {
		.show_one_opts = show_one_opts,
		.show_head = opts->show_head,
	};

	if (patterns && *patterns)
		show_ref_data.patterns = patterns;

	if (opts->show_head)
		refs_head_ref(get_main_ref_store(the_repository), show_ref,
			      &show_ref_data);
	if (opts->branches_only || opts->tags_only) {
		if (opts->branches_only)
			refs_for_each_fullref_in(get_main_ref_store(the_repository),
						 "refs/heads/", NULL,
						 show_ref, &show_ref_data);
		if (opts->tags_only)
			refs_for_each_fullref_in(get_main_ref_store(the_repository),
						 "refs/tags/", NULL, show_ref,
						 &show_ref_data);
	} else {
		refs_for_each_ref(get_main_ref_store(the_repository),
				  show_ref, &show_ref_data);
	}
	if (!show_ref_data.found_match)
		return 1;

	return 0;
}

static int cmd_show_ref__exists(const char **refs)
{
	struct strbuf unused_referent = STRBUF_INIT;
	struct object_id unused_oid;
	unsigned int unused_type;
	int failure_errno = 0;
	const char *ref;
	int ret = 0;

	if (!refs || !*refs)
		die("--exists requires a reference");
	ref = *refs++;
	if (*refs)
		die("--exists requires exactly one reference");

	if (refs_read_raw_ref(get_main_ref_store(the_repository), ref,
			      &unused_oid, &unused_referent, &unused_type,
			      &failure_errno)) {
		if (failure_errno == ENOENT || failure_errno == EISDIR) {
			error(_("reference does not exist"));
			ret = 2;
		} else {
			errno = failure_errno;
			error_errno(_("failed to look up reference"));
			ret = 1;
		}

		goto out;
	}

out:
	strbuf_release(&unused_referent);
	return ret;
}

static int hash_callback(const struct option *opt, const char *arg, int unset)
{
	struct show_one_options *opts = opt->value;
	struct option abbrev_opt = *opt;

	opts->hash_only = 1;
	/* Use full length SHA1 if no argument */
	if (!arg)
		return 0;

	abbrev_opt.value = &opts->abbrev;
	return parse_opt_abbrev_cb(&abbrev_opt, arg, unset);
}

static int exclude_existing_callback(const struct option *opt, const char *arg,
				     int unset)
{
	struct exclude_existing_options *opts = opt->value;
	BUG_ON_OPT_NEG(unset);
	opts->enabled = 1;
	opts->pattern = arg;
	return 0;
}

int cmd_show_ref(int argc,
const char **argv,
const char *prefix,
struct repository *repo UNUSED)
{
	struct exclude_existing_options exclude_existing_opts = {0};
	struct patterns_options patterns_opts = {0};
	struct show_one_options show_one_opts = {0};
	int verify = 0, exists = 0;
	const struct option show_ref_options[] = {
		OPT_BOOL(0, "tags", &patterns_opts.tags_only, N_("only show tags (can be combined with --branches)")),
		OPT_BOOL(0, "branches", &patterns_opts.branches_only, N_("only show branches (can be combined with --tags)")),
		OPT_HIDDEN_BOOL(0, "heads", &patterns_opts.branches_only,
				N_("deprecated synonym for --branches")),
		OPT_BOOL(0, "exists", &exists, N_("check for reference existence without resolving")),
		OPT_BOOL(0, "verify", &verify, N_("stricter reference checking, "
			    "requires exact ref path")),
		OPT_HIDDEN_BOOL('h', NULL, &patterns_opts.show_head,
				N_("show the HEAD reference, even if it would be filtered out")),
		OPT_BOOL(0, "head", &patterns_opts.show_head,
		  N_("show the HEAD reference, even if it would be filtered out")),
		OPT_BOOL('d', "dereference", &show_one_opts.deref_tags,
			    N_("dereference tags into object IDs")),
		OPT_CALLBACK_F('s', "hash", &show_one_opts, N_("n"),
			       N_("only show SHA1 hash using <n> digits"),
			       PARSE_OPT_OPTARG, &hash_callback),
		OPT__ABBREV(&show_one_opts.abbrev),
		OPT__QUIET(&show_one_opts.quiet,
			   N_("do not print results to stdout (useful with --verify)")),
		OPT_CALLBACK_F(0, "exclude-existing", &exclude_existing_opts,
			       N_("pattern"), N_("show refs from stdin that aren't in local repository"),
			       PARSE_OPT_OPTARG | PARSE_OPT_NONEG, exclude_existing_callback),
		OPT_END()
	};

	git_config(git_default_config, NULL);

	argc = parse_options(argc, argv, prefix, show_ref_options,
			     show_ref_usage, 0);

	die_for_incompatible_opt3(exclude_existing_opts.enabled, "--exclude-existing",
				  verify, "--verify",
				  exists, "--exists");

	if (exclude_existing_opts.enabled)
		return cmd_show_ref__exclude_existing(&exclude_existing_opts);
	else if (verify)
		return cmd_show_ref__verify(&show_one_opts, argv);
	else if (exists)
		return cmd_show_ref__exists(argv);
	else
		return cmd_show_ref__patterns(&patterns_opts, &show_one_opts, argv);
}
