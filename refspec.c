#define USE_THE_REPOSITORY_VARIABLE
#define DISABLE_SIGN_COMPARE_WARNINGS

#include "git-compat-util.h"
#include "gettext.h"
#include "hash.h"
#include "hex.h"
#include "string-list.h"
#include "strvec.h"
#include "refs.h"
#include "refspec.h"
#include "remote.h"
#include "strbuf.h"

/*
 * Parses the provided refspec 'refspec' and populates the refspec_item 'item'.
 * Returns 1 if successful and 0 if the refspec is invalid.
 */
static int parse_refspec(struct refspec_item *item, const char *refspec, int fetch)
{
	size_t llen;
	int is_glob;
	const char *lhs, *rhs;
	int flags;

	is_glob = 0;

	lhs = refspec;
	if (*lhs == '+') {
		item->force = 1;
		lhs++;
	} else if (*lhs == '^') {
		item->negative = 1;
		lhs++;
	}

	rhs = strrchr(lhs, ':');

	/* negative refspecs only have one side */
	if (item->negative && rhs)
		return 0;

	/*
	 * Before going on, special case ":" (or "+:") as a refspec
	 * for pushing matching refs.
	 */
	if (!fetch && rhs == lhs && rhs[1] == '\0') {
		item->matching = 1;
		return 1;
	}

	if (rhs) {
		size_t rlen = strlen(++rhs);
		is_glob = (1 <= rlen && strchr(rhs, '*'));
		item->dst = xstrndup(rhs, rlen);
	} else {
		item->dst = NULL;
	}

	llen = (rhs ? (rhs - lhs - 1) : strlen(lhs));
	if (1 <= llen && memchr(lhs, '*', llen)) {
		if ((rhs && !is_glob) || (!rhs && !item->negative && fetch))
			return 0;
		is_glob = 1;
	} else if (rhs && is_glob) {
		return 0;
	}

	item->pattern = is_glob;
	if (llen == 1 && *lhs == '@')
		item->src = xstrdup("HEAD");
	else
		item->src = xstrndup(lhs, llen);
	flags = REFNAME_ALLOW_ONELEVEL | (is_glob ? REFNAME_REFSPEC_PATTERN : 0);

	if (item->negative) {
		struct object_id unused;

		/*
		 * Negative refspecs only have a LHS, which indicates a ref
		 * (or pattern of refs) to exclude from other matches. This
		 * can either be a simple ref, or a glob pattern. Exact sha1
		 * match is not currently supported.
		 */
		if (!*item->src)
			return 0; /* negative refspecs must not be empty */
		else if (llen == the_hash_algo->hexsz && !get_oid_hex(item->src, &unused))
			return 0; /* negative refpsecs cannot be exact sha1 */
		else if (!check_refname_format(item->src, flags))
			; /* valid looking ref is ok */
		else
			return 0;

		/* the other rules below do not apply to negative refspecs */
		return 1;
	}

	if (fetch) {
		struct object_id unused;

		/* LHS */
		if (!*item->src)
			; /* empty is ok; it means "HEAD" */
		else if (llen == the_hash_algo->hexsz && !get_oid_hex(item->src, &unused))
			item->exact_sha1 = 1; /* ok */
		else if (!check_refname_format(item->src, flags))
			; /* valid looking ref is ok */
		else
			return 0;
		/* RHS */
		if (!item->dst)
			; /* missing is ok; it is the same as empty */
		else if (!*item->dst)
			; /* empty is ok; it means "do not store" */
		else if (!check_refname_format(item->dst, flags))
			; /* valid looking ref is ok */
		else
			return 0;
	} else {
		/*
		 * LHS
		 * - empty is allowed; it means delete.
		 * - when wildcarded, it must be a valid looking ref.
		 * - otherwise, it must be an extended SHA-1, but
		 *   there is no existing way to validate this.
		 */
		if (!*item->src)
			; /* empty is ok */
		else if (is_glob) {
			if (check_refname_format(item->src, flags))
				return 0;
		}
		else
			; /* anything goes, for now */
		/*
		 * RHS
		 * - missing is allowed, but LHS then must be a
		 *   valid looking ref.
		 * - empty is not allowed.
		 * - otherwise it must be a valid looking ref.
		 */
		if (!item->dst) {
			if (check_refname_format(item->src, flags))
				return 0;
		} else if (!*item->dst) {
			return 0;
		} else {
			if (check_refname_format(item->dst, flags))
				return 0;
		}
	}

	return 1;
}

static int refspec_item_init(struct refspec_item *item, const char *refspec,
			     int fetch)
{
	memset(item, 0, sizeof(*item));
	item->raw = xstrdup(refspec);
	return parse_refspec(item, refspec, fetch);
}

