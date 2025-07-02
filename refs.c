/*
 * The backend-independent part of the reference module.
 */

#define USE_THE_REPOSITORY_VARIABLE
#define DISABLE_SIGN_COMPARE_WARNINGS

#include "git-compat-util.h"
#include "advice.h"
#include "config.h"
#include "environment.h"
#include "strmap.h"
#include "gettext.h"
#include "hex.h"
#include "lockfile.h"
#include "iterator.h"
#include "refs.h"
#include "refs/refs-internal.h"
#include "run-command.h"
#include "hook.h"
#include "object-name.h"
#include "odb.h"
#include "object.h"
#include "path.h"
#include "submodule.h"
#include "worktree.h"
#include "strvec.h"
#include "repo-settings.h"
#include "setup.h"
#include "sigchain.h"
#include "date.h"
#include "commit.h"
#include "wildmatch.h"
#include "ident.h"

/*
 * List of all available backends
 */
static const struct ref_storage_be *refs_backends[] = {
	[REF_STORAGE_FORMAT_FILES] = &refs_be_files,
	[REF_STORAGE_FORMAT_REFTABLE] = &refs_be_reftable,
};

static const struct ref_storage_be *find_ref_storage_backend(
	enum ref_storage_format ref_storage_format)
{
	if (ref_storage_format < ARRAY_SIZE(refs_backends))
		return refs_backends[ref_storage_format];
	return NULL;
}

enum ref_storage_format ref_storage_format_by_name(const char *name)
{
	for (unsigned int i = 0; i < ARRAY_SIZE(refs_backends); i++)
		if (refs_backends[i] && !strcmp(refs_backends[i]->name, name))
			return i;
	return REF_STORAGE_FORMAT_UNKNOWN;
}

const char *ref_storage_format_to_name(enum ref_storage_format ref_storage_format)
{
	const struct ref_storage_be *be = find_ref_storage_backend(ref_storage_format);
	if (!be)
		return "unknown";
	return be->name;
}

/*
 * How to handle various characters in refnames:
 * 0: An acceptable character for refs
 * 1: End-of-component
 * 2: ., look for a preceding . to reject .. in refs
 * 3: {, look for a preceding @ to reject @{ in refs
 * 4: A bad character: ASCII control characters, and
 *    ":", "?", "[", "\", "^", "~", SP, or TAB
 * 5: *, reject unless REFNAME_REFSPEC_PATTERN is set
 */
static unsigned char refname_disposition[256] = {
	1, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
	4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
	4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 5, 0, 0, 0, 2, 1,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4, 0, 0, 0, 0, 4,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4, 4, 0, 4, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3, 0, 0, 4, 4
};

struct ref_namespace_info ref_namespace[] = {
	[NAMESPACE_HEAD] = {
		.ref = "HEAD",
		.decoration = DECORATION_REF_HEAD,
		.exact = 1,
	},
	[NAMESPACE_BRANCHES] = {
		.ref = "refs/heads/",
		.decoration = DECORATION_REF_LOCAL,
	},
	[NAMESPACE_TAGS] = {
		.ref = "refs/tags/",
		.decoration = DECORATION_REF_TAG,
	},
	[NAMESPACE_REMOTE_REFS] = {
		/*
		 * The default refspec for new remotes copies refs from
		 * refs/heads/ on the remote into refs/remotes/<remote>/.
		 * As such, "refs/remotes/" has special handling.
		 */
		.ref = "refs/remotes/",
		.decoration = DECORATION_REF_REMOTE,
	},
	[NAMESPACE_STASH] = {
		/*
		 * The single ref "refs/stash" stores the latest stash.
		 * Older stashes can be found in the reflog.
		 */
		.ref = "refs/stash",
		.exact = 1,
		.decoration = DECORATION_REF_STASH,
	},
	[NAMESPACE_REPLACE] = {
		/*
		 * This namespace allows Git to act as if one object ID
		 * points to the content of another. Unlike the other
		 * ref namespaces, this one can be changed by the
		 * GIT_REPLACE_REF_BASE environment variable. This
		 * .namespace value will be overwritten in setup_git_env().
		 */
		.ref = "refs/replace/",
		.decoration = DECORATION_GRAFTED,
	},
	[NAMESPACE_NOTES] = {
		/*
		 * The refs/notes/commit ref points to the tip of a
		 * parallel commit history that adds metadata to commits
		 * in the normal history. This ref can be overwritten
		 * by the core.notesRef config variable or the
		 * GIT_NOTES_REFS environment variable.
		 */
		.ref = "refs/notes/commit",
		.exact = 1,
	},
	[NAMESPACE_PREFETCH] = {
		/*
		 * Prefetch refs are written by the background 'fetch'
		 * maintenance task. It allows faster foreground fetches
		 * by advertising these previously-downloaded tips without
		 * updating refs/remotes/ without user intervention.
		 */
		.ref = "refs/prefetch/",
	},
	[NAMESPACE_REWRITTEN] = {
		/*
		 * Rewritten refs are used by the 'label' command in the
		 * sequencer. These are particularly useful during an
		 * interactive rebase that uses the 'merge' command.
		 */
		.ref = "refs/rewritten/",
	},
};

void update_ref_namespace(enum ref_namespace namespace, char *ref)
{
	struct ref_namespace_info *info = &ref_namespace[namespace];
	if (info->ref_updated)
		free((char *)info->ref);
	info->ref = ref;
	info->ref_updated = 1;
}

/*
 * Try to read one refname component from the front of refname.
 * Return the length of the component found, or -1 if the component is
 * not legal.  It is legal if it is something reasonable to have under
 * ".git/refs/"; We do not like it if:
 *
 * - it begins with ".", or
 * - it has double dots "..", or
 * - it has ASCII control characters, or
 * - it has ":", "?", "[", "\", "^", "~", SP, or TAB anywhere, or
 * - it has "*" anywhere unless REFNAME_REFSPEC_PATTERN is set, or
 * - it ends with a "/", or
 * - it ends with ".lock", or
 * - it contains a "@{" portion
 *
 * When sanitized is not NULL, instead of rejecting the input refname
 * as an error, try to come up with a usable replacement for the input
 * refname in it.
 */
static int check_refname_component(const char *refname, int *flags,
				   struct strbuf *sanitized)
{
	const char *cp;
	char last = '\0';
	size_t component_start = 0; /* garbage - not a reasonable initial value */

	if (sanitized)
		component_start = sanitized->len;

	for (cp = refname; ; cp++) {
		int ch = *cp & 255;
		unsigned char disp = refname_disposition[ch];

		if (sanitized && disp != 1)
			strbuf_addch(sanitized, ch);

		switch (disp) {
		case 1:
			goto out;
		case 2:
			if (last == '.') { /* Refname contains "..". */
				if (sanitized)
					/* collapse ".." to single "." */
					strbuf_setlen(sanitized, sanitized->len - 1);
				else
					return -1;
			}
			break;
		case 3:
			if (last == '@') { /* Refname contains "@{". */
				if (sanitized)
					sanitized->buf[sanitized->len-1] = '-';
				else
					return -1;
			}
			break;
		case 4:
			/* forbidden char */
			if (sanitized)
				sanitized->buf[sanitized->len-1] = '-';
			else
				return -1;
			break;
		case 5:
			if (!(*flags & REFNAME_REFSPEC_PATTERN)) {
				/* refspec can't be a pattern */
				if (sanitized)
					sanitized->buf[sanitized->len-1] = '-';
				else
					return -1;
			}

			/*
			 * Unset the pattern flag so that we only accept
			 * a single asterisk for one side of refspec.
			 */
			*flags &= ~ REFNAME_REFSPEC_PATTERN;
			break;
		}
		last = ch;
	}
out:
	if (cp == refname)
		return 0; /* Component has zero length. */

	if (refname[0] == '.') { /* Component starts with '.'. */
		if (sanitized)
			sanitized->buf[component_start] = '-';
		else
			return -1;
	}
	if (cp - refname >= LOCK_SUFFIX_LEN &&
	    !memcmp(cp - LOCK_SUFFIX_LEN, LOCK_SUFFIX, LOCK_SUFFIX_LEN)) {
		if (!sanitized)
			return -1;
		/* Refname ends with ".lock". */
		while (strbuf_strip_suffix(sanitized, LOCK_SUFFIX)) {
			/* try again in case we have .lock.lock */
		}
	}
	return cp - refname;
}

static int check_or_sanitize_refname(const char *refname, int flags,
				     struct strbuf *sanitized)
{
	int component_len, component_count = 0;

	if (!strcmp(refname, "@")) {
		/* Refname is a single character '@'. */
		if (sanitized)
			strbuf_addch(sanitized, '-');
		else
			return -1;
	}

	while (1) {
		if (sanitized && sanitized->len)
			strbuf_complete(sanitized, '/');

		/* We are at the start of a path component. */
		component_len = check_refname_component(refname, &flags,
							sanitized);
		if (sanitized && component_len == 0)
			; /* OK, omit empty component */
		else if (component_len <= 0)
			return -1;

		component_count++;
		if (refname[component_len] == '\0')
			break;
		/* Skip to next component. */
		refname += component_len + 1;
	}

	if (refname[component_len - 1] == '.') {
		/* Refname ends with '.'. */
		if (sanitized)
			; /* omit ending dot */
		else
			return -1;
	}
	if (!(flags & REFNAME_ALLOW_ONELEVEL) && component_count < 2)
		return -1; /* Refname has only one component. */
	return 0;
}

int check_refname_format(const char *refname, int flags)
{
	return check_or_sanitize_refname(refname, flags, NULL);
}

int refs_fsck(struct ref_store *refs, struct fsck_options *o,
	      struct worktree *wt)
{
	return refs->be->fsck(refs, o, wt);
}

void sanitize_refname_component(const char *refname, struct strbuf *out)
{
	if (check_or_sanitize_refname(refname, REFNAME_ALLOW_ONELEVEL, out))
		BUG("sanitizing refname '%s' check returned error", refname);
}

int refname_is_safe(const char *refname)
{
	const char *rest;

	if (skip_prefix(refname, "refs/", &rest)) {
		char *buf;
		int result;
		size_t restlen = strlen(rest);

		/* rest must not be empty, or start or end with "/" */
		if (!restlen || *rest == '/' || rest[restlen - 1] == '/')
			return 0;

		/*
		 * Does the refname try to escape refs/?
		 * For example: refs/foo/../bar is safe but refs/foo/../../bar
		 * is not.
		 */
		buf = xmallocz(restlen);
		result = !normalize_path_copy(buf, rest) && !strcmp(buf, rest);
		free(buf);
		return result;
	}

	do {
		if (!isupper(*refname) && *refname != '_')
			return 0;
		refname++;
	} while (*refname);
	return 1;
}

/*
 * Return true if refname, which has the specified oid and flags, can
 * be resolved to an object in the database. If the referred-to object
 * does not exist, emit a warning and return false.
 */
int ref_resolves_to_object(const char *refname,
			   struct repository *repo,
			   const struct object_id *oid,
			   unsigned int flags)
{
	if (flags & REF_ISBROKEN)
		return 0;
	if (!odb_has_object(repo->objects, oid,
			    HAS_OBJECT_RECHECK_PACKED | HAS_OBJECT_FETCH_PROMISOR)) {
		error(_("%s does not point to a valid object!"), refname);
		return 0;
	}
	return 1;
}

char *refs_resolve_refdup(struct ref_store *refs,
			  const char *refname, int resolve_flags,
			  struct object_id *oid, int *flags)
{
	const char *result;

	result = refs_resolve_ref_unsafe(refs, refname, resolve_flags,
					 oid, flags);
	return xstrdup_or_null(result);
}

/* The argument to for_each_filter_refs */
struct for_each_ref_filter {
	const char *pattern;
	const char *prefix;
	each_ref_fn *fn;
	void *cb_data;
};

int refs_read_ref_full(struct ref_store *refs, const char *refname,
		       int resolve_flags, struct object_id *oid, int *flags)
{
	if (refs_resolve_ref_unsafe(refs, refname, resolve_flags,
				    oid, flags))
		return 0;
	return -1;
}

int refs_read_ref(struct ref_store *refs, const char *refname, struct object_id *oid)
{
	return refs_read_ref_full(refs, refname, RESOLVE_REF_READING, oid, NULL);
}

int refs_ref_exists(struct ref_store *refs, const char *refname)
{
	return !!refs_resolve_ref_unsafe(refs, refname, RESOLVE_REF_READING,
					 NULL, NULL);
}

static int for_each_filter_refs(const char *refname, const char *referent,
				const struct object_id *oid,
				int flags, void *data)
{
	struct for_each_ref_filter *filter = data;

	if (wildmatch(filter->pattern, refname, 0))
		return 0;
	if (filter->prefix)
		skip_prefix(refname, filter->prefix, &refname);
	return filter->fn(refname, referent, oid, flags, filter->cb_data);
}

