/*
 * The backend-independent part of the reference module.
 */

#include "cache.h"
#include "config.h"
#include "hashmap.h"
#include "lockfile.h"
#include "iterator.h"
#include "refs.h"
#include "refs/refs-internal.h"
#include "run-command.h"
#include "object-store.h"
#include "object.h"
#include "tag.h"
#include "submodule.h"
#include "worktree.h"
#include "strvec.h"
#include "repository.h"
#include "sigchain.h"

/*
 * List of all available backends
 */
static struct ref_storage_be *refs_backends = &refs_be_files;

static struct ref_storage_be *find_ref_storage_backend(const char *name)
{
	struct ref_storage_be *be;
	for (be = refs_backends; be; be = be->next)
		if (!strcmp(be->name, name))
			return be;
	return NULL;
}

int ref_storage_backend_exists(const char *name)
{
	return find_ref_storage_backend(name) != NULL;
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
			   const struct object_id *oid,
			   unsigned int flags)
{
	if (flags & REF_ISBROKEN)
		return 0;
	if (!has_object_file(oid)) {
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

char *resolve_refdup(const char *refname, int resolve_flags,
		     struct object_id *oid, int *flags)
{
	return refs_resolve_refdup(get_main_ref_store(the_repository),
				   refname, resolve_flags,
				   oid, flags);
}

/* The argument to filter_refs */
struct ref_filter {
	const char *pattern;
	const char *prefix;
	each_ref_fn *fn;
	void *cb_data;
};

int refs_read_ref_full(struct ref_store *refs, const char *refname,
		       int resolve_flags, struct object_id *oid, int *flags)
{
	if (refs_resolve_ref_unsafe(refs, refname, resolve_flags, oid, flags))
		return 0;
	return -1;
}

int read_ref_full(const char *refname, int resolve_flags, struct object_id *oid, int *flags)
{
	return refs_read_ref_full(get_main_ref_store(the_repository), refname,
				  resolve_flags, oid, flags);
}

int read_ref(const char *refname, struct object_id *oid)
{
	return read_ref_full(refname, RESOLVE_REF_READING, oid, NULL);
}

int refs_ref_exists(struct ref_store *refs, const char *refname)
{
	return !!refs_resolve_ref_unsafe(refs, refname, RESOLVE_REF_READING, NULL, NULL);
}

int ref_exists(const char *refname)
{
	return refs_ref_exists(get_main_ref_store(the_repository), refname);
}

static int filter_refs(const char *refname, const struct object_id *oid,
			   int flags, void *data)
{
	struct ref_filter *filter = (struct ref_filter *)data;

	if (wildmatch(filter->pattern, refname, 0))
		return 0;
	if (filter->prefix)
		skip_prefix(refname, filter->prefix, &refname);
	return filter->fn(refname, oid, flags, filter->cb_data);
}

enum peel_status peel_object(const struct object_id *name, struct object_id *oid)
{
	struct object *o = lookup_unknown_object(the_repository, name);

	if (o->type == OBJ_NONE) {
		int type = oid_object_info(the_repository, name, NULL);
		if (type < 0 || !object_as_type(o, type, 0))
			return PEEL_INVALID;
	}

	if (o->type != OBJ_TAG)
		return PEEL_NON_TAG;

	o = deref_tag_noverify(o);
	if (!o)
		return PEEL_INVALID;

	oidcpy(oid, &o->oid);
	return PEEL_PEELED;
}

struct warn_if_dangling_data {
	FILE *fp;
	const char *refname;
	const struct string_list *refnames;
	const char *msg_fmt;
};

static int warn_if_dangling_symref(const char *refname, const struct object_id *oid,
				   int flags, void *cb_data)
{
	struct warn_if_dangling_data *d = cb_data;
	const char *resolves_to;

	if (!(flags & REF_ISSYMREF))
		return 0;

	resolves_to = resolve_ref_unsafe(refname, 0, NULL, NULL);
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

void warn_dangling_symref(FILE *fp, const char *msg_fmt, const char *refname)
{
	struct warn_if_dangling_data data;

	data.fp = fp;
	data.refname = refname;
	data.refnames = NULL;
	data.msg_fmt = msg_fmt;
	for_each_rawref(warn_if_dangling_symref, &data);
}

void warn_dangling_symrefs(FILE *fp, const char *msg_fmt, const struct string_list *refnames)
{
	struct warn_if_dangling_data data;

	data.fp = fp;
	data.refname = NULL;
	data.refnames = refnames;
	data.msg_fmt = msg_fmt;
	for_each_rawref(warn_if_dangling_symref, &data);
}

int refs_for_each_tag_ref(struct ref_store *refs, each_ref_fn fn, void *cb_data)
{
	return refs_for_each_ref_in(refs, "refs/tags/", fn, cb_data);
}

int for_each_tag_ref(each_ref_fn fn, void *cb_data)
{
	return refs_for_each_tag_ref(get_main_ref_store(the_repository), fn, cb_data);
}

int refs_for_each_branch_ref(struct ref_store *refs, each_ref_fn fn, void *cb_data)
{
	return refs_for_each_ref_in(refs, "refs/heads/", fn, cb_data);
}

int for_each_branch_ref(each_ref_fn fn, void *cb_data)
{
	return refs_for_each_branch_ref(get_main_ref_store(the_repository), fn, cb_data);
}

int refs_for_each_remote_ref(struct ref_store *refs, each_ref_fn fn, void *cb_data)
{
	return refs_for_each_ref_in(refs, "refs/remotes/", fn, cb_data);
}

int for_each_remote_ref(each_ref_fn fn, void *cb_data)
{
	return refs_for_each_remote_ref(get_main_ref_store(the_repository), fn, cb_data);
}

int head_ref_namespaced(each_ref_fn fn, void *cb_data)
{
	struct strbuf buf = STRBUF_INIT;
	int ret = 0;
	struct object_id oid;
	int flag;

	strbuf_addf(&buf, "%sHEAD", get_git_namespace());
	if (!read_ref_full(buf.buf, RESOLVE_REF_READING, &oid, &flag))
		ret = fn(buf.buf, &oid, flag, cb_data);
	strbuf_release(&buf);

	return ret;
}

void normalize_glob_ref(struct string_list_item *item, const char *prefix,
			const char *pattern)
{
	struct strbuf normalized_pattern = STRBUF_INIT;

	if (*pattern == '/')
		BUG("pattern must not start with '/'");

	if (prefix) {
		strbuf_addstr(&normalized_pattern, prefix);
	}
	else if (!starts_with(pattern, "refs/"))
		strbuf_addstr(&normalized_pattern, "refs/");
	strbuf_addstr(&normalized_pattern, pattern);
	strbuf_strip_suffix(&normalized_pattern, "/");

	item->string = strbuf_detach(&normalized_pattern, NULL);
	item->util = has_glob_specials(pattern) ? NULL : item->string;
	strbuf_release(&normalized_pattern);
}

int for_each_glob_ref_in(each_ref_fn fn, const char *pattern,
	const char *prefix, void *cb_data)
{
	struct strbuf real_pattern = STRBUF_INIT;
	struct ref_filter filter;
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
	ret = for_each_ref(filter_refs, &filter);

	strbuf_release(&real_pattern);
	return ret;
}

int for_each_glob_ref(each_ref_fn fn, const char *pattern, void *cb_data)
{
	return for_each_glob_ref_in(fn, pattern, NULL, cb_data);
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
			advise(_(default_branch_name_advice), ret);
	}

	full_ref = xstrfmt("refs/heads/%s", ret);
	if (check_refname_format(full_ref, 0))
		die(_("invalid branch name: %s = %s"), config_display_key, ret);
	free(full_ref);

	return ret;
}

const char *git_default_branch_name(int quiet)
{
	static char *ret;

	if (!ret)
		ret = repo_default_branch_name(the_repository, quiet);

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

		this_result = refs_found ? &oid_from_ref : oid;
		strbuf_reset(&fullref);
		strbuf_addf(&fullref, *p, len, str);
		r = refs_resolve_ref_unsafe(get_main_ref_store(repo),
					    fullref.buf, RESOLVE_REF_READING,
					    this_result, &flag);
		if (r) {
			if (!refs_found++)
				*ref = xstrdup(r);
			if (!warn_ambiguous_refs)
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
					      &hash, NULL);
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
			oidcpy(oid, &hash);
		}
		if (!warn_ambiguous_refs)
			break;
	}
	strbuf_release(&path);
	free(last_branch);
	return logs_found;
}

