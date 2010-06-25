#include "builtin.h"
#include "cache.h"
#include "refs.h"
#include "object.h"
#include "tag.h"
#include "string-list.h"
#include "parse-options.h"

static const char * const show_ref_usage[] = {
	"git show-ref [-q|--quiet] [--verify] [--head] [-d|--dereference] [-s|--hash[=<n>]] [--abbrev[=<n>]] [--tags] [--heads] [--] [pattern*] ",
	"git show-ref --exclude-existing[=pattern] < ref-list",
	NULL
};

static int deref_tags, show_head, tags_only, heads_only, found_match, verify,
	   quiet, hash_only, abbrev, exclude_arg;
static const char **pattern;
static const char *exclude_existing_arg;

static void show_one(const char *refname, const unsigned char *sha1)
{
	const char *hex = find_unique_abbrev(sha1, abbrev);
	if (hash_only)
		printf("%s\n", hex);
	else
		printf("%s %s\n", hex, refname);
}

static int show_ref(const char *refname, const unsigned char *sha1, int flag, void *cbdata)
{
	struct object *obj;
	const char *hex;
	unsigned char peeled[20];

	if (tags_only || heads_only) {
		int match;

		match = heads_only && !prefixcmp(refname, "refs/heads/");
		match |= tags_only && !prefixcmp(refname, "refs/tags/");
		if (!match)
			return 0;
	}
	if (pattern) {
		int reflen = strlen(refname);
		const char **p = pattern, *m;
		while ((m = *p++) != NULL) {
			int len = strlen(m);
			if (len > reflen)
				continue;
			if (memcmp(m, refname + reflen - len, len))
				continue;
			if (len == reflen)
				goto match;
			/* "--verify" requires an exact match */
			if (verify)
				continue;
			if (refname[reflen - len - 1] == '/')
				goto match;
		}
		return 0;
	}

match:
	found_match++;

	/* This changes the semantics slightly that even under quiet we
	 * detect and return error if the repository is corrupt and
	 * ref points at a nonexistent object.
	 */
	if (!has_sha1_file(sha1))
		die("git show-ref: bad ref %s (%s)", refname,
		    sha1_to_hex(sha1));

	if (quiet)
		return 0;

	show_one(refname, sha1);

	if (!deref_tags)
		return 0;

	if ((flag & REF_ISPACKED) && !peel_ref(refname, peeled)) {
		if (!is_null_sha1(peeled)) {
			hex = find_unique_abbrev(peeled, abbrev);
			printf("%s %s^{}\n", hex, refname);
		}
	}
	else {
		obj = parse_object(sha1);
		if (!obj)
			die("git show-ref: bad ref %s (%s)", refname,
			    sha1_to_hex(sha1));
		if (obj->type == OBJ_TAG) {
			obj = deref_tag(obj, refname, 0);
			if (!obj)
				die("git show-ref: bad tag at ref %s (%s)", refname,
				    sha1_to_hex(sha1));
			hex = find_unique_abbrev(obj->sha1, abbrev);
			printf("%s %s^{}\n", hex, refname);
		}
	}
	return 0;
}

static int add_existing(const char *refname, const unsigned char *sha1, int flag, void *cbdata)
{
	struct string_list *list = (struct string_list *)cbdata;
	string_list_insert(list, refname);
	return 0;
}

/*
 * read "^(?:<anything>\s)?<refname>(?:\^\{\})?$" from the standard input,
 * and
 * (1) strip "^{}" at the end of line if any;
 * (2) ignore if match is provided and does not head-match refname;
 * (3) warn if refname is not a well-formed refname and skip;
 * (4) ignore if refname is a ref that exists in the local repository;
 * (5) otherwise output the line.
 */
static int exclude_existing(const char *match)
{
	static struct string_list existing_refs = { NULL, 0, 0, 0 };
	char buf[1024];
	int matchlen = match ? strlen(match) : 0;

	for_each_ref(add_existing, &existing_refs);
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
		if (match) {
			int reflen = buf + len - ref;
			if (reflen < matchlen)
				continue;
			if (strncmp(ref, match, matchlen))
				continue;
		}
		if (check_ref_format(ref)) {
			warning("ref '%s' ignored", ref);
			continue;
		}
		if (!string_list_has_string(&existing_refs, ref)) {
			printf("%s\n", buf);
		}
	}
	return 0;
}

static int hash_callback(const struct option *opt, const char *arg, int unset)
{
	hash_only = 1;
	/* Use full length SHA1 if no argument */
	if (!arg)
		return 0;
	return parse_opt_abbrev_cb(opt, arg, unset);
}

static int exclude_existing_callback(const struct option *opt, const char *arg,
				     int unset)
{
	exclude_arg = 1;
	*(const char **)opt->value = arg;
	return 0;
}

static int help_callback(const struct option *opt, const char *arg, int unset)
{
	return -1;
}

static const struct option show_ref_options[] = {
	OPT_BOOLEAN(0, "tags", &tags_only, "only show tags (can be combined with heads)"),
	OPT_BOOLEAN(0, "heads", &heads_only, "only show heads (can be combined with tags)"),
	OPT_BOOLEAN(0, "verify", &verify, "stricter reference checking, "
		    "requires exact ref path"),
	{ OPTION_BOOLEAN, 'h', NULL, &show_head, NULL,
	  "show the HEAD reference",
	  PARSE_OPT_NOARG | PARSE_OPT_HIDDEN },
	OPT_BOOLEAN(0, "head", &show_head, "show the HEAD reference"),
	OPT_BOOLEAN('d', "dereference", &deref_tags,
		    "dereference tags into object IDs"),
	{ OPTION_CALLBACK, 's', "hash", &abbrev, "n",
	  "only show SHA1 hash using <n> digits",
	  PARSE_OPT_OPTARG, &hash_callback },
	OPT__ABBREV(&abbrev),
	OPT__QUIET(&quiet),
	{ OPTION_CALLBACK, 0, "exclude-existing", &exclude_existing_arg,
	  "pattern", "show refs from stdin that aren't in local repository",
	  PARSE_OPT_OPTARG | PARSE_OPT_NONEG, exclude_existing_callback },
	{ OPTION_CALLBACK, 0, "help-all", NULL, NULL, "show usage",
	  PARSE_OPT_HIDDEN | PARSE_OPT_NOARG, help_callback },
	OPT_END()
};

int cmd_show_ref(int argc, const char **argv, const char *prefix)
{
	if (argc == 2 && !strcmp(argv[1], "-h"))
		usage_with_options(show_ref_usage, show_ref_options);

	argc = parse_options(argc, argv, prefix, show_ref_options,
			     show_ref_usage, PARSE_OPT_NO_INTERNAL_HELP);

	if (exclude_arg)
		return exclude_existing(exclude_existing_arg);

	pattern = argv;
	if (!*pattern)
		pattern = NULL;

	if (verify) {
		if (!pattern)
			die("--verify requires a reference");
		while (*pattern) {
			unsigned char sha1[20];

			if (!prefixcmp(*pattern, "refs/") &&
			    resolve_ref(*pattern, sha1, 1, NULL)) {
				if (!quiet)
					show_one(*pattern, sha1);
			}
			else if (!quiet)
				die("'%s' - not a valid ref", *pattern);
			else
				return 1;
			pattern++;
		}
		return 0;
	}

	if (show_head)
		head_ref(show_ref, NULL);
	for_each_ref(show_ref, NULL);
	if (!found_match) {
		if (verify && !quiet)
			die("No match");
		return 1;
	}
	return 0;
}