struct warn_if_dangling_data {
	struct ref_store *refs;
	FILE *fp;
	const char *refname;
	const struct string_list *refnames;
	const char *msg_fmt;
};

static int warn_if_dangling_symref(const char *refname, const char *referent UNUSED,
				   const struct object_id *oid UNUSED,
				   int flags, void *cb_data)
{
	struct warn_if_dangling_data *d = cb_data;
	const char *resolves_to;

	if (!(flags & REF_ISSYMREF))
		return 0;

	resolves_to = refs_resolve_ref_unsafe(d->refs, refname, 0, NULL, NULL);
	if (!resolves_to
	    || (d->refname
		? strcmp(resolves_to, d->refname)
		: !string_list_has_string(d->refnames, resolves_to))) {
		return 0;
	}

	fprintf(d->fp, d->msg_fmt, refname);
	fputc('\n', d->fp);
	return 0;
}

void refs_warn_dangling_symref(struct ref_store *refs, FILE *fp,
			       const char *msg_fmt, const char *refname)
{
	struct warn_if_dangling_data data = {
		.refs = refs,
		.fp = fp,
		.refname = refname,
		.msg_fmt = msg_fmt,
	};
	refs_for_each_rawref(refs, warn_if_dangling_symref, &data);
}

void refs_warn_dangling_symrefs(struct ref_store *refs, FILE *fp,
				const char *msg_fmt, const struct string_list *refnames)
{
	struct warn_if_dangling_data data = {
		.refs = refs,
		.fp = fp,
		.refnames = refnames,
		.msg_fmt = msg_fmt,
	};
	refs_for_each_rawref(refs, warn_if_dangling_symref, &data);
}

int refs_for_each_tag_ref(struct ref_store *refs, each_ref_fn fn, void *cb_data)
{
	return refs_for_each_ref_in(refs, "refs/tags/", fn, cb_data);
}

int refs_for_each_branch_ref(struct ref_store *refs, each_ref_fn fn, void *cb_data)
{
	return refs_for_each_ref_in(refs, "refs/heads/", fn, cb_data);
}

int refs_for_each_remote_ref(struct ref_store *refs, each_ref_fn fn, void *cb_data)
{
	return refs_for_each_ref_in(refs, "refs/remotes/", fn, cb_data);
}

int refs_head_ref_namespaced(struct ref_store *refs, each_ref_fn fn, void *cb_data)
{
	struct strbuf buf = STRBUF_INIT;
	int ret = 0;
	struct object_id oid;
	int flag;

	strbuf_addf(&buf, "%sHEAD", get_git_namespace());
	if (!refs_read_ref_full(refs, buf.buf, RESOLVE_REF_READING, &oid, &flag))
		ret = fn(buf.buf, NULL, &oid, flag, cb_data);
	strbuf_release(&buf);

	return ret;
}

void normalize_glob_ref(struct string_list_item *item, const char *prefix,
			const char *pattern)
{
	struct strbuf normalized_pattern = STRBUF_INIT;

	if (*pattern == '/')
		BUG("pattern must not start with '/'");

	if (prefix)
		strbuf_addstr(&normalized_pattern, prefix);
	else if (!starts_with(pattern, "refs/") &&
		   strcmp(pattern, "HEAD"))
		strbuf_addstr(&normalized_pattern, "refs/");
	/*
	 * NEEDSWORK: Special case other symrefs such as REBASE_HEAD,
	 * MERGE_HEAD, etc.
	 */

	strbuf_addstr(&normalized_pattern, pattern);
	strbuf_strip_suffix(&normalized_pattern, "/");

	item->string = strbuf_detach(&normalized_pattern, NULL);
	item->util = has_glob_specials(pattern) ? NULL : item->string;
	strbuf_release(&normalized_pattern);
}

int refs_for_each_glob_ref_in(struct ref_store *refs, each_ref_fn fn,
			      const char *pattern, const char *prefix, void *cb_data)
{
	struct strbuf real_pattern = STRBUF_INIT;
	struct for_each_ref_filter filter;
	int ret;

	if (!prefix && !starts_with(pattern, "refs/"))
		strbuf_addstr(&real_pattern, "refs/");
	else if (prefix)
		strbuf_addstr(&real_pattern, prefix);
	strbuf_addstr(&real_pattern, pattern);

	if (!has_glob_specials(pattern)) {
		/* Append implied '/' '*' if not present. */
		strbuf_complete(&real_pattern, '/');
		/* No need to check for '*', there is none. */
		strbuf_addch(&real_pattern, '*');
	}

	filter.pattern = real_pattern.buf;
	filter.prefix = prefix;
	filter.fn = fn;
	filter.cb_data = cb_data;
	ret = refs_for_each_ref(refs, for_each_filter_refs, &filter);

	strbuf_release(&real_pattern);
	return ret;
}

int refs_for_each_glob_ref(struct ref_store *refs, each_ref_fn fn,
			   const char *pattern, void *cb_data)
{
	return refs_for_each_glob_ref_in(refs, fn, pattern, NULL, cb_data);
}

const char *prettify_refname(const char *name)
{
	if (skip_prefix(name, "refs/heads/", &name) ||
	    skip_prefix(name, "refs/tags/", &name) ||
	    skip_prefix(name, "refs/remotes/", &name))
		; /* nothing */
	return name;
}

static const char *ref_rev_parse_rules[] = {
	"%.*s",
	"refs/%.*s",
	"refs/tags/%.*s",
	"refs/heads/%.*s",
	"refs/remotes/%.*s",
	"refs/remotes/%.*s/HEAD",
	NULL
};

#define NUM_REV_PARSE_RULES (ARRAY_SIZE(ref_rev_parse_rules) - 1)

/*
 * Is it possible that the caller meant full_name with abbrev_name?
 * If so return a non-zero value to signal "yes"; the magnitude of
 * the returned value gives the precedence used for disambiguation.
 *
 * If abbrev_name cannot mean full_name, return 0.
 */
int refname_match(const char *abbrev_name, const char *full_name)
{
	const char **p;
	const int abbrev_name_len = strlen(abbrev_name);
	const int num_rules = NUM_REV_PARSE_RULES;

	for (p = ref_rev_parse_rules; *p; p++)
		if (!strcmp(full_name, mkpath(*p, abbrev_name_len, abbrev_name)))
			return &ref_rev_parse_rules[num_rules] - p;

	return 0;
}

/*
 * Given a 'prefix' expand it by the rules in 'ref_rev_parse_rules' and add
 * the results to 'prefixes'
 */
void expand_ref_prefix(struct strvec *prefixes, const char *prefix)
{
	const char **p;
	int len = strlen(prefix);

	for (p = ref_rev_parse_rules; *p; p++)
		strvec_pushf(prefixes, *p, len, prefix);
}

static const char default_branch_name_advice[] = N_(
"Using '%s' as the name for the initial branch. This default branch name\n"
"is subject to change. To configure the initial branch name to use in all\n"
"of your new repositories, which will suppress this warning, call:\n"
"\n"
"\tgit config --global init.defaultBranch <name>\n"
"\n"
"Names commonly chosen instead of 'master' are 'main', 'trunk' and\n"
"'development'. The just-created branch can be renamed via this command:\n"
"\n"
"\tgit branch -m <name>\n"
);

char *repo_default_branch_name(struct repository *r, int quiet)
{
	const char *config_key = "init.defaultbranch";
	const char *config_display_key = "init.defaultBranch";
	char *ret = NULL, *full_ref;
	const char *env = getenv("GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME");

	if (env && *env)
		ret = xstrdup(env);
	else if (repo_config_get_string(r, config_key, &ret) < 0)
		die(_("could not retrieve `%s`"), config_display_key);

	if (!ret) {
		ret = xstrdup("master");
		if (!quiet)
			advise_if_enabled(ADVICE_DEFAULT_BRANCH_NAME,
					  _(default_branch_name_advice), ret);
	}

	full_ref = xstrfmt("refs/heads/%s", ret);
	if (check_refname_format(full_ref, 0))
		die(_("invalid branch name: %s = %s"), config_display_key, ret);
	free(full_ref);

	return ret;
}

/*
 * *string and *len will only be substituted, and *string returned (for
 * later free()ing) if the string passed in is a magic short-hand form
 * to name a branch.
 */
static char *substitute_branch_name(struct repository *r,
				    const char **string, int *len,
				    int nonfatal_dangling_mark)
{
	struct strbuf buf = STRBUF_INIT;
	struct interpret_branch_name_options options = {
		.nonfatal_dangling_mark = nonfatal_dangling_mark
	};
	int ret = repo_interpret_branch_name(r, *string, *len, &buf, &options);

	if (ret == *len) {
		size_t size;
		*string = strbuf_detach(&buf, &size);
		*len = size;
		return (char *)*string;
	}

	return NULL;
}

void copy_branchname(struct strbuf *sb, const char *name, unsigned allowed)
{
	int len = strlen(name);
	struct interpret_branch_name_options options = {
		.allowed = allowed
	};
	int used = repo_interpret_branch_name(the_repository, name, len, sb,
					      &options);

	if (used < 0)
		used = 0;
	strbuf_add(sb, name + used, len - used);
}

int check_branch_ref(struct strbuf *sb, const char *name)
{
	if (startup_info->have_repository)
		copy_branchname(sb, name, INTERPRET_BRANCH_LOCAL);
	else
		strbuf_addstr(sb, name);

	/*
	 * This splice must be done even if we end up rejecting the
	 * name; builtin/branch.c::copy_or_rename_branch() still wants
	 * to see what the name expanded to so that "branch -m" can be
	 * used as a tool to correct earlier mistakes.
	 */
	strbuf_splice(sb, 0, 0, "refs/heads/", 11);

	if (*name == '-' ||
	    !strcmp(sb->buf, "refs/heads/HEAD"))
		return -1;

	return check_refname_format(sb->buf, 0);
}

int check_tag_ref(struct strbuf *sb, const char *name)
{
	if (name[0] == '-' || !strcmp(name, "HEAD"))
		return -1;

	strbuf_reset(sb);
	strbuf_addf(sb, "refs/tags/%s", name);

	return check_refname_format(sb->buf, 0);
}

int repo_dwim_ref(struct repository *r, const char *str, int len,
		  struct object_id *oid, char **ref, int nonfatal_dangling_mark)
{
	char *last_branch = substitute_branch_name(r, &str, &len,
						   nonfatal_dangling_mark);
	int   refs_found  = expand_ref(r, str, len, oid, ref);
	free(last_branch);
	return refs_found;
}

int expand_ref(struct repository *repo, const char *str, int len,
	       struct object_id *oid, char **ref)
{
	const char **p, *r;
	int refs_found = 0;
	struct strbuf fullref = STRBUF_INIT;

	*ref = NULL;
	for (p = ref_rev_parse_rules; *p; p++) {
		struct object_id oid_from_ref;
		struct object_id *this_result;
		int flag;
		struct ref_store *refs = get_main_ref_store(repo);

		this_result = refs_found ? &oid_from_ref : oid;
		strbuf_reset(&fullref);
		strbuf_addf(&fullref, *p, len, str);
		r = refs_resolve_ref_unsafe(refs, fullref.buf,
					    RESOLVE_REF_READING,
					    this_result, &flag);
		if (r) {
			if (!refs_found++)
				*ref = xstrdup(r);
			if (!repo_settings_get_warn_ambiguous_refs(repo))
				break;
		} else if ((flag & REF_ISSYMREF) && strcmp(fullref.buf, "HEAD")) {
			warning(_("ignoring dangling symref %s"), fullref.buf);
		} else if ((flag & REF_ISBROKEN) && strchr(fullref.buf, '/')) {
			warning(_("ignoring broken ref %s"), fullref.buf);
		}
	}
	strbuf_release(&fullref);
	return refs_found;
}

int repo_dwim_log(struct repository *r, const char *str, int len,
		  struct object_id *oid, char **log)
{
	struct ref_store *refs = get_main_ref_store(r);
	char *last_branch = substitute_branch_name(r, &str, &len, 0);
	const char **p;
	int logs_found = 0;
	struct strbuf path = STRBUF_INIT;

	*log = NULL;
	for (p = ref_rev_parse_rules; *p; p++) {
		struct object_id hash;
		const char *ref, *it;

		strbuf_reset(&path);
		strbuf_addf(&path, *p, len, str);
		ref = refs_resolve_ref_unsafe(refs, path.buf,
					      RESOLVE_REF_READING,
					      oid ? &hash : NULL, NULL);
		if (!ref)
			continue;
		if (refs_reflog_exists(refs, path.buf))
			it = path.buf;
		else if (strcmp(ref, path.buf) &&
			 refs_reflog_exists(refs, ref))
			it = ref;
		else
			continue;
		if (!logs_found++) {
			*log = xstrdup(it);
			if (oid)
				oidcpy(oid, &hash);
		}
		if (!repo_settings_get_warn_ambiguous_refs(r))
			break;
	}
	strbuf_release(&path);
	free(last_branch);
	return logs_found;
}