int dwim_log(const char *str, int len, struct object_id *oid, char **log)
{
	return repo_dwim_log(the_repository, str, len, oid, log);
}

static int is_per_worktree_ref(const char *refname)
{
	return starts_with(refname, "refs/worktree/") ||
	       starts_with(refname, "refs/bisect/") ||
	       starts_with(refname, "refs/rewritten/");
}

static int is_pseudoref_syntax(const char *refname)
{
	const char *c;

	for (c = refname; *c; c++) {
		if (!isupper(*c) && *c != '-' && *c != '_')
			return 0;
	}

	return 1;
}

static int is_main_pseudoref_syntax(const char *refname)
{
	return skip_prefix(refname, "main-worktree/", &refname) &&
		*refname &&
		is_pseudoref_syntax(refname);
}

static int is_other_pseudoref_syntax(const char *refname)
{
	if (!skip_prefix(refname, "worktrees/", &refname))
		return 0;
	refname = strchr(refname, '/');
	if (!refname || !refname[1])
		return 0;
	return is_pseudoref_syntax(refname + 1);
}

enum ref_type ref_type(const char *refname)
{
	if (is_per_worktree_ref(refname))
		return REF_TYPE_PER_WORKTREE;
	if (is_pseudoref_syntax(refname))
		return REF_TYPE_PSEUDOREF;
	if (is_main_pseudoref_syntax(refname))
		return REF_TYPE_MAIN_PSEUDOREF;
	if (is_other_pseudoref_syntax(refname))
		return REF_TYPE_OTHER_PSEUDOREF;
	return REF_TYPE_NORMAL;
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

