#include "cache.h"
#include "refs.h"
#include "refspec.h"

static struct refspec s_tag_refspec = {
	0,
	1,
	0,
	0,
	"refs/tags/*",
	"refs/tags/*"
};

/* See TAG_REFSPEC for the string version */
const struct refspec *tag_refspec = &s_tag_refspec;

static struct refspec *parse_refspec_internal(int nr_refspec, const char **refspec, int fetch, int verify)
{
	int i;
	struct refspec *rs = xcalloc(nr_refspec, sizeof(*rs));

	for (i = 0; i < nr_refspec; i++) {
		size_t llen;
		int is_glob;
		const char *lhs, *rhs;
		int flags;

		is_glob = 0;

		lhs = refspec[i];
		if (*lhs == '+') {
			rs[i].force = 1;
			lhs++;
		}

		rhs = strrchr(lhs, ':');

		/*
		 * Before going on, special case ":" (or "+:") as a refspec
		 * for pushing matching refs.
		 */
		if (!fetch && rhs == lhs && rhs[1] == '\0') {
			rs[i].matching = 1;
			continue;
		}

		if (rhs) {
			size_t rlen = strlen(++rhs);
			is_glob = (1 <= rlen && strchr(rhs, '*'));
			rs[i].dst = xstrndup(rhs, rlen);
		}

		llen = (rhs ? (rhs - lhs - 1) : strlen(lhs));
		if (1 <= llen && memchr(lhs, '*', llen)) {
			if ((rhs && !is_glob) || (!rhs && fetch))
				goto invalid;
			is_glob = 1;
		} else if (rhs && is_glob) {
			goto invalid;
		}

		rs[i].pattern = is_glob;
		rs[i].src = xstrndup(lhs, llen);
		flags = REFNAME_ALLOW_ONELEVEL | (is_glob ? REFNAME_REFSPEC_PATTERN : 0);

		if (fetch) {
			struct object_id unused;

			/* LHS */
			if (!*rs[i].src)
				; /* empty is ok; it means "HEAD" */
			else if (llen == GIT_SHA1_HEXSZ && !get_oid_hex(rs[i].src, &unused))
				rs[i].exact_sha1 = 1; /* ok */
			else if (!check_refname_format(rs[i].src, flags))
				; /* valid looking ref is ok */
			else
				goto invalid;
			/* RHS */
			if (!rs[i].dst)
				; /* missing is ok; it is the same as empty */
			else if (!*rs[i].dst)
				; /* empty is ok; it means "do not store" */
			else if (!check_refname_format(rs[i].dst, flags))
				; /* valid looking ref is ok */
			else
				goto invalid;
		} else {
			/*
			 * LHS
			 * - empty is allowed; it means delete.
			 * - when wildcarded, it must be a valid looking ref.
			 * - otherwise, it must be an extended SHA-1, but
			 *   there is no existing way to validate this.
			 */
			if (!*rs[i].src)
				; /* empty is ok */
			else if (is_glob) {
				if (check_refname_format(rs[i].src, flags))
					goto invalid;
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
			if (!rs[i].dst) {
				if (check_refname_format(rs[i].src, flags))
					goto invalid;
			} else if (!*rs[i].dst) {
				goto invalid;
			} else {
				if (check_refname_format(rs[i].dst, flags))
					goto invalid;
			}
		}
	}
	return rs;

 invalid:
	if (verify) {
		/*
		 * nr_refspec must be greater than zero and i must be valid
		 * since it is only possible to reach this point from within
		 * the for loop above.
		 */
		free_refspec(i+1, rs);
		return NULL;
	}
	die("Invalid refspec '%s'", refspec[i]);
}

int valid_fetch_refspec(const char *fetch_refspec_str)
{
	struct refspec *refspec;

	refspec = parse_refspec_internal(1, &fetch_refspec_str, 1, 1);
	free_refspec(1, refspec);
	return !!refspec;
}

struct refspec *parse_fetch_refspec(int nr_refspec, const char **refspec)
{
	return parse_refspec_internal(nr_refspec, refspec, 1, 0);
}

struct refspec *parse_push_refspec(int nr_refspec, const char **refspec)
{
	return parse_refspec_internal(nr_refspec, refspec, 0, 0);
}

void free_refspec(int nr_refspec, struct refspec *refspec)
{
	int i;

	if (!refspec)
		return;

	for (i = 0; i < nr_refspec; i++) {
		free(refspec[i].src);
		free(refspec[i].dst);
	}
	free(refspec);
}