int is_per_worktree_ref(const char *refname)
{
	return starts_with(refname, "refs/worktree/") ||
	       starts_with(refname, "refs/bisect/") ||
	       starts_with(refname, "refs/rewritten/");
}

int is_pseudo_ref(const char *refname)
{
	static const char * const pseudo_refs[] = {
		"FETCH_HEAD",
		"MERGE_HEAD",
	};
	size_t i;

	for (i = 0; i < ARRAY_SIZE(pseudo_refs); i++)
		if (!strcmp(refname, pseudo_refs[i]))
			return 1;

	return 0;
}

static int is_root_ref_syntax(const char *refname)
{
	const char *c;

	for (c = refname; *c; c++) {
		if (!isupper(*c) && *c != '-' && *c != '_')
			return 0;
	}

	return 1;
}

int is_root_ref(const char *refname)
{
	static const char *const irregular_root_refs[] = {
		"HEAD",
		"AUTO_MERGE",
		"BISECT_EXPECTED_REV",
		"NOTES_MERGE_PARTIAL",
		"NOTES_MERGE_REF",
		"MERGE_AUTOSTASH",
	};
	size_t i;

	if (!is_root_ref_syntax(refname) ||
	    is_pseudo_ref(refname))
		return 0;

	if (ends_with(refname, "_HEAD"))
		return 1;

	for (i = 0; i < ARRAY_SIZE(irregular_root_refs); i++)
		if (!strcmp(refname, irregular_root_refs[i]))
			return 1;

	return 0;
}

static int is_current_worktree_ref(const char *ref) {
	return is_root_ref_syntax(ref) || is_per_worktree_ref(ref);
}

enum ref_worktree_type parse_worktree_ref(const char *maybe_worktree_ref,
					  const char **worktree_name, int *worktree_name_length,
					  const char **bare_refname)
{
	const char *name_dummy;
	int name_length_dummy;
	const char *ref_dummy;

	if (!worktree_name)
		worktree_name = &name_dummy;
	if (!worktree_name_length)
		worktree_name_length = &name_length_dummy;
	if (!bare_refname)
		bare_refname = &ref_dummy;

	if (skip_prefix(maybe_worktree_ref, "worktrees/", bare_refname)) {
		const char *slash = strchr(*bare_refname, '/');

		*worktree_name = *bare_refname;
		if (!slash) {
			*worktree_name_length = strlen(*worktree_name);

			/* This is an error condition, and the caller tell because the bare_refname is "" */
			*bare_refname = *worktree_name + *worktree_name_length;
			return REF_WORKTREE_OTHER;
		}

		*worktree_name_length = slash - *bare_refname;
		*bare_refname = slash + 1;

		if (is_current_worktree_ref(*bare_refname))
			return REF_WORKTREE_OTHER;
	}

	*worktree_name = NULL;
	*worktree_name_length = 0;

	if (skip_prefix(maybe_worktree_ref, "main-worktree/", bare_refname)
	    && is_current_worktree_ref(*bare_refname))
		return REF_WORKTREE_MAIN;

	*bare_refname = maybe_worktree_ref;
	if (is_current_worktree_ref(maybe_worktree_ref))
		return REF_WORKTREE_CURRENT;

	return REF_WORKTREE_SHARED;
}

long get_files_ref_lock_timeout_ms(void)
{
	static int configured = 0;

	/* The default timeout is 100 ms: */
	static int timeout_ms = 100;

	if (!configured) {
		git_config_get_int("core.filesreflocktimeout", &timeout_ms);
		configured = 1;
	}

	return timeout_ms;
}

int refs_delete_ref(struct ref_store *refs, const char *msg,
		    const char *refname,
		    const struct object_id *old_oid,
		    unsigned int flags)
{
	struct ref_transaction *transaction;
	struct strbuf err = STRBUF_INIT;

	transaction = ref_store_transaction_begin(refs, 0, &err);
	if (!transaction ||
	    ref_transaction_delete(transaction, refname, old_oid,
				   NULL, flags, msg, &err) ||
	    ref_transaction_commit(transaction, &err)) {
		error("%s", err.buf);
		ref_transaction_free(transaction);
		strbuf_release(&err);
		return 1;
	}
	ref_transaction_free(transaction);
	strbuf_release(&err);
	return 0;
}

static void copy_reflog_msg(struct strbuf *sb, const char *msg)
{
	char c;
	int wasspace = 1;

	while ((c = *msg++)) {
		if (wasspace && isspace(c))
			continue;
		wasspace = isspace(c);
		if (wasspace)
			c = ' ';
		strbuf_addch(sb, c);
	}
	strbuf_rtrim(sb);
}

static char *normalize_reflog_message(const char *msg)
{
	struct strbuf sb = STRBUF_INIT;

	if (msg && *msg)
		copy_reflog_msg(&sb, msg);
	return strbuf_detach(&sb, NULL);
}

int should_autocreate_reflog(enum log_refs_config log_all_ref_updates,
			     const char *refname)
{
	switch (log_all_ref_updates) {
	case LOG_REFS_ALWAYS:
		return 1;
	case LOG_REFS_NORMAL:
		return starts_with(refname, "refs/heads/") ||
			starts_with(refname, "refs/remotes/") ||
			starts_with(refname, "refs/notes/") ||
			!strcmp(refname, "HEAD");
	default:
		return 0;
	}
}

int is_branch(const char *refname)
{
	return !strcmp(refname, "HEAD") || starts_with(refname, "refs/heads/");
}

struct read_ref_at_cb {
	const char *refname;
	timestamp_t at_time;
	int cnt;
	int reccnt;
	struct object_id *oid;
	int found_it;

	struct object_id ooid;
	struct object_id noid;
	int tz;
	timestamp_t date;
	char **msg;
	timestamp_t *cutoff_time;
	int *cutoff_tz;
	int *cutoff_cnt;
};

static void set_read_ref_cutoffs(struct read_ref_at_cb *cb,
		timestamp_t timestamp, int tz, const char *message)
{
	if (cb->msg)
		*cb->msg = xstrdup(message);
	if (cb->cutoff_time)
		*cb->cutoff_time = timestamp;
	if (cb->cutoff_tz)
		*cb->cutoff_tz = tz;
	if (cb->cutoff_cnt)
		*cb->cutoff_cnt = cb->reccnt;
}

static int read_ref_at_ent(struct object_id *ooid, struct object_id *noid,
			   const char *email UNUSED,
			   timestamp_t timestamp, int tz,
			   const char *message, void *cb_data)
{
	struct read_ref_at_cb *cb = cb_data;

	cb->tz = tz;
	cb->date = timestamp;

	if (timestamp <= cb->at_time || cb->cnt == 0) {
		set_read_ref_cutoffs(cb, timestamp, tz, message);
		/*
		 * we have not yet updated cb->[n|o]oid so they still
		 * hold the values for the previous record.
		 */
		if (!is_null_oid(&cb->ooid)) {
			oidcpy(cb->oid, noid);
			if (!oideq(&cb->ooid, noid))
				warning(_("log for ref %s has gap after %s"),
					cb->refname, show_date(cb->date, cb->tz, DATE_MODE(RFC2822)));
		}
		else if (cb->date == cb->at_time)
			oidcpy(cb->oid, noid);
		else if (!oideq(noid, cb->oid))
			warning(_("log for ref %s unexpectedly ended on %s"),
				cb->refname, show_date(cb->date, cb->tz,
						       DATE_MODE(RFC2822)));
		cb->reccnt++;
		oidcpy(&cb->ooid, ooid);
		oidcpy(&cb->noid, noid);
		cb->found_it = 1;
		return 1;
	}
	cb->reccnt++;
	oidcpy(&cb->ooid, ooid);
	oidcpy(&cb->noid, noid);
	if (cb->cnt > 0)
		cb->cnt--;
	return 0;
}

static int read_ref_at_ent_oldest(struct object_id *ooid, struct object_id *noid,
				  const char *email UNUSED,
				  timestamp_t timestamp, int tz,
				  const char *message, void *cb_data)
{
	struct read_ref_at_cb *cb = cb_data;

	set_read_ref_cutoffs(cb, timestamp, tz, message);
	oidcpy(cb->oid, ooid);
	if (cb->at_time && is_null_oid(cb->oid))
		oidcpy(cb->oid, noid);
	/* We just want the first entry */
	return 1;
}

int read_ref_at(struct ref_store *refs, const char *refname,
		unsigned int flags, timestamp_t at_time, int cnt,
		struct object_id *oid, char **msg,
		timestamp_t *cutoff_time, int *cutoff_tz, int *cutoff_cnt)
{
	struct read_ref_at_cb cb;

	memset(&cb, 0, sizeof(cb));
	cb.refname = refname;
	cb.at_time = at_time;
	cb.cnt = cnt;
	cb.msg = msg;
	cb.cutoff_time = cutoff_time;
	cb.cutoff_tz = cutoff_tz;
	cb.cutoff_cnt = cutoff_cnt;
	cb.oid = oid;

	refs_for_each_reflog_ent_reverse(refs, refname, read_ref_at_ent, &cb);

	if (!cb.reccnt) {
		if (cnt == 0) {
			/*
			 * The caller asked for ref@{0}, and we had no entries.
			 * It's a bit subtle, but in practice all callers have
			 * prepped the "oid" field with the current value of
			 * the ref, which is the most reasonable fallback.
			 *
			 * We'll put dummy values into the out-parameters (so
			 * they're not just uninitialized garbage), and the
			 * caller can take our return value as a hint that
			 * we did not find any such reflog.
			 */
			set_read_ref_cutoffs(&cb, 0, 0, "empty reflog");
			return 1;
		}
		if (flags & GET_OID_QUIETLY)
			exit(128);
		else
			die(_("log for %s is empty"), refname);
	}
	if (cb.found_it)
		return 0;

	refs_for_each_reflog_ent(refs, refname, read_ref_at_ent_oldest, &cb);

	return 1;
}

struct ref_transaction *ref_store_transaction_begin(struct ref_store *refs,
						    unsigned int flags,
						    struct strbuf *err)
{
	struct ref_transaction *tr;
	assert(err);

	CALLOC_ARRAY(tr, 1);
	tr->ref_store = refs;
	tr->flags = flags;
	string_list_init_dup(&tr->refnames);

	if (flags & REF_TRANSACTION_ALLOW_FAILURE)
		CALLOC_ARRAY(tr->rejections, 1);

	return tr;
}

void ref_transaction_free(struct ref_transaction *transaction)
{
	size_t i;

	if (!transaction)
		return;

	switch (transaction->state) {
	case REF_TRANSACTION_OPEN:
	case REF_TRANSACTION_CLOSED:
		/* OK */
		break;
	case REF_TRANSACTION_PREPARED:
		BUG("free called on a prepared reference transaction");
		break;
	default:
		BUG("unexpected reference transaction state");
		break;
	}

	for (i = 0; i < transaction->nr; i++) {
		free(transaction->updates[i]->msg);
		free(transaction->updates[i]->committer_info);
		free((char *)transaction->updates[i]->new_target);
		free((char *)transaction->updates[i]->old_target);
		free(transaction->updates[i]);
	}

	if (transaction->rejections)
		free(transaction->rejections->update_indices);
	free(transaction->rejections);

	string_list_clear(&transaction->refnames, 0);
	free(transaction->updates);
	free(transaction);
}

int ref_transaction_maybe_set_rejected(struct ref_transaction *transaction,
				       size_t update_idx,
				       enum ref_transaction_error err)
{
	if (update_idx >= transaction->nr)
		BUG("trying to set rejection on invalid update index");

	if (!(transaction->flags & REF_TRANSACTION_ALLOW_FAILURE))
		return 0;

	if (!transaction->rejections)
		BUG("transaction not inititalized with failure support");

	/*
	 * Don't accept generic errors, since these errors are not user
	 * input related.
	 */
	if (err == REF_TRANSACTION_ERROR_GENERIC)
		return 0;

	transaction->updates[update_idx]->rejection_err = err;
	ALLOC_GROW(transaction->rejections->update_indices,
		   transaction->rejections->nr + 1,
		   transaction->rejections->alloc);
	transaction->rejections->update_indices[transaction->rejections->nr++] = update_idx;

	return 1;
}

