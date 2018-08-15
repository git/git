#include "cache.h"
#include "argv-array.h"
#include "refs.h"
#include "refspec.h"

static struct refspec_item s_tag_refspec = {
	0,
	1,
	0,
	0,
	"refs/tags/*",
	"refs/tags/*"
};

/* See TAG_REFSPEC for the string version */
const struct refspec_item *tag_refspec = &s_tag_refspec;

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
	}

	rhs = strrchr(lhs, ':');

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
		if ((rhs && !is_glob) || (!rhs && fetch))
			return 0;
		is_glob = 1;
	} else if (rhs && is_glob) {
		return 0;
	}

	item->pattern = is_glob;
	item->src = xstrndup(lhs, llen);
	flags = REFNAME_ALLOW_ONELEVEL | (is_glob ? REFNAME_REFSPEC_PATTERN : 0);

	if (fetch) {
		struct object_id unused;

		/* LHS */
		if (!*item->src)
			; /* empty is ok; it means "HEAD" */
		else if (llen == GIT_SHA1_HEXSZ && !get_oid_hex(item->src, &unused))
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

int refspec_item_init(struct refspec_item *item, const char *refspec, int fetch)
{
	memset(item, 0, sizeof(*item));
	return parse_refspec(item, refspec, fetch);
}

void refspec_item_init_or_die(struct refspec_item *item, const char *refspec,
			      int fetch)
{
	if (!refspec_item_init(item, refspec, fetch))
		die(_("invalid refspec '%s'"), refspec);
}

void refspec_item_clear(struct refspec_item *item)
{
	FREE_AND_NULL(item->src);
	FREE_AND_NULL(item->dst);
	item->force = 0;
	item->pattern = 0;
	item->matching = 0;
	item->exact_sha1 = 0;
}

void refspec_init(struct refspec *rs, int fetch)
{
	memset(rs, 0, sizeof(*rs));
	rs->fetch = fetch;
}

void refspec_append(struct refspec *rs, const char *refspec)
{
	struct refspec_item item;

	refspec_item_init_or_die(&item, refspec, rs->fetch);

	ALLOC_GROW(rs->items, rs->nr + 1, rs->alloc);
	rs->items[rs->nr++] = item;

	ALLOC_GROW(rs->raw, rs->raw_nr + 1, rs->raw_alloc);
	rs->raw[rs->raw_nr++] = xstrdup(refspec);
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

	for (i = 0; i < rs->raw_nr; i++)
		free((char *)rs->raw[i]);
	FREE_AND_NULL(rs->raw);
	rs->raw_alloc = 0;
	rs->raw_nr = 0;

	rs->fetch = 0;
}

int valid_fetch_refspec(const char *fetch_refspec_str)
{
	struct refspec_item refspec;
	int ret = refspec_item_init(&refspec, fetch_refspec_str, REFSPEC_FETCH);
	refspec_item_clear(&refspec);
	return ret;
}

void refspec_ref_prefixes(const struct refspec *rs,
			  struct argv_array *ref_prefixes)
{
	int i;
	for (i = 0; i < rs->nr; i++) {
		const struct refspec_item *item = &rs->items[i];
		const char *prefix = NULL;

		if (item->exact_sha1)
			continue;
		if (rs->fetch == REFSPEC_FETCH)
			prefix = item->src;
		else if (item->dst)
			prefix = item->dst;
		else if (item->src && !item->exact_sha1)
			prefix = item->src;

		if (prefix) {
			if (item->pattern) {
				const char *glob = strchr(prefix, '*');
				argv_array_pushf(ref_prefixes, "%.*s",
						 (int)(glob - prefix),
						 prefix);
			} else {
				expand_ref_prefix(ref_prefixes, prefix);
			}
		}
	}
}
