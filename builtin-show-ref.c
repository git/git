#include "cache.h"
#include "refs.h"
#include "object.h"
#include "tag.h"
#include "path-list.h"

static const char show_ref_usage[] = "git show-ref [-q|--quiet] [--verify] [-h|--head] [-d|--dereference] [-s|--hash[=<length>]] [--abbrev[=<length>]] [--tags] [--heads] [--] [pattern*] | --filter-invalid < ref-list";

static int deref_tags = 0, show_head = 0, tags_only = 0, heads_only = 0,
	found_match = 0, verify = 0, quiet = 0, hash_only = 0, abbrev = 0;
static const char **pattern;

static int show_ref(const char *refname, const unsigned char *sha1, int flag, void *cbdata)
{
	struct object *obj;
	const char *hex;
	unsigned char peeled[20];

	if (tags_only || heads_only) {
		int match;

		match = heads_only && !strncmp(refname, "refs/heads/", 11);
		match |= tags_only && !strncmp(refname, "refs/tags/", 10);
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
		die("git-show-ref: bad ref %s (%s)", refname,
		    sha1_to_hex(sha1));

	if (quiet)
		return 0;

	hex = find_unique_abbrev(sha1, abbrev);
	if (hash_only)
		printf("%s\n", hex);
	else
		printf("%s %s\n", hex, refname);

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
			die("git-show-ref: bad ref %s (%s)", refname,
			    sha1_to_hex(sha1));
		if (obj->type == OBJ_TAG) {
			obj = deref_tag(obj, refname, 0);
			hex = find_unique_abbrev(obj->sha1, abbrev);
			printf("%s %s^{}\n", hex, refname);
		}
	}
	return 0;
}

static int add_existing(const char *refname, const unsigned char *sha1, int flag, void *cbdata)
{
	struct path_list *list = (struct path_list *)cbdata;
	path_list_insert(refname, list);
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
	static struct path_list existing_refs = { NULL, 0, 0, 0 };
	char buf[1024];
	int matchlen = match ? strlen(match) : 0;

	for_each_ref(add_existing, &existing_refs);
	while (fgets(buf, sizeof(buf), stdin)) {
		int len = strlen(buf);
		char *ref;
		if (len > 0 && buf[len - 1] == '\n')
			buf[--len] = '\0';
		if (!strcmp(buf + len - 3, "^{}")) {
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
			fprintf(stderr, "warning: ref '%s' ignored\n", ref);
			continue;
		}
		if (!path_list_has_path(&existing_refs, ref)) {
			printf("%s\n", buf);
		}
	}
	return 0;
}

int cmd_show_ref(int argc, const char **argv, const char *prefix)
{
	int i;

	for (i = 1; i < argc; i++) {
		const char *arg = argv[i];
		if (*arg != '-') {
			pattern = argv + i;
			break;
		}
		if (!strcmp(arg, "--")) {
			pattern = argv + i + 1;
			if (!*pattern)
				pattern = NULL;
			break;
		}
		if (!strcmp(arg, "-q") || !strcmp(arg, "--quiet")) {
			quiet = 1;
			continue;
		}
		if (!strcmp(arg, "-h") || !strcmp(arg, "--head")) {
			show_head = 1;
			continue;
		}
		if (!strcmp(arg, "-d") || !strcmp(arg, "--dereference")) {
			deref_tags = 1;
			continue;
		}
		if (!strcmp(arg, "-s") || !strcmp(arg, "--hash")) {
			hash_only = 1;
			continue;
		}
		if (!strncmp(arg, "--hash=", 7) ||
		    (!strncmp(arg, "--abbrev", 8) &&
		     (arg[8] == '=' || arg[8] == '\0'))) {
			if (arg[3] != 'h' && !arg[8])
				/* --abbrev only */
				abbrev = DEFAULT_ABBREV;
			else {
				/* --hash= or --abbrev= */
				char *end;
				if (arg[3] == 'h') {
					hash_only = 1;
					arg += 7;
				}
				else
					arg += 9;
				abbrev = strtoul(arg, &end, 10);
				if (*end || abbrev > 40)
					usage(show_ref_usage);
				if (abbrev < MINIMUM_ABBREV)
					abbrev = MINIMUM_ABBREV;
			}
			continue;
		}
		if (!strcmp(arg, "--verify")) {
			verify = 1;
			continue;
		}
		if (!strcmp(arg, "--tags")) {
			tags_only = 1;
			continue;
		}
		if (!strcmp(arg, "--heads")) {
			heads_only = 1;
			continue;
		}
		if (!strcmp(arg, "--exclude-existing"))
			return exclude_existing(NULL);
		if (!strncmp(arg, "--exclude-existing=", 19))
			return exclude_existing(arg + 19);
		usage(show_ref_usage);
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