struct ref_update *ref_transaction_add_update(
		struct ref_transaction *transaction,
		const char *refname, unsigned int flags,
		const struct object_id *new_oid,
		const struct object_id *old_oid,
		const char *new_target, const char *old_target,
		const char *committer_info,
		const char *msg)
{
	struct string_list_item *item;
	struct ref_update *update;

	if (transaction->state != REF_TRANSACTION_OPEN)
		BUG("update called for transaction that is not open");

	if (old_oid && old_target)
		BUG("only one of old_oid and old_target should be non NULL");
	if (new_oid && new_target)
		BUG("only one of new_oid and new_target should be non NULL");

	FLEX_ALLOC_STR(update, refname, refname);
	ALLOC_GROW(transaction->updates, transaction->nr + 1, transaction->alloc);
	transaction->updates[transaction->nr++] = update;

	update->flags = flags;
	update->rejection_err = 0;

	update->new_target = xstrdup_or_null(new_target);
	update->old_target = xstrdup_or_null(old_target);
	if ((flags & REF_HAVE_NEW) && new_oid)
		oidcpy(&update->new_oid, new_oid);
	if ((flags & REF_HAVE_OLD) && old_oid)
		oidcpy(&update->old_oid, old_oid);
	if (!(flags & REF_SKIP_CREATE_REFLOG)) {
		update->committer_info = xstrdup_or_null(committer_info);
		update->msg = normalize_reflog_message(msg);
	}

	/*
	 * This list is generally used by the backends to avoid duplicates.
	 * But we do support multiple log updates for a given refname within
	 * a single transaction.
	 */
	if (!(update->flags & REF_LOG_ONLY)) {
		item = string_list_append(&transaction->refnames, refname);
		item->util = update;
	}

	return update;
}

static int transaction_refname_valid(const char *refname,
				     const struct object_id *new_oid,
				     unsigned int flags, struct strbuf *err)
{
	if (flags & REF_SKIP_REFNAME_VERIFICATION)
		return 1;

	if (is_pseudo_ref(refname)) {
		const char *refusal_msg;
		if (flags & REF_LOG_ONLY)
			refusal_msg = _("refusing to update reflog for pseudoref '%s'");
		else
			refusal_msg = _("refusing to update pseudoref '%s'");
		strbuf_addf(err, refusal_msg, refname);
		return 0;
	} else if ((new_oid && !is_null_oid(new_oid)) ?
		 check_refname_format(refname, REFNAME_ALLOW_ONELEVEL) :
		 !refname_is_safe(refname)) {
		const char *refusal_msg;
		if (flags & REF_LOG_ONLY)
			refusal_msg = _("refusing to update reflog with bad name '%s'");
		else
			refusal_msg = _("refusing to update ref with bad name '%s'");
		strbuf_addf(err, refusal_msg, refname);
		return 0;
	}

	return 1;
}

int ref_transaction_update(struct ref_transaction *transaction,
			   const char *refname,
			   const struct object_id *new_oid,
			   const struct object_id *old_oid,
			   const char *new_target,
			   const char *old_target,
			   unsigned int flags, const char *msg,
			   struct strbuf *err)
{
	assert(err);

	if ((flags & REF_FORCE_CREATE_REFLOG) &&
	    (flags & REF_SKIP_CREATE_REFLOG)) {
		strbuf_addstr(err, _("refusing to force and skip creation of reflog"));
		return -1;
	}

	if (!transaction_refname_valid(refname, new_oid, flags, err))
		return -1;

	if (flags & ~REF_TRANSACTION_UPDATE_ALLOWED_FLAGS)
		BUG("illegal flags 0x%x passed to ref_transaction_update()", flags);

	/*
	 * Clear flags outside the allowed set; this should be a noop because
	 * of the BUG() check above, but it works around a -Wnonnull warning
	 * with some versions of "gcc -O3".
	 */
	flags &= REF_TRANSACTION_UPDATE_ALLOWED_FLAGS;

	flags |= (new_oid ? REF_HAVE_NEW : 0) | (old_oid ? REF_HAVE_OLD : 0);
	flags |= (new_target ? REF_HAVE_NEW : 0) | (old_target ? REF_HAVE_OLD : 0);

	ref_transaction_add_update(transaction, refname, flags,
				   new_oid, old_oid, new_target,
				   old_target, NULL, msg);

	return 0;
}

/*
 * Similar to`ref_transaction_update`, but this function is only for adding
 * a reflog update. Supports providing custom committer information. The index
 * field can be utiltized to order updates as desired. When not used, the
 * updates default to being ordered by refname.
 */
static int ref_transaction_update_reflog(struct ref_transaction *transaction,
					 const char *refname,
					 const struct object_id *new_oid,
					 const struct object_id *old_oid,
					 const char *committer_info,
					 unsigned int flags,
					 const char *msg,
					 uint64_t index,
					 struct strbuf *err)
{
	struct ref_update *update;

	assert(err);

	flags |= REF_LOG_ONLY | REF_FORCE_CREATE_REFLOG | REF_NO_DEREF;

	if (!transaction_refname_valid(refname, new_oid, flags, err))
		return -1;

	update = ref_transaction_add_update(transaction, refname, flags,
					    new_oid, old_oid, NULL, NULL,
					    committer_info, msg);
	/*
	 * While we do set the old_oid value, we unset the flag to skip
	 * old_oid verification which only makes sense for refs.
	 */
	update->flags &= ~REF_HAVE_OLD;
	update->index = index;

	/*
	 * Reference backends may need to know the max index to optimize
	 * their writes. So we store the max_index on the transaction level.
	 */
	if (index > transaction->max_index)
		transaction->max_index = index;

	return 0;
}

int ref_transaction_create(struct ref_transaction *transaction,
			   const char *refname,
			   const struct object_id *new_oid,
			   const char *new_target,
			   unsigned int flags, const char *msg,
			   struct strbuf *err)
{
	if (new_oid && new_target)
		BUG("create called with both new_oid and new_target set");
	if ((!new_oid || is_null_oid(new_oid)) && !new_target) {
		strbuf_addf(err, "'%s' has neither a valid OID nor a target", refname);
		return 1;
	}
	return ref_transaction_update(transaction, refname, new_oid,
				      null_oid(the_hash_algo), new_target, NULL, flags,
				      msg, err);
}

int ref_transaction_delete(struct ref_transaction *transaction,
			   const char *refname,
			   const struct object_id *old_oid,
			   const char *old_target,
			   unsigned int flags,
			   const char *msg,
			   struct strbuf *err)
{
	if (old_oid && is_null_oid(old_oid))
		BUG("delete called with old_oid set to zeros");
	if (old_oid && old_target)
		BUG("delete called with both old_oid and old_target set");
	if (old_target && !(flags & REF_NO_DEREF))
		BUG("delete cannot operate on symrefs with deref mode");
	return ref_transaction_update(transaction, refname,
				      null_oid(the_hash_algo), old_oid,
				      NULL, old_target, flags,
				      msg, err);
}

int ref_transaction_verify(struct ref_transaction *transaction,
			   const char *refname,
			   const struct object_id *old_oid,
			   const char *old_target,
			   unsigned int flags,
			   struct strbuf *err)
{
	if (!old_target && !old_oid)
		BUG("verify called with old_oid and old_target set to NULL");
	if (old_oid && old_target)
		BUG("verify called with both old_oid and old_target set");
	if (old_target && !(flags & REF_NO_DEREF))
		BUG("verify cannot operate on symrefs with deref mode");
	return ref_transaction_update(transaction, refname,
				      NULL, old_oid,
				      NULL, old_target,
				      flags, NULL, err);
}

int refs_update_ref(struct ref_store *refs, const char *msg,
		    const char *refname, const struct object_id *new_oid,
		    const struct object_id *old_oid, unsigned int flags,
		    enum action_on_err onerr)
{
	struct ref_transaction *t = NULL;
	struct strbuf err = STRBUF_INIT;
	int ret = 0;

	t = ref_store_transaction_begin(refs, 0, &err);
	if (!t ||
	    ref_transaction_update(t, refname, new_oid, old_oid, NULL, NULL,
				   flags, msg, &err) ||
	    ref_transaction_commit(t, &err)) {
		ret = 1;
		ref_transaction_free(t);
	}
	if (ret) {
		const char *str = _("update_ref failed for ref '%s': %s");

		switch (onerr) {
		case UPDATE_REFS_MSG_ON_ERR:
			error(str, refname, err.buf);
			break;
		case UPDATE_REFS_DIE_ON_ERR:
			die(str, refname, err.buf);
			break;
		case UPDATE_REFS_QUIET_ON_ERR:
			break;
		}
		strbuf_release(&err);
		return 1;
	}
	strbuf_release(&err);
	if (t)
		ref_transaction_free(t);
	return 0;
}

/*
 * Check that the string refname matches a rule of the form
 * "{prefix}%.*s{suffix}". So "foo/bar/baz" would match the rule
 * "foo/%.*s/baz", and return the string "bar".
 */
static const char *match_parse_rule(const char *refname, const char *rule,
				    size_t *len)
{
	/*
	 * Check that rule matches refname up to the first percent in the rule.
	 * We can bail immediately if not, but otherwise we leave "rule" at the
	 * %-placeholder, and "refname" at the start of the potential matched
	 * name.
	 */
	while (*rule != '%') {
		if (!*rule)
			BUG("rev-parse rule did not have percent");
		if (*refname++ != *rule++)
			return NULL;
	}

	/*
	 * Check that our "%" is the expected placeholder. This assumes there
	 * are no other percents (placeholder or quoted) in the string, but
	 * that is sufficient for our rev-parse rules.
	 */
	if (!skip_prefix(rule, "%.*s", &rule))
		return NULL;

	/*
	 * And now check that our suffix (if any) matches.
	 */
	if (!strip_suffix(refname, rule, len))
		return NULL;

	return refname; /* len set by strip_suffix() */
}

char *refs_shorten_unambiguous_ref(struct ref_store *refs,
				   const char *refname, int strict)
{
	int i;
	struct strbuf resolved_buf = STRBUF_INIT;

	/* skip first rule, it will always match */
	for (i = NUM_REV_PARSE_RULES - 1; i > 0 ; --i) {
		int j;
		int rules_to_fail = i;
		const char *short_name;
		size_t short_name_len;

		short_name = match_parse_rule(refname, ref_rev_parse_rules[i],
					      &short_name_len);
		if (!short_name)
			continue;

		/*
		 * in strict mode, all (except the matched one) rules
		 * must fail to resolve to a valid non-ambiguous ref
		 */
		if (strict)
			rules_to_fail = NUM_REV_PARSE_RULES;

		/*
		 * check if the short name resolves to a valid ref,
		 * but use only rules prior to the matched one
		 */
		for (j = 0; j < rules_to_fail; j++) {
			const char *rule = ref_rev_parse_rules[j];

			/* skip matched rule */
			if (i == j)
				continue;

			/*
			 * the short name is ambiguous, if it resolves
			 * (with this previous rule) to a valid ref
			 * read_ref() returns 0 on success
			 */
			strbuf_reset(&resolved_buf);
			strbuf_addf(&resolved_buf, rule,
				    cast_size_t_to_int(short_name_len),
				    short_name);
			if (refs_ref_exists(refs, resolved_buf.buf))
				break;
		}

		/*
		 * short name is non-ambiguous if all previous rules
		 * haven't resolved to a valid ref
		 */
		if (j == rules_to_fail) {
			strbuf_release(&resolved_buf);
			return xmemdupz(short_name, short_name_len);
		}
	}

	strbuf_release(&resolved_buf);
	return xstrdup(refname);
}

int parse_hide_refs_config(const char *var, const char *value, const char *section,
			   struct strvec *hide_refs)
{
	const char *key;
	if (!strcmp("transfer.hiderefs", var) ||
	    (!parse_config_key(var, section, NULL, NULL, &key) &&
	     !strcmp(key, "hiderefs"))) {
		char *ref;
		int len;

		if (!value)
			return config_error_nonbool(var);

		/* drop const to remove trailing '/' characters */
		ref = (char *)strvec_push(hide_refs, value);
		len = strlen(ref);
		while (len && ref[len - 1] == '/')
			ref[--len] = '\0';
	}
	return 0;
}

int ref_is_hidden(const char *refname, const char *refname_full,
		  const struct strvec *hide_refs)
{
	int i;

	for (i = hide_refs->nr - 1; i >= 0; i--) {
		const char *match = hide_refs->v[i];
		const char *subject;
		int neg = 0;
		const char *p;

		if (*match == '!') {
			neg = 1;
			match++;
		}

		if (*match == '^') {
			subject = refname_full;
			match++;
		} else {
			subject = refname;
		}

		/* refname can be NULL when namespaces are used. */
		if (subject &&
		    skip_prefix(subject, match, &p) &&
		    (!*p || *p == '/'))
			return !neg;
	}
	return 0;
}