int refspec_item_init_fetch(struct refspec_item *item, const char *refspec)
{
	return refspec_item_init(item, refspec, 1);
}

int refspec_item_init_push(struct refspec_item *item, const char *refspec)
{
	return refspec_item_init(item, refspec, 0);
}

void refspec_item_clear(struct refspec_item *item)
{
	FREE_AND_NULL(item->src);
	FREE_AND_NULL(item->dst);
	FREE_AND_NULL(item->raw);
	item->force = 0;
	item->pattern = 0;
	item->matching = 0;
	item->exact_sha1 = 0;
}

void refspec_init_fetch(struct refspec *rs)
{
	struct refspec blank = REFSPEC_INIT_FETCH;
	memcpy(rs, &blank, sizeof(*rs));
}

void refspec_init_push(struct refspec *rs)
{
	struct refspec blank = REFSPEC_INIT_PUSH;
	memcpy(rs, &blank, sizeof(*rs));
}

void refspec_append(struct refspec *rs, const char *refspec)
{
	struct refspec_item item;
	int ret;

	if (rs->fetch)
		ret = refspec_item_init_fetch(&item, refspec);
	else
		ret = refspec_item_init_push(&item, refspec);
	if (!ret)
		die(_("invalid refspec '%s'"), refspec);

	ALLOC_GROW(rs->items, rs->nr + 1, rs->alloc);
	rs->items[rs->nr] = item;

	rs->nr++;
}

void refspec_appendf(struct refspec *rs, const char *fmt, ...)
{
	va_list ap;
	char *buf;

	va_start(ap, fmt);
	buf = xstrvfmt(fmt, ap);
	va_end(ap);

	refspec_append(rs, buf);
	free(buf);
}

void refspec_appendn(struct refspec *rs, const char **refspecs, int nr)
{
	int i;
	for (i = 0; i < nr; i++)
		refspec_append(rs, refspecs[i]);
}

void refspec_clear(struct refspec *rs)
{
	int i;

	for (i = 0; i < rs->nr; i++)
		refspec_item_clear(&rs->items[i]);

	FREE_AND_NULL(rs->items);
	rs->alloc = 0;
	rs->nr = 0;

	rs->fetch = 0;
}

int valid_fetch_refspec(const char *fetch_refspec_str)
{
	struct refspec_item refspec;
	int ret = refspec_item_init_fetch(&refspec, fetch_refspec_str);
	refspec_item_clear(&refspec);
	return ret;
}

void refspec_ref_prefixes(const struct refspec *rs,
			  struct strvec *ref_prefixes)
{
	int i;
	for (i = 0; i < rs->nr; i++) {
		const struct refspec_item *item = &rs->items[i];
		const char *prefix = NULL;

		if (item->negative)
			continue;

		if (rs->fetch) {
			if (item->exact_sha1)
				continue;
			prefix = item->src;
		} else {
			/*
			 * Pushes can have an explicit destination like
			 * "foo:bar", or can implicitly use the src for both
			 * ("foo" is the same as "foo:foo").
			 */
			if (item->dst)
				prefix = item->dst;
			else if (item->src && !item->exact_sha1)
				prefix = item->src;
		}

		if (!prefix)
			continue;

		if (item->pattern) {
			const char *glob = strchr(prefix, '*');
			strvec_pushf(ref_prefixes, "%.*s",
				     (int)(glob - prefix),
				     prefix);
		} else {
			expand_ref_prefix(ref_prefixes, prefix);
		}
	}
}

int match_refname_with_pattern(const char *pattern, const char *refname,
				   const char *replacement, char **result)
{
	const char *kstar = strchr(pattern, '*');
	size_t klen;
	size_t ksuffixlen;
	size_t namelen;
	int ret;
	if (!kstar)
		die(_("pattern '%s' has no '*'"), pattern);
	klen = kstar - pattern;
	ksuffixlen = strlen(kstar + 1);
	namelen = strlen(refname);
	ret = !strncmp(refname, pattern, klen) && namelen >= klen + ksuffixlen &&
		!memcmp(refname + namelen - ksuffixlen, kstar + 1, ksuffixlen);
	if (ret && replacement) {
		struct strbuf sb = STRBUF_INIT;
		const char *vstar = strchr(replacement, '*');
		if (!vstar)
			die(_("replacement '%s' has no '*'"), replacement);
		strbuf_add(&sb, replacement, vstar - replacement);
		strbuf_add(&sb, refname + klen, namelen - klen - ksuffixlen);
		strbuf_addstr(&sb, vstar + 1);
		*result = strbuf_detach(&sb, NULL);
	}
	return ret;
}

static int refspec_match(const struct refspec_item *refspec,
			 const char *name)
{
	if (refspec->pattern)
		return match_refname_with_pattern(refspec->src, name, NULL, NULL);

	return !strcmp(refspec->src, name);
}