	transaction = ref_store_transaction_begin(refs, &err);
	if (!transaction ||
	    ref_transaction_delete(transaction, refname, old_oid,
				   flags, msg, &err) ||
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

int delete_ref(const char *msg, const char *refname,
	       const struct object_id *old_oid, unsigned int flags)
{
	return refs_delete_ref(get_main_ref_store(the_repository), msg, refname,
			       old_oid, flags);
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

int should_autocreate_reflog(const char *refname)
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
		const char *email, timestamp_t timestamp, int tz,
		const char *message, void *cb_data)
{
	struct read_ref_at_cb *cb = cb_data;
	int reached_count;

	cb->tz = tz;
	cb->date = timestamp;

	/*
	 * It is not possible for cb->cnt == 0 on the first iteration because
	 * that special case is handled in read_ref_at().
	 */
	if (cb->cnt > 0)
		cb->cnt--;
	reached_count = cb->cnt == 0 && !is_null_oid(ooid);
	if (timestamp <= cb->at_time || reached_count) {
		set_read_ref_cutoffs(cb, timestamp, tz, message);
		/*
		 * we have not yet updated cb->[n|o]oid so they still
		 * hold the values for the previous record.
		 */
		if (!is_null_oid(&cb->ooid) && !oideq(&cb->ooid, noid))
			warning(_("log for ref %s has gap after %s"),
					cb->refname, show_date(cb->date, cb->tz, DATE_MODE(RFC2822)));
		if (reached_count)
			oidcpy(cb->oid, ooid);
		else if (!is_null_oid(&cb->ooid) || cb->date == cb->at_time)
			oidcpy(cb->oid, noid);
		else if (!oideq(noid, cb->oid))
			warning(_("log for ref %s unexpectedly ended on %s"),
				cb->refname, show_date(cb->date, cb->tz,
						       DATE_MODE(RFC2822)));
		cb->found_it = 1;
	}
	cb->reccnt++;
	oidcpy(&cb->ooid, ooid);
	oidcpy(&cb->noid, noid);
	return cb->found_it;
}

static int read_ref_at_ent_newest(struct object_id *ooid, struct object_id *noid,
				  const char *email, timestamp_t timestamp,
				  int tz, const char *message, void *cb_data)
{
	struct read_ref_at_cb *cb = cb_data;

	set_read_ref_cutoffs(cb, timestamp, tz, message);
	oidcpy(cb->oid, noid);
	/* We just want the first entry */
	return 1;
}

static int read_ref_at_ent_oldest(struct object_id *ooid, struct object_id *noid,
				  const char *email, timestamp_t timestamp,
				  int tz, const char *message, void *cb_data)
{
	struct read_ref_at_cb *cb = cb_data;

	set_read_ref_cutoffs(cb, timestamp, tz, message);
	oidcpy(cb->oid, ooid);
	if (is_null_oid(cb->oid))
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

	if (cb.cnt == 0) {
		refs_for_each_reflog_ent_reverse(refs, refname, read_ref_at_ent_newest, &cb);
		return 0;
	}

	refs_for_each_reflog_ent_reverse(refs, refname, read_ref_at_ent, &cb);

	if (!cb.reccnt) {
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
						    struct strbuf *err)
{
	struct ref_transaction *tr;
	assert(err);

	CALLOC_ARRAY(tr, 1);
	tr->ref_store = refs;
	return tr;
}

struct ref_transaction *ref_transaction_begin(struct strbuf *err)
{
	return ref_store_transaction_begin(get_main_ref_store(the_repository), err);
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
		free(transaction->updates[i]);
	}
	free(transaction->updates);
	free(transaction);
}

struct ref_update *ref_transaction_add_update(
		struct ref_transaction *transaction,
		const char *refname, unsigned int flags,
		const struct object_id *new_oid,
		const struct object_id *old_oid,
		const char *msg)
{
	struct ref_update *update;

	if (transaction->state != REF_TRANSACTION_OPEN)
		BUG("update called for transaction that is not open");

	FLEX_ALLOC_STR(update, refname, refname);
	ALLOC_GROW(transaction->updates, transaction->nr + 1, transaction->alloc);
	transaction->updates[transaction->nr++] = update;

	update->flags = flags;

	if (flags & REF_HAVE_NEW)
		oidcpy(&update->new_oid, new_oid);
	if (flags & REF_HAVE_OLD)
		oidcpy(&update->old_oid, old_oid);
	update->msg = normalize_reflog_message(msg);
	return update;
}

int ref_transaction_update(struct ref_transaction *transaction,
			   const char *refname,
			   const struct object_id *new_oid,
			   const struct object_id *old_oid,
			   unsigned int flags, const char *msg,
			   struct strbuf *err)
{
	assert(err);

	if ((new_oid && !is_null_oid(new_oid)) ?
	    check_refname_format(refname, REFNAME_ALLOW_ONELEVEL) :
	    !refname_is_safe(refname)) {
		strbuf_addf(err, _("refusing to update ref with bad name '%s'"),
			    refname);
		return -1;
	}

	if (flags & ~REF_TRANSACTION_UPDATE_ALLOWED_FLAGS)
		BUG("illegal flags 0x%x passed to ref_transaction_update()", flags);

	flags |= (new_oid ? REF_HAVE_NEW : 0) | (old_oid ? REF_HAVE_OLD : 0);

	ref_transaction_add_update(transaction, refname, flags,
				   new_oid, old_oid, msg);
	return 0;
}

int ref_transaction_create(struct ref_transaction *transaction,
			   const char *refname,
			   const struct object_id *new_oid,
			   unsigned int flags, const char *msg,
			   struct strbuf *err)
{
	if (!new_oid || is_null_oid(new_oid))
		BUG("create called without valid new_oid");
	return ref_transaction_update(transaction, refname, new_oid,
				      null_oid(), flags, msg, err);
}

int ref_transaction_delete(struct ref_transaction *transaction,
			   const char *refname,
			   const struct object_id *old_oid,
			   unsigned int flags, const char *msg,
			   struct strbuf *err)
{
	if (old_oid && is_null_oid(old_oid))
		BUG("delete called with old_oid set to zeros");
	return ref_transaction_update(transaction, refname,
				      null_oid(), old_oid,
				      flags, msg, err);
}

int ref_transaction_verify(struct ref_transaction *transaction,
			   const char *refname,
			   const struct object_id *old_oid,
			   unsigned int flags,
			   struct strbuf *err)
{
	if (!old_oid)
		BUG("verify called with old_oid set to NULL");
	return ref_transaction_update(transaction, refname,
				      NULL, old_oid,
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

	t = ref_store_transaction_begin(refs, &err);
	if (!t ||
	    ref_transaction_update(t, refname, new_oid, old_oid, flags, msg,
				   &err) ||
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

int update_ref(const char *msg, const char *refname,
	       const struct object_id *new_oid,
	       const struct object_id *old_oid,
	       unsigned int flags, enum action_on_err onerr)
{
	return refs_update_ref(get_main_ref_store(the_repository), msg, refname, new_oid,
			       old_oid, flags, onerr);
}

char *refs_shorten_unambiguous_ref(struct ref_store *refs,
				   const char *refname, int strict)
{
	int i;
	static char **scanf_fmts;
	static int nr_rules;
	char *short_name;
	struct strbuf resolved_buf = STRBUF_INIT;

	if (!nr_rules) {
		/*
		 * Pre-generate scanf formats from ref_rev_parse_rules[].
		 * Generate a format suitable for scanf from a
		 * ref_rev_parse_rules rule by interpolating "%s" at the
		 * location of the "%.*s".
		 */
		size_t total_len = 0;
		size_t offset = 0;

		/* the rule list is NULL terminated, count them first */
		for (nr_rules = 0; ref_rev_parse_rules[nr_rules]; nr_rules++)
			/* -2 for strlen("%.*s") - strlen("%s"); +1 for NUL */
			total_len += strlen(ref_rev_parse_rules[nr_rules]) - 2 + 1;

		scanf_fmts = xmalloc(st_add(st_mult(sizeof(char *), nr_rules), total_len));

		offset = 0;
		for (i = 0; i < nr_rules; i++) {
			assert(offset < total_len);
			scanf_fmts[i] = (char *)&scanf_fmts[nr_rules] + offset;
			offset += xsnprintf(scanf_fmts[i], total_len - offset,
					    ref_rev_parse_rules[i], 2, "%s") + 1;
		}
	}

	/* bail out if there are no rules */
	if (!nr_rules)
		return xstrdup(refname);

	/* buffer for scanf result, at most refname must fit */
	short_name = xstrdup(refname);

	/* skip first rule, it will always match */
	for (i = nr_rules - 1; i > 0 ; --i) {
		int j;
		int rules_to_fail = i;
		int short_name_len;

		if (1 != sscanf(refname, scanf_fmts[i], short_name))
			continue;

		short_name_len = strlen(short_name);

		/*
		 * in strict mode, all (except the matched one) rules
		 * must fail to resolve to a valid non-ambiguous ref
		 */
		if (strict)
			rules_to_fail = nr_rules;

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
				    short_name_len, short_name);
			if (refs_ref_exists(refs, resolved_buf.buf))
				break;
		}

		/*
		 * short name is non-ambiguous if all previous rules
		 * haven't resolved to a valid ref
		 */
		if (j == rules_to_fail) {
			strbuf_release(&resolved_buf);
			return short_name;
		}
	}

	strbuf_release(&resolved_buf);
	free(short_name);
	return xstrdup(refname);
}

char *shorten_unambiguous_ref(const char *refname, int strict)
{
	return refs_shorten_unambiguous_ref(get_main_ref_store(the_repository),
					    refname, strict);
}

static struct string_list *hide_refs;

int parse_hide_refs_config(const char *var, const char *value, const char *section)
{
	const char *key;
	if (!strcmp("transfer.hiderefs", var) ||
	    (!parse_config_key(var, section, NULL, NULL, &key) &&
	     !strcmp(key, "hiderefs"))) {
		char *ref;
		int len;

		if (!value)
			return config_error_nonbool(var);
		ref = xstrdup(value);
		len = strlen(ref);
		while (len && ref[len - 1] == '/')
			ref[--len] = '\0';
		if (!hide_refs) {
			CALLOC_ARRAY(hide_refs, 1);
			hide_refs->strdup_strings = 1;
		}
		string_list_append(hide_refs, ref);
	}
	return 0;
}

int ref_is_hidden(const char *refname, const char *refname_full)
{
	int i;

	if (!hide_refs)
		return 0;
	for (i = hide_refs->nr - 1; i >= 0; i--) {
		const char *match = hide_refs->items[i].string;
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

int refs_rename_ref_available(struct ref_store *refs,
			      const char *old_refname,
			      const char *new_refname)
{
	struct string_list skip = STRING_LIST_INIT_NODUP;
	struct strbuf err = STRBUF_INIT;
	int ok;

	string_list_insert(&skip, old_refname);
	ok = !refs_verify_refname_available(refs, new_refname,
					    NULL, &skip, &err);
	if (!ok)
		error("%s", err.buf);

	string_list_clear(&skip, 0);
	strbuf_release(&err);
	return ok;
}

int refs_head_ref(struct ref_store *refs, each_ref_fn fn, void *cb_data)
{
	struct object_id oid;
	int flag;

	if (!refs_read_ref_full(refs, "HEAD", RESOLVE_REF_READING,
				&oid, &flag))
		return fn("HEAD", &oid, flag, cb_data);

	return 0;
}

int head_ref(each_ref_fn fn, void *cb_data)
{
	return refs_head_ref(get_main_ref_store(the_repository), fn, cb_data);
}

struct ref_iterator *refs_ref_iterator_begin(
		struct ref_store *refs,
		const char *prefix, int trim, int flags)
{
	struct ref_iterator *iter;

	if (ref_paranoia < 0)
		ref_paranoia = git_env_bool("GIT_REF_PARANOIA", 0);
	if (ref_paranoia)
		flags |= DO_FOR_EACH_INCLUDE_BROKEN;

	iter = refs->be->iterator_begin(refs, prefix, flags);

	/*
	 * `iterator_begin()` already takes care of prefix, but we
	 * might need to do some trimming:
	 */
	if (trim)
		iter = prefix_ref_iterator_begin(iter, "", trim);

	/* Sanity check for subclasses: */
	if (!iter->ordered)
		BUG("reference iterator is not ordered");

	return iter;
}

/*
 * Call fn for each reference in the specified submodule for which the
 * refname begins with prefix. If trim is non-zero, then trim that
 * many characters off the beginning of each refname before passing
 * the refname to fn. flags can be DO_FOR_EACH_INCLUDE_BROKEN to
 * include broken references in the iteration. If fn ever returns a
 * non-zero value, stop the iteration and return that value;
 * otherwise, return 0.
 */
static int do_for_each_repo_ref(struct repository *r, const char *prefix,
				each_repo_ref_fn fn, int trim, int flags,
				void *cb_data)
{
	struct ref_iterator *iter;
	struct ref_store *refs = get_main_ref_store(r);

	if (!refs)
		return 0;

	iter = refs_ref_iterator_begin(refs, prefix, trim, flags);

	return do_for_each_repo_ref_iterator(r, iter, fn, cb_data);
}

struct do_for_each_ref_help {
	each_ref_fn *fn;
	void *cb_data;
};

static int do_for_each_ref_helper(struct repository *r,
				  const char *refname,
				  const struct object_id *oid,
				  int flags,
				  void *cb_data)
{
	struct do_for_each_ref_help *hp = cb_data;

	return hp->fn(refname, oid, flags, hp->cb_data);
}

static int do_for_each_ref(struct ref_store *refs, const char *prefix,
			   each_ref_fn fn, int trim, int flags, void *cb_data)
{
	struct ref_iterator *iter;
	struct do_for_each_ref_help hp = { fn, cb_data };

	if (!refs)
		return 0;

	iter = refs_ref_iterator_begin(refs, prefix, trim, flags);

	return do_for_each_repo_ref_iterator(the_repository, iter,
					do_for_each_ref_helper, &hp);
}

int refs_for_each_ref(struct ref_store *refs, each_ref_fn fn, void *cb_data)
{
	return do_for_each_ref(refs, "", fn, 0, 0, cb_data);
}

int for_each_ref(each_ref_fn fn, void *cb_data)
{
	return refs_for_each_ref(get_main_ref_store(the_repository), fn, cb_data);
}

int refs_for_each_ref_in(struct ref_store *refs, const char *prefix,
			 each_ref_fn fn, void *cb_data)
{
	return do_for_each_ref(refs, prefix, fn, strlen(prefix), 0, cb_data);
}

int for_each_ref_in(const char *prefix, each_ref_fn fn, void *cb_data)
{
	return refs_for_each_ref_in(get_main_ref_store(the_repository), prefix, fn, cb_data);
}

int for_each_fullref_in(const char *prefix, each_ref_fn fn, void *cb_data, unsigned int broken)
{
	unsigned int flag = 0;

	if (broken)
		flag = DO_FOR_EACH_INCLUDE_BROKEN;
	return do_for_each_ref(get_main_ref_store(the_repository),
			       prefix, fn, 0, flag, cb_data);
}

int refs_for_each_fullref_in(struct ref_store *refs, const char *prefix,
			     each_ref_fn fn, void *cb_data,
			     unsigned int broken)
{
	unsigned int flag = 0;

	if (broken)
		flag = DO_FOR_EACH_INCLUDE_BROKEN;
	return do_for_each_ref(refs, prefix, fn, 0, flag, cb_data);
}

int for_each_replace_ref(struct repository *r, each_repo_ref_fn fn, void *cb_data)
{
	return do_for_each_repo_ref(r, git_replace_ref_base, fn,
				    strlen(git_replace_ref_base),
				    DO_FOR_EACH_INCLUDE_BROKEN, cb_data);
}

int for_each_namespaced_ref(each_ref_fn fn, void *cb_data)
{
	struct strbuf buf = STRBUF_INIT;
	int ret;
	strbuf_addf(&buf, "%srefs/", get_git_namespace());
	ret = do_for_each_ref(get_main_ref_store(the_repository),
			      buf.buf, fn, 0, 0, cb_data);
	strbuf_release(&buf);
	return ret;
}

int refs_for_each_rawref(struct ref_store *refs, each_ref_fn fn, void *cb_data)
{
	return do_for_each_ref(refs, "", fn, 0,
			       DO_FOR_EACH_INCLUDE_BROKEN, cb_data);
}

int for_each_rawref(each_ref_fn fn, void *cb_data)
{
	return refs_for_each_rawref(get_main_ref_store(the_repository), fn, cb_data);
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

int for_each_fullref_in_prefixes(const char *namespace,
				 const char **patterns,
				 each_ref_fn fn, void *cb_data,
				 unsigned int broken)
{
	struct string_list prefixes = STRING_LIST_INIT_DUP;
	struct string_list_item *prefix;
	struct strbuf buf = STRBUF_INIT;
	int ret = 0, namespace_len;

	find_longest_prefixes(&prefixes, patterns);

	if (namespace)
		strbuf_addstr(&buf, namespace);
	namespace_len = buf.len;

	for_each_string_list_item(prefix, &prefixes) {
		strbuf_addstr(&buf, prefix->string);
		ret = for_each_fullref_in(buf.buf, fn, cb_data, broken);
		if (ret)
			break;
		strbuf_setlen(&buf, namespace_len);
	}

	string_list_clear(&prefixes, 0);
	strbuf_release(&buf);
	return ret;
}

static int refs_read_special_head(struct ref_store *ref_store,
				  const char *refname, struct object_id *oid,
				  struct strbuf *referent, unsigned int *type)
{
	struct strbuf full_path = STRBUF_INIT;
	struct strbuf content = STRBUF_INIT;
	int result = -1;
	strbuf_addf(&full_path, "%s/%s", ref_store->gitdir, refname);

	if (strbuf_read_file(&content, full_path.buf, 0) < 0)
		goto done;

	result = parse_loose_ref_contents(content.buf, oid, referent, type);

done:
	strbuf_release(&full_path);
	strbuf_release(&content);
	return result;
}

int refs_read_raw_ref(struct ref_store *ref_store,
		      const char *refname, struct object_id *oid,
		      struct strbuf *referent, unsigned int *type)
{
	if (!strcmp(refname, "FETCH_HEAD") || !strcmp(refname, "MERGE_HEAD")) {
		return refs_read_special_head(ref_store, refname, oid, referent,
					      type);
	}

	return ref_store->be->read_raw_ref(ref_store, refname, oid, referent,
					   type);
}

/* This function needs to return a meaningful errno on failure */
const char *refs_resolve_ref_unsafe(struct ref_store *refs,
				    const char *refname,
				    int resolve_flags,
				    struct object_id *oid, int *flags)
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
		    !refname_is_safe(refname)) {
			errno = EINVAL;
			return NULL;
		}

		/*
		 * dwim_ref() uses REF_ISBROKEN to distinguish between
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

		if (refs_read_raw_ref(refs, refname,
				      oid, &sb_refname, &read_flags)) {
			*flags |= read_flags;

			/* In reading mode, refs must eventually resolve */
			if (resolve_flags & RESOLVE_REF_READING)
				return NULL;

			/*
			 * Otherwise a missing ref is OK. But the files backend
			 * may show errors besides ENOENT if there are
			 * similarly-named refs.
			 */
			if (errno != ENOENT &&
			    errno != EISDIR &&
			    errno != ENOTDIR)
				return NULL;

			oidclr(oid);
			if (*flags & REF_BAD_NAME)
				*flags |= REF_ISBROKEN;
			return refname;
		}

		*flags |= read_flags;

		if (!(read_flags & REF_ISSYMREF)) {
			if (*flags & REF_BAD_NAME) {
				oidclr(oid);
				*flags |= REF_ISBROKEN;
			}
			return refname;
		}

		refname = sb_refname.buf;
		if (resolve_flags & RESOLVE_REF_NO_RECURSE) {
			oidclr(oid);
			return refname;
		}
		if (check_refname_format(refname, REFNAME_ALLOW_ONELEVEL)) {
			if (!(resolve_flags & RESOLVE_REF_ALLOW_BAD_NAME) ||
			    !refname_is_safe(refname)) {
				errno = EINVAL;
				return NULL;
			}

			*flags |= REF_ISBROKEN | REF_BAD_NAME;
		}
	}

	errno = ELOOP;
	return NULL;
}

/* backend functions */
int refs_init_db(struct strbuf *err)
{
	struct ref_store *refs = get_main_ref_store(the_repository);

	return refs->be->init_db(refs, err);
}

const char *resolve_ref_unsafe(const char *refname, int resolve_flags,
			       struct object_id *oid, int *flags)
{
	return refs_resolve_ref_unsafe(get_main_ref_store(the_repository), refname,
				       resolve_flags, oid, flags);
}

int resolve_gitlink_ref(const char *submodule, const char *refname,
			struct object_id *oid)
{
	struct ref_store *refs;
	int flags;

	refs = get_submodule_ref_store(submodule);

	if (!refs)
		return -1;

	if (!refs_resolve_ref_unsafe(refs, refname, 0, oid, &flags) ||
	    is_null_oid(oid))
		return -1;
	return 0;
}

struct ref_store_hash_entry
{
	struct hashmap_entry ent;

	struct ref_store *refs;

	/* NUL-terminated identifier of the ref store: */
	char name[FLEX_ARRAY];
};

static int ref_store_hash_cmp(const void *unused_cmp_data,
			      const struct hashmap_entry *eptr,
			      const struct hashmap_entry *entry_or_key,
			      const void *keydata)
{
	const struct ref_store_hash_entry *e1, *e2;
	const char *name;

	e1 = container_of(eptr, const struct ref_store_hash_entry, ent);
	e2 = container_of(entry_or_key, const struct ref_store_hash_entry, ent);
	name = keydata ? keydata : e2->name;

	return strcmp(e1->name, name);
}

static struct ref_store_hash_entry *alloc_ref_store_hash_entry(
		const char *name, struct ref_store *refs)
{
	struct ref_store_hash_entry *entry;

	FLEX_ALLOC_STR(entry, name, name);
	hashmap_entry_init(&entry->ent, strhash(name));
	entry->refs = refs;
	return entry;
}

/* A hashmap of ref_stores, stored by submodule name: */
static struct hashmap submodule_ref_stores;

/* A hashmap of ref_stores, stored by worktree id: */
static struct hashmap worktree_ref_stores;

/*
 * Look up a ref store by name. If that ref_store hasn't been
 * registered yet, return NULL.
 */
static struct ref_store *lookup_ref_store_map(struct hashmap *map,
					      const char *name)
{
	struct ref_store_hash_entry *entry;
	unsigned int hash;

	if (!map->tablesize)
		/* It's initialized on demand in register_ref_store(). */
		return NULL;

	hash = strhash(name);
	entry = hashmap_get_entry_from_hash(map, hash, name,
					struct ref_store_hash_entry, ent);
	return entry ? entry->refs : NULL;
}

/*
 * Create, record, and return a ref_store instance for the specified
 * gitdir.
 */
static struct ref_store *ref_store_init(const char *gitdir,
					unsigned int flags)
{
	const char *be_name = "files";
	struct ref_storage_be *be = find_ref_storage_backend(be_name);
	struct ref_store *refs;

	if (!be)
		BUG("reference backend %s is unknown", be_name);

	refs = be->init(gitdir, flags);
	return refs;
}

struct ref_store *get_main_ref_store(struct repository *r)
{
	if (r->refs_private)
		return r->refs_private;

	if (!r->gitdir)
		BUG("attempting to get main_ref_store outside of repository");

	r->refs_private = ref_store_init(r->gitdir, REF_STORE_ALL_CAPS);
	r->refs_private = maybe_debug_wrap_ref_store(r->gitdir, r->refs_private);
	return r->refs_private;
}

/*
 * Associate a ref store with a name. It is a fatal error to call this
 * function twice for the same name.
 */
static void register_ref_store_map(struct hashmap *map,
				   const char *type,
				   struct ref_store *refs,
				   const char *name)
{
	struct ref_store_hash_entry *entry;

	if (!map->tablesize)
		hashmap_init(map, ref_store_hash_cmp, NULL, 0);

	entry = alloc_ref_store_hash_entry(name, refs);
	if (hashmap_put(map, &entry->ent))
		BUG("%s ref_store '%s' initialized twice", type, name);
}

struct ref_store *get_submodule_ref_store(const char *submodule)
{
	struct strbuf submodule_sb = STRBUF_INIT;
	struct ref_store *refs;
	char *to_free = NULL;
	size_t len;

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

	refs = lookup_ref_store_map(&submodule_ref_stores, submodule);
	if (refs)
		goto done;

	strbuf_addstr(&submodule_sb, submodule);
	if (!is_nonbare_repository_dir(&submodule_sb))
		goto done;

	if (submodule_to_gitdir(&submodule_sb, submodule))
		goto done;

	/* assume that add_submodule_odb() has been called */
	refs = ref_store_init(submodule_sb.buf,
			      REF_STORE_READ | REF_STORE_ODB);
	register_ref_store_map(&submodule_ref_stores, "submodule",
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
		return get_main_ref_store(the_repository);

	id = wt->id ? wt->id : "/";
	refs = lookup_ref_store_map(&worktree_ref_stores, id);
	if (refs)
		return refs;

	if (wt->id)
		refs = ref_store_init(git_common_path("worktrees/%s", wt->id),
				      REF_STORE_ALL_CAPS);
	else
		refs = ref_store_init(get_git_common_dir(),
				      REF_STORE_ALL_CAPS);

	if (refs)
		register_ref_store_map(&worktree_ref_stores, "worktree",
				       refs, id);
	return refs;
}

void base_ref_store_init(struct ref_store *refs,
			 const struct ref_storage_be *be)
{
	refs->be = be;
}

/* backend functions */
int refs_pack_refs(struct ref_store *refs, unsigned int flags)
{
	return refs->be->pack_refs(refs, flags);
}

int peel_iterated_oid(const struct object_id *base, struct object_id *peeled)
{
	if (current_ref_iter &&
	    (current_ref_iter->oid == base ||
	     oideq(current_ref_iter->oid, base)))
		return ref_iterator_peel(current_ref_iter, peeled);

	return peel_object(base, peeled) ? -1 : 0;
}

int refs_create_symref(struct ref_store *refs,
		       const char *ref_target,
		       const char *refs_heads_master,
		       const char *logmsg)
{
	char *msg;
	int retval;

	msg = normalize_reflog_message(logmsg);
	retval = refs->be->create_symref(refs, ref_target, refs_heads_master,
					 msg);
	free(msg);
	return retval;
}

int create_symref(const char *ref_target, const char *refs_heads_master,
		  const char *logmsg)
{
	return refs_create_symref(get_main_ref_store(the_repository), ref_target,
				  refs_heads_master, logmsg);
}

int ref_update_reject_duplicates(struct string_list *refnames,
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

	hook = find_hook("reference-transaction");
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

		strbuf_reset(&buf);
		strbuf_addf(&buf, "%s %s %s\n",
			    oid_to_hex(&update->old_oid),
			    oid_to_hex(&update->new_oid),
			    update->refname);

		if (write_in_full(proc.in, buf.buf, buf.len) < 0) {
			if (errno != EPIPE)
				ret = -1;
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

	if (getenv(GIT_QUARANTINE_ENVIRONMENT)) {
		strbuf_addstr(err,
			      _("ref updates forbidden inside quarantine environment"));
		return -1;
	}

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
	if (!ret)
		run_transaction_hook(transaction, "committed");
	return ret;
}

int refs_verify_refname_available(struct ref_store *refs,
				  const char *refname,
				  const struct string_list *extras,
				  const struct string_list *skip,
				  struct strbuf *err)
{
	const char *slash;
	const char *extra_refname;
	struct strbuf dirname = STRBUF_INIT;
	struct strbuf referent = STRBUF_INIT;
	struct object_id oid;
	unsigned int type;
	struct ref_iterator *iter;
	int ok;
	int ret = -1;

	/*
	 * For the sake of comments in this function, suppose that
	 * refname is "refs/foo/bar".
	 */

	assert(err);

	strbuf_grow(&dirname, strlen(refname) + 1);
	for (slash = strchr(refname, '/'); slash; slash = strchr(slash + 1, '/')) {
		/* Expand dirname to the new prefix, not including the trailing slash: */
		strbuf_add(&dirname, refname + dirname.len, slash - refname - dirname.len);

		/*
		 * We are still at a leading dir of the refname (e.g.,
		 * "refs/foo"; if there is a reference with that name,
		 * it is a conflict, *unless* it is in skip.
		 */
		if (skip && string_list_has_string(skip, dirname.buf))
			continue;

		if (!refs_read_raw_ref(refs, dirname.buf, &oid, &referent, &type)) {
			strbuf_addf(err, _("'%s' exists; cannot create '%s'"),
				    dirname.buf, refname);
			goto cleanup;
		}

		if (extras && string_list_has_string(extras, dirname.buf)) {
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

	iter = refs_ref_iterator_begin(refs, dirname.buf, 0,
				       DO_FOR_EACH_INCLUDE_BROKEN);
	while ((ok = ref_iterator_advance(iter)) == ITER_OK) {
		if (skip &&
		    string_list_has_string(skip, iter->refname))
			continue;

		strbuf_addf(err, _("'%s' exists; cannot create '%s'"),
			    iter->refname, refname);
		ref_iterator_abort(iter);
		goto cleanup;
	}

	if (ok != ITER_DONE)
		BUG("error while iterating over references");

	extra_refname = find_descendant_ref(dirname.buf, extras, skip);
	if (extra_refname)
		strbuf_addf(err, _("cannot process '%s' and '%s' at the same time"),
			    refname, extra_refname);
	else
		ret = 0;

cleanup:
	strbuf_release(&referent);
	strbuf_release(&dirname);
	return ret;
}

int refs_for_each_reflog(struct ref_store *refs, each_ref_fn fn, void *cb_data)
{
	struct ref_iterator *iter;
	struct do_for_each_ref_help hp = { fn, cb_data };

	iter = refs->be->reflog_iterator_begin(refs);

	return do_for_each_repo_ref_iterator(the_repository, iter,
					     do_for_each_ref_helper, &hp);
}

int for_each_reflog(each_ref_fn fn, void *cb_data)
{
	return refs_for_each_reflog(get_main_ref_store(the_repository), fn, cb_data);
}

int refs_for_each_reflog_ent_reverse(struct ref_store *refs,
				     const char *refname,
				     each_reflog_ent_fn fn,
				     void *cb_data)
{
	return refs->be->for_each_reflog_ent_reverse(refs, refname,
						     fn, cb_data);
}

int for_each_reflog_ent_reverse(const char *refname, each_reflog_ent_fn fn,
				void *cb_data)
{
	return refs_for_each_reflog_ent_reverse(get_main_ref_store(the_repository),
						refname, fn, cb_data);
}

int refs_for_each_reflog_ent(struct ref_store *refs, const char *refname,
			     each_reflog_ent_fn fn, void *cb_data)
{
	return refs->be->for_each_reflog_ent(refs, refname, fn, cb_data);
}

int for_each_reflog_ent(const char *refname, each_reflog_ent_fn fn,
			void *cb_data)
{
	return refs_for_each_reflog_ent(get_main_ref_store(the_repository), refname,
					fn, cb_data);
}

int refs_reflog_exists(struct ref_store *refs, const char *refname)
{
	return refs->be->reflog_exists(refs, refname);
}

int reflog_exists(const char *refname)
{
	return refs_reflog_exists(get_main_ref_store(the_repository), refname);
}

int refs_create_reflog(struct ref_store *refs, const char *refname,
		       int force_create, struct strbuf *err)
{
	return refs->be->create_reflog(refs, refname, force_create, err);
}

int safe_create_reflog(const char *refname, int force_create,
		       struct strbuf *err)
{
	return refs_create_reflog(get_main_ref_store(the_repository), refname,
				  force_create, err);
}

int refs_delete_reflog(struct ref_store *refs, const char *refname)
{
	return refs->be->delete_reflog(refs, refname);
}

int delete_reflog(const char *refname)
{
	return refs_delete_reflog(get_main_ref_store(the_repository), refname);
}

int refs_reflog_expire(struct ref_store *refs,
		       const char *refname, const struct object_id *oid,
		       unsigned int flags,
		       reflog_expiry_prepare_fn prepare_fn,
		       reflog_expiry_should_prune_fn should_prune_fn,
		       reflog_expiry_cleanup_fn cleanup_fn,
		       void *policy_cb_data)
{
	return refs->be->reflog_expire(refs, refname, oid, flags,
				       prepare_fn, should_prune_fn,
				       cleanup_fn, policy_cb_data);
}

int reflog_expire(const char *refname, const struct object_id *oid,
		  unsigned int flags,
		  reflog_expiry_prepare_fn prepare_fn,
		  reflog_expiry_should_prune_fn should_prune_fn,
		  reflog_expiry_cleanup_fn cleanup_fn,
		  void *policy_cb_data)
{
	return refs_reflog_expire(get_main_ref_store(the_repository),
				  refname, oid, flags,
				  prepare_fn, should_prune_fn,
				  cleanup_fn, policy_cb_data);
}

int initial_ref_transaction_commit(struct ref_transaction *transaction,
				   struct strbuf *err)
{
	struct ref_store *refs = transaction->ref_store;

	return refs->be->initial_transaction_commit(refs, transaction, err);
}

int refs_delete_refs(struct ref_store *refs, const char *logmsg,
		     struct string_list *refnames, unsigned int flags)
{
	char *msg;
	int retval;

	msg = normalize_reflog_message(logmsg);
	retval = refs->be->delete_refs(refs, msg, refnames, flags);
	free(msg);
	return retval;
}

int delete_refs(const char *msg, struct string_list *refnames,
		unsigned int flags)
{
	return refs_delete_refs(get_main_ref_store(the_repository), msg, refnames, flags);
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

int rename_ref(const char *oldref, const char *newref, const char *logmsg)
{
	return refs_rename_ref(get_main_ref_store(the_repository), oldref, newref, logmsg);
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

int copy_existing_ref(const char *oldref, const char *newref, const char *logmsg)
{
	return refs_copy_existing_ref(get_main_ref_store(the_repository), oldref, newref, logmsg);
}