const char **hidden_refs_to_excludes(const struct strvec *hide_refs)
{
	const char **pattern;
	for (pattern = hide_refs->v; *pattern; pattern++) {
		/*
		 * We can't feed any excludes from hidden refs config
		 * sections, since later rules may override previous
		 * ones. For example, with rules "refs/foo" and
		 * "!refs/foo/bar", we should show "refs/foo/bar" (and
		 * everything underneath it), but the earlier exclusion
		 * would cause us to skip all of "refs/foo".  We
		 * likewise don't implement the namespace stripping
		 * required for '^' rules.
		 *
		 * Both are possible to do, but complicated, so avoid
		 * populating the jump list at all if we see either of
		 * these patterns.
		 */
		if (**pattern == '!' || **pattern == '^')
			return NULL;
	}
	return hide_refs->v;
}

const char **get_namespaced_exclude_patterns(const char **exclude_patterns,
					     const char *namespace,
					     struct strvec *out)
{
	if (!namespace || !*namespace || !exclude_patterns || !*exclude_patterns)
		return exclude_patterns;

	for (size_t i = 0; exclude_patterns[i]; i++)
		strvec_pushf(out, "%s%s", namespace, exclude_patterns[i]);

	return out->v;
}

const char *find_descendant_ref(const char *dirname,
				const struct string_list *extras,
				const struct string_list *skip)
{
	int pos;

	if (!extras)
		return NULL;

	/*
	 * Look at the place where dirname would be inserted into
	 * extras. If there is an entry at that position that starts
	 * with dirname (remember, dirname includes the trailing
	 * slash) and is not in skip, then we have a conflict.
	 */
	for (pos = string_list_find_insert_index(extras, dirname, 0);
	     pos < extras->nr; pos++) {
		const char *extra_refname = extras->items[pos].string;

		if (!starts_with(extra_refname, dirname))
			break;

		if (!skip || !string_list_has_string(skip, extra_refname))
			return extra_refname;
	}
	return NULL;
}

int refs_head_ref(struct ref_store *refs, each_ref_fn fn, void *cb_data)
{
	struct object_id oid;
	int flag;

	if (refs_resolve_ref_unsafe(refs, "HEAD", RESOLVE_REF_READING,
				    &oid, &flag))
		return fn("HEAD", NULL, &oid, flag, cb_data);

	return 0;
}

struct ref_iterator *refs_ref_iterator_begin(
		struct ref_store *refs,
		const char *prefix,
		const char **exclude_patterns,
		int trim,
		enum do_for_each_ref_flags flags)
{
	struct ref_iterator *iter;
	struct strvec normalized_exclude_patterns = STRVEC_INIT;

	if (exclude_patterns) {
		for (size_t i = 0; exclude_patterns[i]; i++) {
			const char *pattern = exclude_patterns[i];
			size_t len = strlen(pattern);
			if (!len)
				continue;

			if (pattern[len - 1] == '/')
				strvec_push(&normalized_exclude_patterns, pattern);
			else
				strvec_pushf(&normalized_exclude_patterns, "%s/",
					     pattern);
		}

		exclude_patterns = normalized_exclude_patterns.v;
	}

	if (!(flags & DO_FOR_EACH_INCLUDE_BROKEN)) {
		static int ref_paranoia = -1;

		if (ref_paranoia < 0)
			ref_paranoia = git_env_bool("GIT_REF_PARANOIA", 1);
		if (ref_paranoia) {
			flags |= DO_FOR_EACH_INCLUDE_BROKEN;
			flags |= DO_FOR_EACH_OMIT_DANGLING_SYMREFS;
		}
	}

	iter = refs->be->iterator_begin(refs, prefix, exclude_patterns, flags);
	/*
	 * `iterator_begin()` already takes care of prefix, but we
	 * might need to do some trimming:
	 */
	if (trim)
		iter = prefix_ref_iterator_begin(iter, "", trim);

	strvec_clear(&normalized_exclude_patterns);

	return iter;
}

static int do_for_each_ref(struct ref_store *refs, const char *prefix,
			   const char **exclude_patterns,
			   each_ref_fn fn, int trim,
			   enum do_for_each_ref_flags flags, void *cb_data)
{
	struct ref_iterator *iter;

	if (!refs)
		return 0;

	iter = refs_ref_iterator_begin(refs, prefix, exclude_patterns, trim,
				       flags);

	return do_for_each_ref_iterator(iter, fn, cb_data);
}

int refs_for_each_ref(struct ref_store *refs, each_ref_fn fn, void *cb_data)
{
	return do_for_each_ref(refs, "", NULL, fn, 0, 0, cb_data);
}

int refs_for_each_ref_in(struct ref_store *refs, const char *prefix,
			 each_ref_fn fn, void *cb_data)
{
	return do_for_each_ref(refs, prefix, NULL, fn, strlen(prefix), 0, cb_data);
}

int refs_for_each_fullref_in(struct ref_store *refs, const char *prefix,
			     const char **exclude_patterns,
			     each_ref_fn fn, void *cb_data)
{
	return do_for_each_ref(refs, prefix, exclude_patterns, fn, 0, 0, cb_data);
}

int refs_for_each_replace_ref(struct ref_store *refs, each_ref_fn fn, void *cb_data)
{
	const char *git_replace_ref_base = ref_namespace[NAMESPACE_REPLACE].ref;
	return do_for_each_ref(refs, git_replace_ref_base, NULL, fn,
			       strlen(git_replace_ref_base),
			       DO_FOR_EACH_INCLUDE_BROKEN, cb_data);
}

int refs_for_each_namespaced_ref(struct ref_store *refs,
				 const char **exclude_patterns,
				 each_ref_fn fn, void *cb_data)
{
	struct strvec namespaced_exclude_patterns = STRVEC_INIT;
	struct strbuf prefix = STRBUF_INIT;
	int ret;

	exclude_patterns = get_namespaced_exclude_patterns(exclude_patterns,
							   get_git_namespace(),
							   &namespaced_exclude_patterns);

	strbuf_addf(&prefix, "%srefs/", get_git_namespace());
	ret = do_for_each_ref(refs, prefix.buf, exclude_patterns, fn, 0, 0, cb_data);

	strvec_clear(&namespaced_exclude_patterns);
	strbuf_release(&prefix);
	return ret;
}

int refs_for_each_rawref(struct ref_store *refs, each_ref_fn fn, void *cb_data)
{
	return do_for_each_ref(refs, "", NULL, fn, 0,
			       DO_FOR_EACH_INCLUDE_BROKEN, cb_data);
}

int refs_for_each_include_root_refs(struct ref_store *refs, each_ref_fn fn,
				    void *cb_data)
{
	return do_for_each_ref(refs, "", NULL, fn, 0,
			       DO_FOR_EACH_INCLUDE_ROOT_REFS, cb_data);
}

static int qsort_strcmp(const void *va, const void *vb)
{
	const char *a = *(const char **)va;
	const char *b = *(const char **)vb;

	return strcmp(a, b);
}

static void find_longest_prefixes_1(struct string_list *out,
				  struct strbuf *prefix,
				  const char **patterns, size_t nr)
{
	size_t i;

	for (i = 0; i < nr; i++) {
		char c = patterns[i][prefix->len];
		if (!c || is_glob_special(c)) {
			string_list_append(out, prefix->buf);
			return;
		}
	}

	i = 0;
	while (i < nr) {
		size_t end;

		/*
		* Set "end" to the index of the element _after_ the last one
		* in our group.
		*/
		for (end = i + 1; end < nr; end++) {
			if (patterns[i][prefix->len] != patterns[end][prefix->len])
				break;
		}

		strbuf_addch(prefix, patterns[i][prefix->len]);
		find_longest_prefixes_1(out, prefix, patterns + i, end - i);
		strbuf_setlen(prefix, prefix->len - 1);

		i = end;
	}
}

static void find_longest_prefixes(struct string_list *out,
				  const char **patterns)
{
	struct strvec sorted = STRVEC_INIT;
	struct strbuf prefix = STRBUF_INIT;

	strvec_pushv(&sorted, patterns);
	QSORT(sorted.v, sorted.nr, qsort_strcmp);

	find_longest_prefixes_1(out, &prefix, sorted.v, sorted.nr);

	strvec_clear(&sorted);
	strbuf_release(&prefix);
}

int refs_for_each_fullref_in_prefixes(struct ref_store *ref_store,
				      const char *namespace,
				      const char **patterns,
				      const char **exclude_patterns,
				      each_ref_fn fn, void *cb_data)
{
	struct strvec namespaced_exclude_patterns = STRVEC_INIT;
	struct string_list prefixes = STRING_LIST_INIT_DUP;
	struct string_list_item *prefix;
	struct strbuf buf = STRBUF_INIT;
	int ret = 0, namespace_len;

	find_longest_prefixes(&prefixes, patterns);

	if (namespace)
		strbuf_addstr(&buf, namespace);
	namespace_len = buf.len;

	exclude_patterns = get_namespaced_exclude_patterns(exclude_patterns,
							   namespace,
							   &namespaced_exclude_patterns);

	for_each_string_list_item(prefix, &prefixes) {
		strbuf_addstr(&buf, prefix->string);
		ret = refs_for_each_fullref_in(ref_store, buf.buf,
					       exclude_patterns, fn, cb_data);
		if (ret)
			break;
		strbuf_setlen(&buf, namespace_len);
	}

	strvec_clear(&namespaced_exclude_patterns);
	string_list_clear(&prefixes, 0);
	strbuf_release(&buf);
	return ret;
}

static int refs_read_special_head(struct ref_store *ref_store,
				  const char *refname, struct object_id *oid,
				  struct strbuf *referent, unsigned int *type,
				  int *failure_errno)
{
	struct strbuf full_path = STRBUF_INIT;
	struct strbuf content = STRBUF_INIT;
	int result = -1;
	strbuf_addf(&full_path, "%s/%s", ref_store->gitdir, refname);

	if (strbuf_read_file(&content, full_path.buf, 0) < 0) {
		*failure_errno = errno;
		goto done;
	}

	result = parse_loose_ref_contents(ref_store->repo->hash_algo, content.buf,
					  oid, referent, type, NULL, failure_errno);

done:
	strbuf_release(&full_path);
	strbuf_release(&content);
	return result;
}

int refs_read_raw_ref(struct ref_store *ref_store, const char *refname,
		      struct object_id *oid, struct strbuf *referent,
		      unsigned int *type, int *failure_errno)
{
	assert(failure_errno);
	if (is_pseudo_ref(refname))
		return refs_read_special_head(ref_store, refname, oid, referent,
					      type, failure_errno);

	return ref_store->be->read_raw_ref(ref_store, refname, oid, referent,
					   type, failure_errno);
}

int refs_read_symbolic_ref(struct ref_store *ref_store, const char *refname,
			   struct strbuf *referent)
{
	return ref_store->be->read_symbolic_ref(ref_store, refname, referent);
}

const char *refs_resolve_ref_unsafe(struct ref_store *refs,
				    const char *refname,
				    int resolve_flags,
				    struct object_id *oid,
				    int *flags)
{
	static struct strbuf sb_refname = STRBUF_INIT;
	struct object_id unused_oid;
	int unused_flags;
	int symref_count;

	if (!oid)
		oid = &unused_oid;
	if (!flags)
		flags = &unused_flags;

	*flags = 0;

	if (check_refname_format(refname, REFNAME_ALLOW_ONELEVEL)) {
		if (!(resolve_flags & RESOLVE_REF_ALLOW_BAD_NAME) ||
		    !refname_is_safe(refname))
			return NULL;

		/*
		 * repo_dwim_ref() uses REF_ISBROKEN to distinguish between
		 * missing refs and refs that were present but invalid,
		 * to complain about the latter to stderr.
		 *
		 * We don't know whether the ref exists, so don't set
		 * REF_ISBROKEN yet.
		 */
		*flags |= REF_BAD_NAME;
	}

	for (symref_count = 0; symref_count < SYMREF_MAXDEPTH; symref_count++) {
		unsigned int read_flags = 0;
		int failure_errno;

		if (refs_read_raw_ref(refs, refname, oid, &sb_refname,
				      &read_flags, &failure_errno)) {
			*flags |= read_flags;

			/* In reading mode, refs must eventually resolve */
			if (resolve_flags & RESOLVE_REF_READING)
				return NULL;

			/*
			 * Otherwise a missing ref is OK. But the files backend
			 * may show errors besides ENOENT if there are
			 * similarly-named refs.
			 */
			if (failure_errno != ENOENT &&
			    failure_errno != EISDIR &&
			    failure_errno != ENOTDIR)
				return NULL;

			oidclr(oid, refs->repo->hash_algo);
			if (*flags & REF_BAD_NAME)
				*flags |= REF_ISBROKEN;
			return refname;
		}

		*flags |= read_flags;

		if (!(read_flags & REF_ISSYMREF)) {
			if (*flags & REF_BAD_NAME) {
				oidclr(oid, refs->repo->hash_algo);
				*flags |= REF_ISBROKEN;
			}
			return refname;
		}

		refname = sb_refname.buf;
		if (resolve_flags & RESOLVE_REF_NO_RECURSE) {
			oidclr(oid, refs->repo->hash_algo);
			return refname;
		}
		if (check_refname_format(refname, REFNAME_ALLOW_ONELEVEL)) {
			if (!(resolve_flags & RESOLVE_REF_ALLOW_BAD_NAME) ||
			    !refname_is_safe(refname))
				return NULL;

			*flags |= REF_ISBROKEN | REF_BAD_NAME;
		}
	}

	return NULL;
}