int refname_matches_negative_refspec_item(const char *refname, struct refspec *rs)
{
	int i;

	for (i = 0; i < rs->nr; i++) {
		if (rs->items[i].negative && refspec_match(&rs->items[i], refname))
			return 1;
	}
	return 0;
}

static int refspec_find_negative_match(struct refspec *rs, struct refspec_item *query)
{
	int i, matched_negative = 0;
	int find_src = !query->src;
	struct string_list reversed = STRING_LIST_INIT_DUP;
	const char *needle = find_src ? query->dst : query->src;

	/*
	 * Check whether the queried ref matches any negative refpsec. If so,
	 * then we should ultimately treat this as not matching the query at
	 * all.
	 *
	 * Note that negative refspecs always match the source, but the query
	 * item uses the destination. To handle this, we apply pattern
	 * refspecs in reverse to figure out if the query source matches any
	 * of the negative refspecs.
	 *
	 * The first loop finds and expands all positive refspecs
	 * matched by the queried ref.
	 *
	 * The second loop checks if any of the results of the first loop
	 * match any negative refspec.
	 */
	for (i = 0; i < rs->nr; i++) {
		struct refspec_item *refspec = &rs->items[i];
		char *expn_name;

		if (refspec->negative)
			continue;

		/* Note the reversal of src and dst */
		if (refspec->pattern) {
			const char *key = refspec->dst ? refspec->dst : refspec->src;
			const char *value = refspec->src;

			if (match_refname_with_pattern(key, needle, value, &expn_name))
				string_list_append_nodup(&reversed, expn_name);
		} else if (refspec->matching) {
			/* For the special matching refspec, any query should match */
			string_list_append(&reversed, needle);
		} else if (!refspec->src) {
			BUG("refspec->src should not be null here");
		} else if (!strcmp(needle, refspec->src)) {
			string_list_append(&reversed, refspec->src);
		}
	}

	for (i = 0; !matched_negative && i < reversed.nr; i++) {
		if (refname_matches_negative_refspec_item(reversed.items[i].string, rs))
			matched_negative = 1;
	}

	string_list_clear(&reversed, 0);

	return matched_negative;
}

void refspec_find_all_matches(struct refspec *rs,
				    struct refspec_item *query,
				    struct string_list *results)
{
	int i;
	int find_src = !query->src;

	if (find_src && !query->dst)
		BUG("refspec_find_all_matches: need either src or dst");

	if (refspec_find_negative_match(rs, query))
		return;

	for (i = 0; i < rs->nr; i++) {
		struct refspec_item *refspec = &rs->items[i];
		const char *key = find_src ? refspec->dst : refspec->src;
		const char *value = find_src ? refspec->src : refspec->dst;
		const char *needle = find_src ? query->dst : query->src;
		char **result = find_src ? &query->src : &query->dst;

		if (!refspec->dst || refspec->negative)
			continue;
		if (refspec->pattern) {
			if (match_refname_with_pattern(key, needle, value, result))
				string_list_append_nodup(results, *result);
		} else if (!strcmp(needle, key)) {
			string_list_append(results, value);
		}
	}
}

int refspec_find_match(struct refspec *rs, struct refspec_item *query)
{
	int i;
	int find_src = !query->src;
	const char *needle = find_src ? query->dst : query->src;
	char **result = find_src ? &query->src : &query->dst;

	if (find_src && !query->dst)
		BUG("refspec_find_match: need either src or dst");

	if (refspec_find_negative_match(rs, query))
		return -1;

	for (i = 0; i < rs->nr; i++) {
		struct refspec_item *refspec = &rs->items[i];
		const char *key = find_src ? refspec->dst : refspec->src;
		const char *value = find_src ? refspec->src : refspec->dst;

		if (!refspec->dst || refspec->negative)
			continue;
		if (refspec->pattern) {
			if (match_refname_with_pattern(key, needle, value, result)) {
				query->force = refspec->force;
				return 0;
			}
		} else if (!strcmp(needle, key)) {
			*result = xstrdup(value);
			query->force = refspec->force;
			return 0;
		}
	}
	return -1;
}

struct ref *apply_negative_refspecs(struct ref *ref_map, struct refspec *rs)
{
	struct ref **tail;

	for (tail = &ref_map; *tail; ) {
		struct ref *ref = *tail;

		if (refname_matches_negative_refspec_item(ref->name, rs)) {
			*tail = ref->next;
			free(ref->peer_ref);
			free(ref);
		} else
			tail = &ref->next;
	}

	return ref_map;
}

char *apply_refspecs(struct refspec *rs, const char *name)
{
	struct refspec_item query;

	memset(&query, 0, sizeof(struct refspec_item));
	query.src = (char *)name;

	if (refspec_find_match(rs, &query))
		return NULL;

	return query.dst;
}