/* backend functions */
int ref_store_create_on_disk(struct ref_store *refs, int flags, struct strbuf *err)
{
	return refs->be->create_on_disk(refs, flags, err);
}

int ref_store_remove_on_disk(struct ref_store *refs, struct strbuf *err)
{
	return refs->be->remove_on_disk(refs, err);
}

int repo_resolve_gitlink_ref(struct repository *r,
			     const char *submodule, const char *refname,
			     struct object_id *oid)
{
	struct ref_store *refs;
	int flags;

	refs = repo_get_submodule_ref_store(r, submodule);
	if (!refs)
		return -1;

	if (!refs_resolve_ref_unsafe(refs, refname, 0, oid, &flags) ||
	    is_null_oid(oid))
		return -1;
	return 0;
}

/*
 * Look up a ref store by name. If that ref_store hasn't been
 * registered yet, return NULL.
 */
static struct ref_store *lookup_ref_store_map(struct strmap *map,
					      const char *name)
{
	struct strmap_entry *entry;

	if (!map->map.tablesize)
		/* It's initialized on demand in register_ref_store(). */
		return NULL;

	entry = strmap_get_entry(map, name);
	return entry ? entry->value : NULL;
}

/*
 * Create, record, and return a ref_store instance for the specified
 * gitdir using the given ref storage format.
 */
static struct ref_store *ref_store_init(struct repository *repo,
					enum ref_storage_format format,
					const char *gitdir,
					unsigned int flags)
{
	const struct ref_storage_be *be;
	struct ref_store *refs;

	be = find_ref_storage_backend(format);
	if (!be)
		BUG("reference backend is unknown");

	refs = be->init(repo, gitdir, flags);
	return refs;
}

void ref_store_release(struct ref_store *ref_store)
{
	ref_store->be->release(ref_store);
	free(ref_store->gitdir);
}

struct ref_store *get_main_ref_store(struct repository *r)
{
	if (r->refs_private)
		return r->refs_private;

	if (!r->gitdir)
		BUG("attempting to get main_ref_store outside of repository");

	r->refs_private = ref_store_init(r, r->ref_storage_format,
					 r->gitdir, REF_STORE_ALL_CAPS);
	r->refs_private = maybe_debug_wrap_ref_store(r->gitdir, r->refs_private);
	return r->refs_private;
}

/*
 * Associate a ref store with a name. It is a fatal error to call this
 * function twice for the same name.
 */
static void register_ref_store_map(struct strmap *map,
				   const char *type,
				   struct ref_store *refs,
				   const char *name)
{
	if (!map->map.tablesize)
		strmap_init(map);
	if (strmap_put(map, name, refs))
		BUG("%s ref_store '%s' initialized twice", type, name);
}

struct ref_store *repo_get_submodule_ref_store(struct repository *repo,
					       const char *submodule)
{
	struct strbuf submodule_sb = STRBUF_INIT;
	struct ref_store *refs;
	char *to_free = NULL;
	size_t len;
	struct repository *subrepo;

	if (!submodule)
		return NULL;

	len = strlen(submodule);
	while (len && is_dir_sep(submodule[len - 1]))
		len--;
	if (!len)
		return NULL;

	if (submodule[len])
		/* We need to strip off one or more trailing slashes */
		submodule = to_free = xmemdupz(submodule, len);

	refs = lookup_ref_store_map(&repo->submodule_ref_stores, submodule);
	if (refs)
		goto done;

	strbuf_addstr(&submodule_sb, submodule);
	if (!is_nonbare_repository_dir(&submodule_sb))
		goto done;

	if (submodule_to_gitdir(repo, &submodule_sb, submodule))
		goto done;

	subrepo = xmalloc(sizeof(*subrepo));

	if (repo_submodule_init(subrepo, repo, submodule,
				null_oid(the_hash_algo))) {
		free(subrepo);
		goto done;
	}
	refs = ref_store_init(subrepo, subrepo->ref_storage_format,
			      submodule_sb.buf,
			      REF_STORE_READ | REF_STORE_ODB);
	register_ref_store_map(&repo->submodule_ref_stores, "submodule",
			       refs, submodule);

done:
	strbuf_release(&submodule_sb);
	free(to_free);

	return refs;
}

struct ref_store *get_worktree_ref_store(const struct worktree *wt)
{
	struct ref_store *refs;
	const char *id;

	if (wt->is_current)
		return get_main_ref_store(wt->repo);

	id = wt->id ? wt->id : "/";
	refs = lookup_ref_store_map(&wt->repo->worktree_ref_stores, id);
	if (refs)
		return refs;

	if (wt->id) {
		struct strbuf common_path = STRBUF_INIT;
		repo_common_path_append(wt->repo, &common_path,
					"worktrees/%s", wt->id);
		refs = ref_store_init(wt->repo, wt->repo->ref_storage_format,
				      common_path.buf, REF_STORE_ALL_CAPS);
		strbuf_release(&common_path);
	} else {
		refs = ref_store_init(wt->repo, wt->repo->ref_storage_format,
				      wt->repo->commondir, REF_STORE_ALL_CAPS);
	}

	if (refs)
		register_ref_store_map(&wt->repo->worktree_ref_stores,
				       "worktree", refs, id);

	return refs;
}

void base_ref_store_init(struct ref_store *refs, struct repository *repo,
			 const char *path, const struct ref_storage_be *be)
{
	refs->be = be;
	refs->repo = repo;
	refs->gitdir = xstrdup(path);
}

/* backend functions */
int refs_pack_refs(struct ref_store *refs, struct pack_refs_opts *opts)
{
	return refs->be->pack_refs(refs, opts);
}

int peel_iterated_oid(struct repository *r, const struct object_id *base, struct object_id *peeled)
{
	if (current_ref_iter &&
	    (current_ref_iter->oid == base ||
	     oideq(current_ref_iter->oid, base)))
		return ref_iterator_peel(current_ref_iter, peeled);

	return peel_object(r, base, peeled) ? -1 : 0;
}

int refs_update_symref(struct ref_store *refs, const char *ref,
		       const char *target, const char *logmsg)
{
	return refs_update_symref_extended(refs, ref, target, logmsg, NULL, 0);
}

int refs_update_symref_extended(struct ref_store *refs, const char *ref,
		       const char *target, const char *logmsg,
		       struct strbuf *referent, int create_only)
{
	struct ref_transaction *transaction;
	struct strbuf err = STRBUF_INIT;
	int ret = 0, prepret = 0;

	transaction = ref_store_transaction_begin(refs, 0, &err);
	if (!transaction) {
	error_return:
		ret = error("%s", err.buf);
		goto cleanup;
	}
	if (create_only) {
		if (ref_transaction_create(transaction, ref, NULL, target,
					   REF_NO_DEREF, logmsg, &err))
			goto error_return;
		prepret = ref_transaction_prepare(transaction, &err);
		if (prepret && prepret != REF_TRANSACTION_ERROR_CREATE_EXISTS)
			goto error_return;
	} else {
		if (ref_transaction_update(transaction, ref, NULL, NULL,
					   target, NULL, REF_NO_DEREF,
					   logmsg, &err) ||
			ref_transaction_prepare(transaction, &err))
			goto error_return;
	}

	if (referent && refs_read_symbolic_ref(refs, ref, referent) == NOT_A_SYMREF) {
		struct object_id oid;
		if (!refs_read_ref(refs, ref, &oid)) {
			strbuf_addstr(referent, oid_to_hex(&oid));
			ret = NOT_A_SYMREF;
		}
	}

	if (prepret == REF_TRANSACTION_ERROR_CREATE_EXISTS)
		goto cleanup;

	if (ref_transaction_commit(transaction, &err))
		goto error_return;

cleanup:
	strbuf_release(&err);
	if (transaction)
		ref_transaction_free(transaction);

	return ret;
}

/*
 * Write an error to `err` and return a nonzero value iff the same
 * refname appears multiple times in `refnames`. `refnames` must be
 * sorted on entry to this function.
 */
static int ref_update_reject_duplicates(struct string_list *refnames,
					struct strbuf *err)
{
	size_t i, n = refnames->nr;

	assert(err);

	for (i = 1; i < n; i++) {
		int cmp = strcmp(refnames->items[i - 1].string,
				 refnames->items[i].string);

		if (!cmp) {
			strbuf_addf(err,
				    _("multiple updates for ref '%s' not allowed"),
				    refnames->items[i].string);
			return 1;
		} else if (cmp > 0) {
			BUG("ref_update_reject_duplicates() received unsorted list");
		}
	}
	return 0;
}

static int run_transaction_hook(struct ref_transaction *transaction,
				const char *state)
{
	struct child_process proc = CHILD_PROCESS_INIT;
	struct strbuf buf = STRBUF_INIT;
	const char *hook;
	int ret = 0, i;

	hook = find_hook(transaction->ref_store->repo, "reference-transaction");
	if (!hook)
		return ret;

	strvec_pushl(&proc.args, hook, state, NULL);
	proc.in = -1;
	proc.stdout_to_stderr = 1;
	proc.trace2_hook_name = "reference-transaction";

	ret = start_command(&proc);
	if (ret)
		return ret;

	sigchain_push(SIGPIPE, SIG_IGN);

	for (i = 0; i < transaction->nr; i++) {
		struct ref_update *update = transaction->updates[i];

		if (update->flags & REF_LOG_ONLY)
			continue;

		strbuf_reset(&buf);

		if (!(update->flags & REF_HAVE_OLD))
			strbuf_addf(&buf, "%s ", oid_to_hex(null_oid(the_hash_algo)));
		else if (update->old_target)
			strbuf_addf(&buf, "ref:%s ", update->old_target);
		else
			strbuf_addf(&buf, "%s ", oid_to_hex(&update->old_oid));

		if (!(update->flags & REF_HAVE_NEW))
			strbuf_addf(&buf, "%s ", oid_to_hex(null_oid(the_hash_algo)));
		else if (update->new_target)
			strbuf_addf(&buf, "ref:%s ", update->new_target);
		else
			strbuf_addf(&buf, "%s ", oid_to_hex(&update->new_oid));

		strbuf_addf(&buf, "%s\n", update->refname);

		if (write_in_full(proc.in, buf.buf, buf.len) < 0) {
			if (errno != EPIPE) {
				/* Don't leak errno outside this API */
				errno = 0;
				ret = -1;
			}
			break;
		}
	}

	close(proc.in);
	sigchain_pop(SIGPIPE);
	strbuf_release(&buf);

	ret |= finish_command(&proc);
	return ret;
}

int ref_transaction_prepare(struct ref_transaction *transaction,
			    struct strbuf *err)
{
	struct ref_store *refs = transaction->ref_store;
	int ret;

	switch (transaction->state) {
	case REF_TRANSACTION_OPEN:
		/* Good. */
		break;
	case REF_TRANSACTION_PREPARED:
		BUG("prepare called twice on reference transaction");
		break;
	case REF_TRANSACTION_CLOSED:
		BUG("prepare called on a closed reference transaction");
		break;
	default:
		BUG("unexpected reference transaction state");
		break;
	}

	if (refs->repo->objects->sources->disable_ref_updates) {
		strbuf_addstr(err,
			      _("ref updates forbidden inside quarantine environment"));
		return -1;
	}

	string_list_sort(&transaction->refnames);
	if (ref_update_reject_duplicates(&transaction->refnames, err))
		return REF_TRANSACTION_ERROR_GENERIC;

	ret = refs->be->transaction_prepare(refs, transaction, err);
	if (ret)
		return ret;

	ret = run_transaction_hook(transaction, "prepared");
	if (ret) {
		ref_transaction_abort(transaction, err);
		die(_("ref updates aborted by hook"));
	}

	return 0;
}

int ref_transaction_abort(struct ref_transaction *transaction,
			  struct strbuf *err)
{
	struct ref_store *refs = transaction->ref_store;
	int ret = 0;

	switch (transaction->state) {
	case REF_TRANSACTION_OPEN:
		/* No need to abort explicitly. */
		break;
	case REF_TRANSACTION_PREPARED:
		ret = refs->be->transaction_abort(refs, transaction, err);
		break;
	case REF_TRANSACTION_CLOSED:
		BUG("abort called on a closed reference transaction");
		break;
	default:
		BUG("unexpected reference transaction state");
		break;
	}

	run_transaction_hook(transaction, "aborted");

	ref_transaction_free(transaction);
	return ret;
}

int ref_transaction_commit(struct ref_transaction *transaction,
			   struct strbuf *err)
{
	struct ref_store *refs = transaction->ref_store;
	int ret;

	switch (transaction->state) {
	case REF_TRANSACTION_OPEN:
		/* Need to prepare first. */
		ret = ref_transaction_prepare(transaction, err);
		if (ret)
			return ret;
		break;
	case REF_TRANSACTION_PREPARED:
		/* Fall through to finish. */
		break;
	case REF_TRANSACTION_CLOSED:
		BUG("commit called on a closed reference transaction");
		break;
	default:
		BUG("unexpected reference transaction state");
		break;
	}

	ret = refs->be->transaction_finish(refs, transaction, err);
	if (!ret && !(transaction->flags & REF_TRANSACTION_FLAG_INITIAL))
		run_transaction_hook(transaction, "committed");
	return ret;
}

enum ref_transaction_error refs_verify_refnames_available(struct ref_store *refs,
					  const struct string_list *refnames,
					  const struct string_list *extras,
					  const struct string_list *skip,
					  struct ref_transaction *transaction,
					  unsigned int initial_transaction,
					  struct strbuf *err)
{
	struct strbuf dirname = STRBUF_INIT;
	struct strbuf referent = STRBUF_INIT;
	struct string_list_item *item;
	struct ref_iterator *iter = NULL;
	struct strset conflicting_dirnames;
	struct strset dirnames;
	int ret = REF_TRANSACTION_ERROR_NAME_CONFLICT;

	/*
	 * For the sake of comments in this function, suppose that
	 * refname is "refs/foo/bar".
	 */

	assert(err);

	strset_init(&conflicting_dirnames);
	strset_init(&dirnames);

	for_each_string_list_item(item, refnames) {
		const size_t *update_idx = (size_t *)item->util;
		const char *refname = item->string;
		const char *extra_refname;
		struct object_id oid;
		unsigned int type;
		const char *slash;

		strbuf_reset(&dirname);

		for (slash = strchr(refname, '/'); slash; slash = strchr(slash + 1, '/')) {
			/*
			 * Just saying "Is a directory" when we e.g. can't
			 * lock some multi-level ref isn't very informative,
			 * the user won't be told *what* is a directory, so
			 * let's not use strerror() below.
			 */
			int ignore_errno;

			/* Expand dirname to the new prefix, not including the trailing slash: */
			strbuf_add(&dirname, refname + dirname.len, slash - refname - dirname.len);

			/*
			 * We are still at a leading dir of the refname (e.g.,
			 * "refs/foo"; if there is a reference with that name,
			 * it is a conflict, *unless* it is in skip.
			 */
			if (skip && string_list_has_string(skip, dirname.buf))
				continue;

			/*
			 * If we've already seen the directory we don't need to
			 * process it again. Skip it to avoid checking common
			 * prefixes like "refs/heads/" repeatedly.
			 */
			if (!strset_add(&dirnames, dirname.buf))
				continue;

			if (!initial_transaction &&
			    (strset_contains(&conflicting_dirnames, dirname.buf) ||
			     !refs_read_raw_ref(refs, dirname.buf, &oid, &referent,
						       &type, &ignore_errno))) {
				if (transaction && ref_transaction_maybe_set_rejected(
					    transaction, *update_idx,
					    REF_TRANSACTION_ERROR_NAME_CONFLICT)) {
					strset_remove(&dirnames, dirname.buf);
					strset_add(&conflicting_dirnames, dirname.buf);
					continue;
				}

				strbuf_addf(err, _("'%s' exists; cannot create '%s'"),
					    dirname.buf, refname);
				goto cleanup;
			}

			if (extras && string_list_has_string(extras, dirname.buf)) {
				if (transaction && ref_transaction_maybe_set_rejected(
					    transaction, *update_idx,
					    REF_TRANSACTION_ERROR_NAME_CONFLICT)) {
					strset_remove(&dirnames, dirname.buf);
					continue;
				}

				strbuf_addf(err, _("cannot process '%s' and '%s' at the same time"),
					    refname, dirname.buf);
				goto cleanup;
			}
		}

		/*
		 * We are at the leaf of our refname (e.g., "refs/foo/bar").
		 * There is no point in searching for a reference with that
		 * name, because a refname isn't considered to conflict with
		 * itself. But we still need to check for references whose
		 * names are in the "refs/foo/bar/" namespace, because they
		 * *do* conflict.
		 */
		strbuf_addstr(&dirname, refname + dirname.len);
		strbuf_addch(&dirname, '/');

		if (!initial_transaction) {
			int ok;

			if (!iter) {
				iter = refs_ref_iterator_begin(refs, dirname.buf, NULL, 0,
							       DO_FOR_EACH_INCLUDE_BROKEN);
			} else if (ref_iterator_seek(iter, dirname.buf) < 0) {
				goto cleanup;
			}

			while ((ok = ref_iterator_advance(iter)) == ITER_OK) {
				if (skip &&
				    string_list_has_string(skip, iter->refname))
					continue;

				if (transaction && ref_transaction_maybe_set_rejected(
					    transaction, *update_idx,
					    REF_TRANSACTION_ERROR_NAME_CONFLICT))
					continue;

				strbuf_addf(err, _("'%s' exists; cannot create '%s'"),
					    iter->refname, refname);
				goto cleanup;
			}

			if (ok != ITER_DONE)
				BUG("error while iterating over references");
		}

		extra_refname = find_descendant_ref(dirname.buf, extras, skip);
		if (extra_refname) {
			if (transaction && ref_transaction_maybe_set_rejected(
				    transaction, *update_idx,
				    REF_TRANSACTION_ERROR_NAME_CONFLICT))
				continue;

			strbuf_addf(err, _("cannot process '%s' and '%s' at the same time"),
				    refname, extra_refname);
			goto cleanup;
		}
	}

	ret = 0;

cleanup:
	strbuf_release(&referent);
	strbuf_release(&dirname);
	strset_clear(&conflicting_dirnames);
	strset_clear(&dirnames);
	ref_iterator_free(iter);
	return ret;
}

enum ref_transaction_error refs_verify_refname_available(
	struct ref_store *refs,
	const char *refname,
	const struct string_list *extras,
	const struct string_list *skip,
	unsigned int initial_transaction,
	struct strbuf *err)
{
	struct string_list_item item = { .string = (char *) refname };
	struct string_list refnames = {
		.items = &item,
		.nr = 1,
	};

	return refs_verify_refnames_available(refs, &refnames, extras, skip,
					      NULL, initial_transaction, err);
}

struct do_for_each_reflog_help {
	each_reflog_fn *fn;
	void *cb_data;
};

static int do_for_each_reflog_helper(const char *refname,
				     const char *referent UNUSED,
				     const struct object_id *oid UNUSED,
				     int flags UNUSED,
				     void *cb_data)
{
	struct do_for_each_reflog_help *hp = cb_data;
	return hp->fn(refname, hp->cb_data);
}

int refs_for_each_reflog(struct ref_store *refs, each_reflog_fn fn, void *cb_data)
{
	struct ref_iterator *iter;
	struct do_for_each_reflog_help hp = { fn, cb_data };

	iter = refs->be->reflog_iterator_begin(refs);

	return do_for_each_ref_iterator(iter, do_for_each_reflog_helper, &hp);
}

int refs_for_each_reflog_ent_reverse(struct ref_store *refs,
				     const char *refname,
				     each_reflog_ent_fn fn,
				     void *cb_data)
{
	return refs->be->for_each_reflog_ent_reverse(refs, refname,
						     fn, cb_data);
}

int refs_for_each_reflog_ent(struct ref_store *refs, const char *refname,
			     each_reflog_ent_fn fn, void *cb_data)
{
	return refs->be->for_each_reflog_ent(refs, refname, fn, cb_data);
}

int refs_reflog_exists(struct ref_store *refs, const char *refname)
{
	return refs->be->reflog_exists(refs, refname);
}

int refs_create_reflog(struct ref_store *refs, const char *refname,
		       struct strbuf *err)
{
	return refs->be->create_reflog(refs, refname, err);
}

int refs_delete_reflog(struct ref_store *refs, const char *refname)
{
	return refs->be->delete_reflog(refs, refname);
}

int refs_reflog_expire(struct ref_store *refs,
		       const char *refname,
		       unsigned int flags,
		       reflog_expiry_prepare_fn prepare_fn,
		       reflog_expiry_should_prune_fn should_prune_fn,
		       reflog_expiry_cleanup_fn cleanup_fn,
		       void *policy_cb_data)
{
	return refs->be->reflog_expire(refs, refname, flags,
				       prepare_fn, should_prune_fn,
				       cleanup_fn, policy_cb_data);
}

void ref_transaction_for_each_queued_update(struct ref_transaction *transaction,
					    ref_transaction_for_each_queued_update_fn cb,
					    void *cb_data)
{
	int i;

	for (i = 0; i < transaction->nr; i++) {
		struct ref_update *update = transaction->updates[i];

		cb(update->refname,
		   (update->flags & REF_HAVE_OLD) ? &update->old_oid : NULL,
		   (update->flags & REF_HAVE_NEW) ? &update->new_oid : NULL,
		   cb_data);
	}
}

void ref_transaction_for_each_rejected_update(struct ref_transaction *transaction,
					      ref_transaction_for_each_rejected_update_fn cb,
					      void *cb_data)
{
	if (!transaction->rejections)
		return;

	for (size_t i = 0; i < transaction->rejections->nr; i++) {
		size_t update_index = transaction->rejections->update_indices[i];
		struct ref_update *update = transaction->updates[update_index];

		if (!update->rejection_err)
			continue;

		cb(update->refname,
		   (update->flags & REF_HAVE_OLD) ? &update->old_oid : NULL,
		   (update->flags & REF_HAVE_NEW) ? &update->new_oid : NULL,
		   update->old_target, update->new_target,
		   update->rejection_err, cb_data);
	}
}

int refs_delete_refs(struct ref_store *refs, const char *logmsg,
		     struct string_list *refnames, unsigned int flags)
{
	struct ref_transaction *transaction;
	struct strbuf err = STRBUF_INIT;
	struct string_list_item *item;
	int ret = 0, failures = 0;
	char *msg;

	if (!refnames->nr)
		return 0;

	msg = normalize_reflog_message(logmsg);

	/*
	 * Since we don't check the references' old_oids, the
	 * individual updates can't fail, so we can pack all of the
	 * updates into a single transaction.
	 */
	transaction = ref_store_transaction_begin(refs, 0, &err);
	if (!transaction) {
		ret = error("%s", err.buf);
		goto out;
	}

	for_each_string_list_item(item, refnames) {
		ret = ref_transaction_delete(transaction, item->string,
					     NULL, NULL, flags, msg, &err);
		if (ret) {
			warning(_("could not delete reference %s: %s"),
				item->string, err.buf);
			strbuf_reset(&err);
			failures = 1;
		}
	}

	ret = ref_transaction_commit(transaction, &err);
	if (ret) {
		if (refnames->nr == 1)
			error(_("could not delete reference %s: %s"),
			      refnames->items[0].string, err.buf);
		else
			error(_("could not delete references: %s"), err.buf);
	}

out:
	if (!ret && failures)
		ret = -1;
	ref_transaction_free(transaction);
	strbuf_release(&err);
	free(msg);
	return ret;
}

int refs_rename_ref(struct ref_store *refs, const char *oldref,
		    const char *newref, const char *logmsg)
{
	char *msg;
	int retval;

	msg = normalize_reflog_message(logmsg);
	retval = refs->be->rename_ref(refs, oldref, newref, msg);
	free(msg);
	return retval;
}

int refs_copy_existing_ref(struct ref_store *refs, const char *oldref,
		    const char *newref, const char *logmsg)
{
	char *msg;
	int retval;

	msg = normalize_reflog_message(logmsg);
	retval = refs->be->copy_ref(refs, oldref, newref, msg);
	free(msg);
	return retval;
}

const char *ref_update_original_update_refname(struct ref_update *update)
{
	while (update->parent_update)
		update = update->parent_update;

	return update->refname;
}

int ref_update_has_null_new_value(struct ref_update *update)
{
	return !update->new_target && is_null_oid(&update->new_oid);
}

enum ref_transaction_error ref_update_check_old_target(const char *referent,
						       struct ref_update *update,
						       struct strbuf *err)
{
	if (!update->old_target)
		BUG("called without old_target set");

	if (!strcmp(referent, update->old_target))
		return 0;

	if (!strcmp(referent, "")) {
		strbuf_addf(err, "verifying symref target: '%s': "
			    "reference is missing but expected %s",
			    ref_update_original_update_refname(update),
			    update->old_target);
		return REF_TRANSACTION_ERROR_NONEXISTENT_REF;
	}

	strbuf_addf(err, "verifying symref target: '%s': is at %s but expected %s",
			    ref_update_original_update_refname(update),
			    referent, update->old_target);
	return REF_TRANSACTION_ERROR_INCORRECT_OLD_VALUE;
}

struct migration_data {
	struct ref_store *old_refs;
	struct ref_transaction *transaction;
	struct strbuf *errbuf;
	struct strbuf sb;
};

static int migrate_one_ref(const char *refname, const char *referent UNUSED, const struct object_id *oid,
			   int flags, void *cb_data)
{
	struct migration_data *data = cb_data;
	struct strbuf symref_target = STRBUF_INIT;
	int ret;

	if (flags & REF_ISSYMREF) {
		ret = refs_read_symbolic_ref(data->old_refs, refname, &symref_target);
		if (ret < 0)
			goto done;

		ret = ref_transaction_update(data->transaction, refname, NULL, null_oid(the_hash_algo),
					     symref_target.buf, NULL,
					     REF_SKIP_CREATE_REFLOG | REF_NO_DEREF, NULL, data->errbuf);
		if (ret < 0)
			goto done;
	} else {
		ret = ref_transaction_create(data->transaction, refname, oid, NULL,
					     REF_SKIP_CREATE_REFLOG | REF_SKIP_OID_VERIFICATION,
					     NULL, data->errbuf);
		if (ret < 0)
			goto done;
	}

done:
	strbuf_release(&symref_target);
	return ret;
}

struct reflog_migration_data {
	uint64_t index;
	const char *refname;
	struct ref_store *old_refs;
	struct ref_transaction *transaction;
	struct strbuf *errbuf;
	struct strbuf *sb;
};

static int migrate_one_reflog_entry(struct object_id *old_oid,
				    struct object_id *new_oid,
				    const char *committer,
				    timestamp_t timestamp, int tz,
				    const char *msg, void *cb_data)
{
	struct reflog_migration_data *data = cb_data;
	const char *date;
	int ret;

	date = show_date(timestamp, tz, DATE_MODE(NORMAL));
	strbuf_reset(data->sb);
	/* committer contains name and email */
	strbuf_addstr(data->sb, fmt_ident("", committer, WANT_BLANK_IDENT, date, 0));

	ret = ref_transaction_update_reflog(data->transaction, data->refname,
					    new_oid, old_oid, data->sb->buf,
					    REF_HAVE_NEW | REF_HAVE_OLD, msg,
					    data->index++, data->errbuf);
	return ret;
}

static int migrate_one_reflog(const char *refname, void *cb_data)
{
	struct migration_data *migration_data = cb_data;
	struct reflog_migration_data data = {
		.refname = refname,
		.old_refs = migration_data->old_refs,
		.transaction = migration_data->transaction,
		.errbuf = migration_data->errbuf,
		.sb = &migration_data->sb,
	};

	return refs_for_each_reflog_ent(migration_data->old_refs, refname,
					migrate_one_reflog_entry, &data);
}

static int move_files(const char *from_path, const char *to_path, struct strbuf *errbuf)
{
	struct strbuf from_buf = STRBUF_INIT, to_buf = STRBUF_INIT;
	size_t from_len, to_len;
	DIR *from_dir;
	int ret;

	from_dir = opendir(from_path);
	if (!from_dir) {
		strbuf_addf(errbuf, "could not open source directory '%s': %s",
			    from_path, strerror(errno));
		ret = -1;
		goto done;
	}

	strbuf_addstr(&from_buf, from_path);
	strbuf_complete(&from_buf, '/');
	from_len = from_buf.len;

	strbuf_addstr(&to_buf, to_path);
	strbuf_complete(&to_buf, '/');
	to_len = to_buf.len;

	while (1) {
		struct dirent *ent;

		errno = 0;
		ent = readdir(from_dir);
		if (!ent)
			break;

		if (!strcmp(ent->d_name, ".") ||
		    !strcmp(ent->d_name, ".."))
			continue;

		strbuf_setlen(&from_buf, from_len);
		strbuf_addstr(&from_buf, ent->d_name);

		strbuf_setlen(&to_buf, to_len);
		strbuf_addstr(&to_buf, ent->d_name);

		ret = rename(from_buf.buf, to_buf.buf);
		if (ret < 0) {
			strbuf_addf(errbuf, "could not link file '%s' to '%s': %s",
				    from_buf.buf, to_buf.buf, strerror(errno));
			goto done;
		}
	}

	if (errno) {
		strbuf_addf(errbuf, "could not read entry from directory '%s': %s",
			    from_path, strerror(errno));
		ret = -1;
		goto done;
	}

	ret = 0;

done:
	strbuf_release(&from_buf);
	strbuf_release(&to_buf);
	if (from_dir)
		closedir(from_dir);
	return ret;
}

static int has_worktrees(void)
{
	struct worktree **worktrees = get_worktrees();
	int ret = 0;
	size_t i;

	for (i = 0; worktrees[i]; i++) {
		if (is_main_worktree(worktrees[i]))
			continue;
		ret = 1;
	}

	free_worktrees(worktrees);
	return ret;
}

int repo_migrate_ref_storage_format(struct repository *repo,
				    enum ref_storage_format format,
				    unsigned int flags,
				    struct strbuf *errbuf)
{
	struct ref_store *old_refs = NULL, *new_refs = NULL;
	struct ref_transaction *transaction = NULL;
	struct strbuf new_gitdir = STRBUF_INIT;
	struct migration_data data = {
		.sb = STRBUF_INIT,
	};
	int did_migrate_refs = 0;
	int ret;

	if (repo->ref_storage_format == format) {
		strbuf_addstr(errbuf, "current and new ref storage format are equal");
		ret = -1;
		goto done;
	}

	old_refs = get_main_ref_store(repo);

	/*
	 * Worktrees complicate the migration because every worktree has a
	 * separate ref storage. While it should be feasible to implement, this
	 * is pushed out to a future iteration.
	 *
	 * TODO: we should really be passing the caller-provided repository to
	 * `has_worktrees()`, but our worktree subsystem doesn't yet support
	 * that.
	 */
	if (has_worktrees()) {
		strbuf_addstr(errbuf, "migrating repositories with worktrees is not supported yet");
		ret = -1;
		goto done;
	}

	/*
	 * The overall logic looks like this:
	 *
	 *   1. Set up a new temporary directory and initialize it with the new
	 *      format. This is where all refs will be migrated into.
	 *
	 *   2. Enumerate all refs and write them into the new ref storage.
	 *      This operation is safe as we do not yet modify the main
	 *      repository.
	 *
	 *   3. Enumerate all reflogs and write them into the new ref storage.
	 *      This operation is safe as we do not yet modify the main
	 *      repository.
	 *
	 *   4. If we're in dry-run mode then we are done and can hand over the
	 *      directory to the caller for inspection. If not, we now start
	 *      with the destructive part.
	 *
	 *   5. Delete the old ref storage from disk. As we have a copy of refs
	 *      in the new ref storage it's okay(ish) if we now get interrupted
	 *      as there is an equivalent copy of all refs available.
	 *
	 *   6. Move the new ref storage files into place.
	 *
	 *  7. Change the repository format to the new ref format.
	 */
	strbuf_addf(&new_gitdir, "%s/%s", old_refs->gitdir, "ref_migration.XXXXXX");
	if (!mkdtemp(new_gitdir.buf)) {
		strbuf_addf(errbuf, "cannot create migration directory: %s",
			    strerror(errno));
		ret = -1;
		goto done;
	}

	new_refs = ref_store_init(repo, format, new_gitdir.buf,
				  REF_STORE_ALL_CAPS);
	ret = ref_store_create_on_disk(new_refs, 0, errbuf);
	if (ret < 0)
		goto done;

	transaction = ref_store_transaction_begin(new_refs, REF_TRANSACTION_FLAG_INITIAL,
						  errbuf);
	if (!transaction)
		goto done;

	data.old_refs = old_refs;
	data.transaction = transaction;
	data.errbuf = errbuf;

	/*
	 * We need to use the internal `do_for_each_ref()` here so that we can
	 * also include broken refs and symrefs. These would otherwise be
	 * skipped silently.
	 *
	 * Ideally, we would do this call while locking the old ref storage
	 * such that there cannot be any concurrent modifications. We do not
	 * have the infra for that though, and the "files" backend does not
	 * allow for a central lock due to its design. It's thus on the user to
	 * ensure that there are no concurrent writes.
	 */
	ret = do_for_each_ref(old_refs, "", NULL, migrate_one_ref, 0,
			      DO_FOR_EACH_INCLUDE_ROOT_REFS | DO_FOR_EACH_INCLUDE_BROKEN,
			      &data);
	if (ret < 0)
		goto done;

	if (!(flags & REPO_MIGRATE_REF_STORAGE_FORMAT_SKIP_REFLOG)) {
		ret = refs_for_each_reflog(old_refs, migrate_one_reflog, &data);
		if (ret < 0)
			goto done;
	}

	ret = ref_transaction_commit(transaction, errbuf);
	if (ret < 0)
		goto done;
	did_migrate_refs = 1;

	if (flags & REPO_MIGRATE_REF_STORAGE_FORMAT_DRYRUN) {
		printf(_("Finished dry-run migration of refs, "
			 "the result can be found at '%s'\n"), new_gitdir.buf);
		ret = 0;
		goto done;
	}

	/*
	 * Release the new ref store such that any potentially-open files will
	 * be closed. This is required for platforms like Cygwin, where
	 * renaming an open file results in EPERM.
	 */
	ref_store_release(new_refs);
	FREE_AND_NULL(new_refs);

	/*
	 * Until now we were in the non-destructive phase, where we only
	 * populated the new ref store. From hereon though we are about
	 * to get hands by deleting the old ref store and then moving
	 * the new one into place.
	 *
	 * Assuming that there were no concurrent writes, the new ref
	 * store should have all information. So if we fail from hereon
	 * we may be in an in-between state, but it would still be able
	 * to recover by manually moving remaining files from the
	 * temporary migration directory into place.
	 */
	ret = ref_store_remove_on_disk(old_refs, errbuf);
	if (ret < 0)
		goto done;

	ret = move_files(new_gitdir.buf, old_refs->gitdir, errbuf);
	if (ret < 0)
		goto done;

	if (rmdir(new_gitdir.buf) < 0)
		warning_errno(_("could not remove temporary migration directory '%s'"),
			      new_gitdir.buf);

	/*
	 * We have migrated the repository, so we now need to adjust the
	 * repository format so that clients will use the new ref store.
	 * We also need to swap out the repository's main ref store.
	 */
	initialize_repository_version(hash_algo_by_ptr(repo->hash_algo), format, 1);

	/*
	 * Unset the old ref store and release it. `get_main_ref_store()` will
	 * make sure to lazily re-initialize the repository's ref store with
	 * the new format.
	 */
	ref_store_release(old_refs);
	FREE_AND_NULL(old_refs);
	repo->refs_private = NULL;

	ret = 0;

done:
	if (ret && did_migrate_refs) {
		strbuf_complete(errbuf, '\n');
		strbuf_addf(errbuf, _("migrated refs can be found at '%s'"),
			    new_gitdir.buf);
	}

	if (new_refs) {
		ref_store_release(new_refs);
		free(new_refs);
	}
	ref_transaction_free(transaction);
	strbuf_release(&new_gitdir);
	strbuf_release(&data.sb);
	return ret;
}

int ref_update_expects_existing_old_ref(struct ref_update *update)
{
	return (update->flags & REF_HAVE_OLD) &&
		(!is_null_oid(&update->old_oid) || update->old_target);
}

const char *ref_transaction_error_msg(enum ref_transaction_error err)
{
	switch (err) {
	case REF_TRANSACTION_ERROR_NAME_CONFLICT:
		return "refname conflict";
	case REF_TRANSACTION_ERROR_CREATE_EXISTS:
		return "reference already exists";
	case REF_TRANSACTION_ERROR_NONEXISTENT_REF:
		return "reference does not exist";
	case REF_TRANSACTION_ERROR_INCORRECT_OLD_VALUE:
		return "incorrect old value provided";
	case REF_TRANSACTION_ERROR_INVALID_NEW_VALUE:
		return "invalid new value provided";
	case REF_TRANSACTION_ERROR_EXPECTED_SYMREF:
		return "expected symref but found regular ref";
	default:
		return "unknown failure";
	}
}
