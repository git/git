#include "cache.h"
#include "tag.h"
#include "commit.h"
#include "tree.h"
#include "blob.h"
#include "tree-walk.h"
#include "refs.h"
#include "remote.h"
#include "dir.h"
#include "sha1-array.h"

static int get_sha1_oneline(const char *, unsigned char *, struct commit_list *);

typedef int (*disambiguate_hint_fn)(const struct object_id *, void *);

struct disambiguate_state {
	int len; /* length of prefix in hex chars */
	char hex_pfx[GIT_MAX_HEXSZ + 1];
	struct object_id bin_pfx;

	disambiguate_hint_fn fn;
	void *cb_data;
	struct object_id candidate;
	unsigned candidate_exists:1;
	unsigned candidate_checked:1;
	unsigned candidate_ok:1;
	unsigned disambiguate_fn_used:1;
	unsigned ambiguous:1;
	unsigned always_call_fn:1;
};

static void update_candidates(struct disambiguate_state *ds, const struct object_id *current)
{
	if (ds->always_call_fn) {
		ds->ambiguous = ds->fn(current, ds->cb_data) ? 1 : 0;
		return;
	}
	if (!ds->candidate_exists) {
		/* this is the first candidate */
		oidcpy(&ds->candidate, current);
		ds->candidate_exists = 1;
		return;
	} else if (!oidcmp(&ds->candidate, current)) {
		/* the same as what we already have seen */
		return;
	}

	if (!ds->fn) {
		/* cannot disambiguate between ds->candidate and current */
		ds->ambiguous = 1;
		return;
	}

	if (!ds->candidate_checked) {
		ds->candidate_ok = ds->fn(&ds->candidate, ds->cb_data);
		ds->disambiguate_fn_used = 1;
		ds->candidate_checked = 1;
	}

	if (!ds->candidate_ok) {
		/* discard the candidate; we know it does not satisfy fn */
		oidcpy(&ds->candidate, current);
		ds->candidate_checked = 0;
		return;
	}

	/* if we reach this point, we know ds->candidate satisfies fn */
	if (ds->fn(current, ds->cb_data)) {
		/*
		 * if both current and candidate satisfy fn, we cannot
		 * disambiguate.
		 */
		ds->candidate_ok = 0;
		ds->ambiguous = 1;
	}

	/* otherwise, current can be discarded and candidate is still good */
}

static void find_short_object_filename(struct disambiguate_state *ds)
{
	struct alternate_object_database *alt;
	char hex[GIT_MAX_HEXSZ];
	static struct alternate_object_database *fakeent;

	if (!fakeent) {
		/*
		 * Create a "fake" alternate object database that
		 * points to our own object database, to make it
		 * easier to get a temporary working space in
		 * alt->name/alt->base while iterating over the
		 * object databases including our own.
		 */
		fakeent = alloc_alt_odb(get_object_directory());
	}
	fakeent->next = alt_odb_list;

	xsnprintf(hex, sizeof(hex), "%.2s", ds->hex_pfx);
	for (alt = fakeent; alt && !ds->ambiguous; alt = alt->next) {
		struct strbuf *buf = alt_scratch_buf(alt);
		struct dirent *de;
		DIR *dir;

		strbuf_addf(buf, "%.2s/", ds->hex_pfx);
		dir = opendir(buf->buf);
		if (!dir)
			continue;

		while (!ds->ambiguous && (de = readdir(dir)) != NULL) {
			struct object_id oid;

			if (strlen(de->d_name) != GIT_SHA1_HEXSZ - 2)
				continue;
			if (memcmp(de->d_name, ds->hex_pfx + 2, ds->len - 2))
				continue;
			memcpy(hex + 2, de->d_name, GIT_SHA1_HEXSZ - 2);
			if (!get_oid_hex(hex, &oid))
				update_candidates(ds, &oid);
		}
		closedir(dir);
	}
}

static int match_sha(unsigned len, const unsigned char *a, const unsigned char *b)
{
	do {
		if (*a != *b)
			return 0;
		a++;
		b++;
		len -= 2;
	} while (len > 1);
	if (len)
		if ((*a ^ *b) & 0xf0)
			return 0;
	return 1;
}

static void unique_in_pack(struct packed_git *p,
			   struct disambiguate_state *ds)
{
	uint32_t num, last, i, first = 0;
	const struct object_id *current = NULL;

	open_pack_index(p);
	num = p->num_objects;
	last = num;
	while (first < last) {
		uint32_t mid = (first + last) / 2;
		const unsigned char *current;
		int cmp;

		current = nth_packed_object_sha1(p, mid);
		cmp = hashcmp(ds->bin_pfx.hash, current);
		if (!cmp) {
			first = mid;
			break;
		}
		if (cmp > 0) {
			first = mid+1;
			continue;
		}
		last = mid;
	}

	/*
	 * At this point, "first" is the location of the lowest object
	 * with an object name that could match "bin_pfx".  See if we have
	 * 0, 1 or more objects that actually match(es).
	 */
	for (i = first; i < num && !ds->ambiguous; i++) {
		struct object_id oid;
		current = nth_packed_object_oid(&oid, p, i);
		if (!match_sha(ds->len, ds->bin_pfx.hash, current->hash))
			break;
		update_candidates(ds, current);
	}
}

static void find_short_packed_object(struct disambiguate_state *ds)
{
	struct packed_git *p;

	prepare_packed_git();
	for (p = packed_git; p && !ds->ambiguous; p = p->next)
		unique_in_pack(p, ds);
}

#define SHORT_NAME_NOT_FOUND (-1)
#define SHORT_NAME_AMBIGUOUS (-2)

static int finish_object_disambiguation(struct disambiguate_state *ds,
					unsigned char *sha1)
{
	if (ds->ambiguous)
		return SHORT_NAME_AMBIGUOUS;

	if (!ds->candidate_exists)
		return SHORT_NAME_NOT_FOUND;

	if (!ds->candidate_checked)
		/*
		 * If this is the only candidate, there is no point
		 * calling the disambiguation hint callback.
		 *
		 * On the other hand, if the current candidate
		 * replaced an earlier candidate that did _not_ pass
		 * the disambiguation hint callback, then we do have
		 * more than one objects that match the short name
		 * given, so we should make sure this one matches;
		 * otherwise, if we discovered this one and the one
		 * that we previously discarded in the reverse order,
		 * we would end up showing different results in the
		 * same repository!
		 */
		ds->candidate_ok = (!ds->disambiguate_fn_used ||
				    ds->fn(&ds->candidate, ds->cb_data));

	if (!ds->candidate_ok)
		return SHORT_NAME_AMBIGUOUS;

	hashcpy(sha1, ds->candidate.hash);
	return 0;
}

static int disambiguate_commit_only(const struct object_id *oid, void *cb_data_unused)
{
	int kind = sha1_object_info(oid->hash, NULL);
	return kind == OBJ_COMMIT;
}

static int disambiguate_committish_only(const struct object_id *oid, void *cb_data_unused)
{
	struct object *obj;
	int kind;

	kind = sha1_object_info(oid->hash, NULL);
	if (kind == OBJ_COMMIT)
		return 1;
	if (kind != OBJ_TAG)
		return 0;

	/* We need to do this the hard way... */
	obj = deref_tag(parse_object(oid->hash), NULL, 0);
	if (obj && obj->type == OBJ_COMMIT)
		return 1;
	return 0;
}

static int disambiguate_tree_only(const struct object_id *oid, void *cb_data_unused)
{
	int kind = sha1_object_info(oid->hash, NULL);
	return kind == OBJ_TREE;
}

static int disambiguate_treeish_only(const struct object_id *oid, void *cb_data_unused)
{
	struct object *obj;
	int kind;

	kind = sha1_object_info(oid->hash, NULL);
	if (kind == OBJ_TREE || kind == OBJ_COMMIT)
		return 1;
	if (kind != OBJ_TAG)
		return 0;

	/* We need to do this the hard way... */
	obj = deref_tag(parse_object(oid->hash), NULL, 0);
	if (obj && (obj->type == OBJ_TREE || obj->type == OBJ_COMMIT))
		return 1;
	return 0;
}

static int disambiguate_blob_only(const struct object_id *oid, void *cb_data_unused)
{
	int kind = sha1_object_info(oid->hash, NULL);
	return kind == OBJ_BLOB;
}

static disambiguate_hint_fn default_disambiguate_hint;

int set_disambiguate_hint_config(const char *var, const char *value)
{
	static const struct {
		const char *name;
		disambiguate_hint_fn fn;
	} hints[] = {
		{ "none", NULL },
		{ "commit", disambiguate_commit_only },
		{ "committish", disambiguate_committish_only },
		{ "tree", disambiguate_tree_only },
		{ "treeish", disambiguate_treeish_only },
		{ "blob", disambiguate_blob_only }
	};
	int i;

	if (!value)
		return config_error_nonbool(var);

	for (i = 0; i < ARRAY_SIZE(hints); i++) {
		if (!strcasecmp(value, hints[i].name)) {
			default_disambiguate_hint = hints[i].fn;
			return 0;
		}
	}

	return error("unknown hint type for '%s': %s", var, value);
}

static int init_object_disambiguation(const char *name, int len,
				      struct disambiguate_state *ds)
{
	int i;

	if (len < MINIMUM_ABBREV || len > GIT_SHA1_HEXSZ)
		return -1;

	memset(ds, 0, sizeof(*ds));

	for (i = 0; i < len ;i++) {
		unsigned char c = name[i];
		unsigned char val;
		if (c >= '0' && c <= '9')
			val = c - '0';
		else if (c >= 'a' && c <= 'f')
			val = c - 'a' + 10;
		else if (c >= 'A' && c <='F') {
			val = c - 'A' + 10;
			c -= 'A' - 'a';
		}
		else
			return -1;
		ds->hex_pfx[i] = c;
		if (!(i & 1))
			val <<= 4;
		ds->bin_pfx.hash[i >> 1] |= val;
	}

	ds->len = len;
	ds->hex_pfx[len] = '\0';
	prepare_alt_odb();
	return 0;
}

static int show_ambiguous_object(const struct object_id *oid, void *data)
{
	const struct disambiguate_state *ds = data;
	struct strbuf desc = STRBUF_INIT;
	int type;


	if (ds->fn && !ds->fn(oid, ds->cb_data))
		return 0;

	type = sha1_object_info(oid->hash, NULL);
	if (type == OBJ_COMMIT) {
		struct commit *commit = lookup_commit(oid->hash);
		if (commit) {
			struct pretty_print_context pp = {0};
			pp.date_mode.type = DATE_SHORT;
			format_commit_message(commit, " %ad - %s", &desc, &pp);
		}
	} else if (type == OBJ_TAG) {
		struct tag *tag = lookup_tag(oid->hash);
		if (!parse_tag(tag) && tag->tag)
			strbuf_addf(&desc, " %s", tag->tag);
	}

	advise("  %s %s%s",
	       find_unique_abbrev(oid->hash, DEFAULT_ABBREV),
	       typename(type) ? typename(type) : "unknown type",
	       desc.buf);

	strbuf_release(&desc);
	return 0;
}

static int get_short_sha1(const char *name, int len, unsigned char *sha1,
			  unsigned flags)
{
	int status;
	struct disambiguate_state ds;
	int quietly = !!(flags & GET_SHA1_QUIETLY);

	if (init_object_disambiguation(name, len, &ds) < 0)
		return -1;

	if (HAS_MULTI_BITS(flags & GET_SHA1_DISAMBIGUATORS))
		die("BUG: multiple get_short_sha1 disambiguator flags");

	if (flags & GET_SHA1_COMMIT)
		ds.fn = disambiguate_commit_only;
	else if (flags & GET_SHA1_COMMITTISH)
		ds.fn = disambiguate_committish_only;
	else if (flags & GET_SHA1_TREE)
		ds.fn = disambiguate_tree_only;
	else if (flags & GET_SHA1_TREEISH)
		ds.fn = disambiguate_treeish_only;
	else if (flags & GET_SHA1_BLOB)
		ds.fn = disambiguate_blob_only;
	else
		ds.fn = default_disambiguate_hint;

	find_short_object_filename(&ds);
	find_short_packed_object(&ds);
	status = finish_object_disambiguation(&ds, sha1);

	if (!quietly && (status == SHORT_NAME_AMBIGUOUS)) {
		error(_("short SHA1 %s is ambiguous"), ds.hex_pfx);

		/*
		 * We may still have ambiguity if we simply saw a series of
		 * candidates that did not satisfy our hint function. In
		 * that case, we still want to show them, so disable the hint
		 * function entirely.
		 */
		if (!ds.ambiguous)
			ds.fn = NULL;

		advise(_("The candidates are:"));
		for_each_abbrev(ds.hex_pfx, show_ambiguous_object, &ds);
	}

	return status;
}

static int collect_ambiguous(const struct object_id *oid, void *data)
{
	oid_array_append(data, oid);
	return 0;
}

int for_each_abbrev(const char *prefix, each_abbrev_fn fn, void *cb_data)
{
	struct oid_array collect = OID_ARRAY_INIT;
	struct disambiguate_state ds;
	int ret;

	if (init_object_disambiguation(prefix, strlen(prefix), &ds) < 0)
		return -1;

	ds.always_call_fn = 1;
	ds.fn = collect_ambiguous;
	ds.cb_data = &collect;
	find_short_object_filename(&ds);
	find_short_packed_object(&ds);

	ret = oid_array_for_each_unique(&collect, fn, cb_data);
	oid_array_clear(&collect);
	return ret;
}

/*
 * Return the slot of the most-significant bit set in "val". There are various
 * ways to do this quickly with fls() or __builtin_clzl(), but speed is
 * probably not a big deal here.
 */
static unsigned msb(unsigned long val)
{
	unsigned r = 0;
	while (val >>= 1)
		r++;
	return r;
}

int find_unique_abbrev_r(char *hex, const unsigned char *sha1, int len)
{
	int status, exists;

	if (len < 0) {
		unsigned long count = approximate_object_count();
		/*
		 * Add one because the MSB only tells us the highest bit set,
		 * not including the value of all the _other_ bits (so "15"
		 * is only one off of 2^4, but the MSB is the 3rd bit.
		 */
		len = msb(count) + 1;
		/*
		 * We now know we have on the order of 2^len objects, which
		 * expects a collision at 2^(len/2). But we also care about hex
		 * chars, not bits, and there are 4 bits per hex. So all
		 * together we need to divide by 2; but we also want to round
		 * odd numbers up, hence adding one before dividing.
		 */
		len = (len + 1) / 2;
		/*
		 * For very small repos, we stick with our regular fallback.
		 */
		if (len < FALLBACK_DEFAULT_ABBREV)
			len = FALLBACK_DEFAULT_ABBREV;
	}

	sha1_to_hex_r(hex, sha1);
	if (len == 40 || !len)
		return 40;
	exists = has_sha1_file(sha1);
	while (len < 40) {
		unsigned char sha1_ret[20];
		status = get_short_sha1(hex, len, sha1_ret, GET_SHA1_QUIETLY);
		if (exists
		    ? !status
		    : status == SHORT_NAME_NOT_FOUND) {
			hex[len] = 0;
			return len;
		}
		len++;
	}
	return len;
}

const char *find_unique_abbrev(const unsigned char *sha1, int len)
{
	static int bufno;
	static char hexbuffer[4][GIT_MAX_HEXSZ + 1];
	char *hex = hexbuffer[bufno];
	bufno = (bufno + 1) % ARRAY_SIZE(hexbuffer);
	find_unique_abbrev_r(hex, sha1, len);
	return hex;
}

static int ambiguous_path(const char *path, int len)
{
	int slash = 1;
	int cnt;

	for (cnt = 0; cnt < len; cnt++) {
		switch (*path++) {
		case '\0':
			break;
		case '/':
			if (slash)
				break;
			slash = 1;
			continue;
		case '.':
			continue;
		default:
			slash = 0;
			continue;
		}
		break;
	}
	return slash;
}

static inline int at_mark(const char *string, int len,
			  const char **suffix, int nr)
{
	int i;

	for (i = 0; i < nr; i++) {
		int suffix_len = strlen(suffix[i]);
		if (suffix_len <= len
		    && !memcmp(string, suffix[i], suffix_len))
			return suffix_len;
	}
	return 0;
}

static inline int upstream_mark(const char *string, int len)
{
	const char *suffix[] = { "@{upstream}", "@{u}" };
	return at_mark(string, len, suffix, ARRAY_SIZE(suffix));
}

static inline int push_mark(const char *string, int len)
{
	const char *suffix[] = { "@{push}" };
	return at_mark(string, len, suffix, ARRAY_SIZE(suffix));
}

static int get_sha1_1(const char *name, int len, unsigned char *sha1, unsigned lookup_flags);
static int interpret_nth_prior_checkout(const char *name, int namelen, struct strbuf *buf);

static int get_sha1_basic(const char *str, int len, unsigned char *sha1,
			  unsigned int flags)
{
	static const char *warn_msg = "refname '%.*s' is ambiguous.";
	static const char *object_name_msg = N_(
	"Git normally never creates a ref that ends with 40 hex characters\n"
	"because it will be ignored when you just specify 40-hex. These refs\n"
	"may be created by mistake. For example,\n"
	"\n"
	"  git checkout -b $br $(git rev-parse ...)\n"
	"\n"
	"where \"$br\" is somehow empty and a 40-hex ref is created. Please\n"
	"examine these refs and maybe delete them. Turn this message off by\n"
	"running \"git config advice.objectNameWarning false\"");
	unsigned char tmp_sha1[20];
	char *real_ref = NULL;
	int refs_found = 0;
	int at, reflog_len, nth_prior = 0;

	if (len == 40 && !get_sha1_hex(str, sha1)) {
		if (warn_ambiguous_refs && warn_on_object_refname_ambiguity) {
			refs_found = dwim_ref(str, len, tmp_sha1, &real_ref);
			if (refs_found > 0) {
				warning(warn_msg, len, str);
				if (advice_object_name_warning)
					fprintf(stderr, "%s\n", _(object_name_msg));
			}
			free(real_ref);
		}
		return 0;
	}

	/* basic@{time or number or -number} format to query ref-log */
	reflog_len = at = 0;
	if (len && str[len-1] == '}') {
		for (at = len-4; at >= 0; at--) {
			if (str[at] == '@' && str[at+1] == '{') {
				if (str[at+2] == '-') {
					if (at != 0)
						/* @{-N} not at start */
						return -1;
					nth_prior = 1;
					continue;
				}
				if (!upstream_mark(str + at, len - at) &&
				    !push_mark(str + at, len - at)) {
					reflog_len = (len-1) - (at+2);
					len = at;
				}
				break;
			}
		}
	}

	/* Accept only unambiguous ref paths. */
	if (len && ambiguous_path(str, len))
		return -1;

	if (nth_prior) {
		struct strbuf buf = STRBUF_INIT;
		int detached;

		if (interpret_nth_prior_checkout(str, len, &buf) > 0) {
			detached = (buf.len == 40 && !get_sha1_hex(buf.buf, sha1));
			strbuf_release(&buf);
			if (detached)
				return 0;
		}
	}

	if (!len && reflog_len)
		/* allow "@{...}" to mean the current branch reflog */
		refs_found = dwim_ref("HEAD", 4, sha1, &real_ref);
	else if (reflog_len)
		refs_found = dwim_log(str, len, sha1, &real_ref);
	else
		refs_found = dwim_ref(str, len, sha1, &real_ref);

	if (!refs_found)
		return -1;

	if (warn_ambiguous_refs && !(flags & GET_SHA1_QUIETLY) &&
	    (refs_found > 1 ||
	     !get_short_sha1(str, len, tmp_sha1, GET_SHA1_QUIETLY)))
		warning(warn_msg, len, str);

	if (reflog_len) {
		int nth, i;
		unsigned long at_time;
		unsigned long co_time;
		int co_tz, co_cnt;

		/* Is it asking for N-th entry, or approxidate? */
		for (i = nth = 0; 0 <= nth && i < reflog_len; i++) {
			char ch = str[at+2+i];
			if ('0' <= ch && ch <= '9')
				nth = nth * 10 + ch - '0';
			else
				nth = -1;
		}
		if (100000000 <= nth) {
			at_time = nth;
			nth = -1;
		} else if (0 <= nth)
			at_time = 0;
		else {
			int errors = 0;
			char *tmp = xstrndup(str + at + 2, reflog_len);
			at_time = approxidate_careful(tmp, &errors);
			free(tmp);
			if (errors) {
				free(real_ref);
				return -1;
			}
		}
		if (read_ref_at(real_ref, flags, at_time, nth, sha1, NULL,
				&co_time, &co_tz, &co_cnt)) {
			if (!len) {
				if (starts_with(real_ref, "refs/heads/")) {
					str = real_ref + 11;
					len = strlen(real_ref + 11);
				} else {
					/* detached HEAD */
					str = "HEAD";
					len = 4;
				}
			}
			if (at_time) {
				if (!(flags & GET_SHA1_QUIETLY)) {
					warning("Log for '%.*s' only goes "
						"back to %s.", len, str,
						show_date(co_time, co_tz, DATE_MODE(RFC2822)));
				}
			} else {
				if (flags & GET_SHA1_QUIETLY) {
					exit(128);
				}
				die("Log for '%.*s' only has %d entries.",
				    len, str, co_cnt);
			}
		}
	}

	free(real_ref);
	return 0;
}

static int get_parent(const char *name, int len,
		      unsigned char *result, int idx)
{
	unsigned char sha1[20];
	int ret = get_sha1_1(name, len, sha1, GET_SHA1_COMMITTISH);
	struct commit *commit;
	struct commit_list *p;

	if (ret)
		return ret;
	commit = lookup_commit_reference(sha1);
	if (parse_commit(commit))
		return -1;
	if (!idx) {
		hashcpy(result, commit->object.oid.hash);
		return 0;
	}
	p = commit->parents;
	while (p) {
		if (!--idx) {
			hashcpy(result, p->item->object.oid.hash);
			return 0;
		}
		p = p->next;
	}
	return -1;
}

static int get_nth_ancestor(const char *name, int len,
			    unsigned char *result, int generation)
{
	unsigned char sha1[20];
	struct commit *commit;
	int ret;

	ret = get_sha1_1(name, len, sha1, GET_SHA1_COMMITTISH);
	if (ret)
		return ret;
	commit = lookup_commit_reference(sha1);
	if (!commit)
		return -1;

	while (generation--) {
		if (parse_commit(commit) || !commit->parents)
			return -1;
		commit = commit->parents->item;
	}
	hashcpy(result, commit->object.oid.hash);
	return 0;
}

struct object *peel_to_type(const char *name, int namelen,
			    struct object *o, enum object_type expected_type)
{
	if (name && !namelen)
		namelen = strlen(name);
	while (1) {
		if (!o || (!o->parsed && !parse_object(o->oid.hash)))
			return NULL;
		if (expected_type == OBJ_ANY || o->type == expected_type)
			return o;
		if (o->type == OBJ_TAG)
			o = ((struct tag*) o)->tagged;
		else if (o->type == OBJ_COMMIT)
			o = &(((struct commit *) o)->tree->object);
		else {
			if (name)
				error("%.*s: expected %s type, but the object "
				      "dereferences to %s type",
				      namelen, name, typename(expected_type),
				      typename(o->type));
			return NULL;
		}
	}
}

static int peel_onion(const char *name, int len, unsigned char *sha1,
		      unsigned lookup_flags)
{
	unsigned char outer[20];
	const char *sp;
	unsigned int expected_type = 0;
	struct object *o;

	/*
	 * "ref^{type}" dereferences ref repeatedly until you cannot
	 * dereference anymore, or you get an object of given type,
	 * whichever comes first.  "ref^{}" means just dereference
	 * tags until you get a non-tag.  "ref^0" is a shorthand for
	 * "ref^{commit}".  "commit^{tree}" could be used to find the
	 * top-level tree of the given commit.
	 */
	if (len < 4 || name[len-1] != '}')
		return -1;

	for (sp = name + len - 1; name <= sp; sp--) {
		int ch = *sp;
		if (ch == '{' && name < sp && sp[-1] == '^')
			break;
	}
	if (sp <= name)
		return -1;

	sp++; /* beginning of type name, or closing brace for empty */
	if (starts_with(sp, "commit}"))
		expected_type = OBJ_COMMIT;
	else if (starts_with(sp, "tag}"))
		expected_type = OBJ_TAG;
	else if (starts_with(sp, "tree}"))
		expected_type = OBJ_TREE;
	else if (starts_with(sp, "blob}"))
		expected_type = OBJ_BLOB;
	else if (starts_with(sp, "object}"))
		expected_type = OBJ_ANY;
	else if (sp[0] == '}')
		expected_type = OBJ_NONE;
	else if (sp[0] == '/')
		expected_type = OBJ_COMMIT;
	else
		return -1;

	lookup_flags &= ~GET_SHA1_DISAMBIGUATORS;
	if (expected_type == OBJ_COMMIT)
		lookup_flags |= GET_SHA1_COMMITTISH;
	else if (expected_type == OBJ_TREE)
		lookup_flags |= GET_SHA1_TREEISH;

	if (get_sha1_1(name, sp - name - 2, outer, lookup_flags))
		return -1;

	o = parse_object(outer);
	if (!o)
		return -1;
	if (!expected_type) {
		o = deref_tag(o, name, sp - name - 2);
		if (!o || (!o->parsed && !parse_object(o->oid.hash)))
			return -1;
		hashcpy(sha1, o->oid.hash);
		return 0;
	}

	/*
	 * At this point, the syntax look correct, so
	 * if we do not get the needed object, we should
	 * barf.
	 */
	o = peel_to_type(name, len, o, expected_type);
	if (!o)
		return -1;

	hashcpy(sha1, o->oid.hash);
	if (sp[0] == '/') {
		/* "$commit^{/foo}" */
		char *prefix;
		int ret;
		struct commit_list *list = NULL;

		/*
		 * $commit^{/}. Some regex implementation may reject.
		 * We don't need regex anyway. '' pattern always matches.
		 */
		if (sp[1] == '}')
			return 0;

		prefix = xstrndup(sp + 1, name + len - 1 - (sp + 1));
		commit_list_insert((struct commit *)o, &list);
		ret = get_sha1_oneline(prefix, sha1, list);
		free(prefix);
		return ret;
	}
	return 0;
}

static int get_describe_name(const char *name, int len, unsigned char *sha1)
{
	const char *cp;
	unsigned flags = GET_SHA1_QUIETLY | GET_SHA1_COMMIT;

	for (cp = name + len - 1; name + 2 <= cp; cp--) {
		char ch = *cp;
		if (!isxdigit(ch)) {
			/* We must be looking at g in "SOMETHING-g"
			 * for it to be describe output.
			 */
			if (ch == 'g' && cp[-1] == '-') {
				cp++;
				len -= cp - name;
				return get_short_sha1(cp, len, sha1, flags);
			}
		}
	}
	return -1;
}

static int get_sha1_1(const char *name, int len, unsigned char *sha1, unsigned lookup_flags)
{
	int ret, has_suffix;
	const char *cp;

	/*
	 * "name~3" is "name^^^", "name~" is "name~1", and "name^" is "name^1".
	 */
	has_suffix = 0;
	for (cp = name + len - 1; name <= cp; cp--) {
		int ch = *cp;
		if ('0' <= ch && ch <= '9')
			continue;
		if (ch == '~' || ch == '^')
			has_suffix = ch;
		break;
	}

	if (has_suffix) {
		int num = 0;
		int len1 = cp - name;
		cp++;
		while (cp < name + len)
			num = num * 10 + *cp++ - '0';
		if (!num && len1 == len - 1)
			num = 1;
		if (has_suffix == '^')
			return get_parent(name, len1, sha1, num);
		/* else if (has_suffix == '~') -- goes without saying */
		return get_nth_ancestor(name, len1, sha1, num);
	}

	ret = peel_onion(name, len, sha1, lookup_flags);
	if (!ret)
		return 0;

	ret = get_sha1_basic(name, len, sha1, lookup_flags);
	if (!ret)
		return 0;

	/* It could be describe output that is "SOMETHING-gXXXX" */
	ret = get_describe_name(name, len, sha1);
	if (!ret)
		return 0;

	return get_short_sha1(name, len, sha1, lookup_flags);
}

/*
 * This interprets names like ':/Initial revision of "git"' by searching
 * through history and returning the first commit whose message starts
 * the given regular expression.
 *
 * For negative-matching, prefix the pattern-part with '!-', like: ':/!-WIP'.
 *
 * For a literal '!' character at the beginning of a pattern, you have to repeat
 * that, like: ':/!!foo'
 *
 * For future extension, all other sequences beginning with ':/!' are reserved.
 */

/* Remember to update object flag allocation in object.h */
#define ONELINE_SEEN (1u<<20)

static int handle_one_ref(const char *path, const struct object_id *oid,
			  int flag, void *cb_data)
{
	struct commit_list **list = cb_data;
	struct object *object = parse_object(oid->hash);
	if (!object)
		return 0;
	if (object->type == OBJ_TAG) {
		object = deref_tag(object, path, strlen(path));
		if (!object)
			return 0;
	}
	if (object->type != OBJ_COMMIT)
		return 0;
	commit_list_insert((struct commit *)object, list);
	return 0;
}

static int get_sha1_oneline(const char *prefix, unsigned char *sha1,
			    struct commit_list *list)
{
	struct commit_list *backup = NULL, *l;
	int found = 0;
	int negative = 0;
	regex_t regex;

	if (prefix[0] == '!') {
		prefix++;

		if (prefix[0] == '-') {
			prefix++;
			negative = 1;
		} else if (prefix[0] != '!') {
			return -1;
		}
	}

	if (regcomp(&regex, prefix, REG_EXTENDED))
		return -1;

	for (l = list; l; l = l->next) {
		l->item->object.flags |= ONELINE_SEEN;
		commit_list_insert(l->item, &backup);
	}
	while (list) {
		const char *p, *buf;
		struct commit *commit;
		int matches;

		commit = pop_most_recent_commit(&list, ONELINE_SEEN);
		if (!parse_object(commit->object.oid.hash))
			continue;
		buf = get_commit_buffer(commit, NULL);
		p = strstr(buf, "\n\n");
		matches = negative ^ (p && !regexec(&regex, p + 2, 0, NULL, 0));
		unuse_commit_buffer(commit, buf);

		if (matches) {
			hashcpy(sha1, commit->object.oid.hash);
			found = 1;
			break;
		}
	}
	regfree(&regex);
	free_commit_list(list);
	for (l = backup; l; l = l->next)
		clear_commit_marks(l->item, ONELINE_SEEN);
	free_commit_list(backup);
	return found ? 0 : -1;
}

struct grab_nth_branch_switch_cbdata {
	int remaining;
	struct strbuf buf;
};

static int grab_nth_branch_switch(struct object_id *ooid, struct object_id *noid,
				  const char *email, unsigned long timestamp, int tz,
				  const char *message, void *cb_data)
{
	struct grab_nth_branch_switch_cbdata *cb = cb_data;
	const char *match = NULL, *target = NULL;
	size_t len;

	if (skip_prefix(message, "checkout: moving from ", &match))
		target = strstr(match, " to ");

	if (!match || !target)
		return 0;
	if (--(cb->remaining) == 0) {
		len = target - match;
		strbuf_reset(&cb->buf);
		strbuf_add(&cb->buf, match, len);
		return 1; /* we are done */
	}
	return 0;
}

/*
 * Parse @{-N} syntax, return the number of characters parsed
 * if successful; otherwise signal an error with negative value.
 */
static int interpret_nth_prior_checkout(const char *name, int namelen,
					struct strbuf *buf)
{
	long nth;
	int retval;
	struct grab_nth_branch_switch_cbdata cb;
	const char *brace;
	char *num_end;

	if (namelen < 4)
		return -1;
	if (name[0] != '@' || name[1] != '{' || name[2] != '-')
		return -1;
	brace = memchr(name, '}', namelen);
	if (!brace)
		return -1;
	nth = strtol(name + 3, &num_end, 10);
	if (num_end != brace)
		return -1;
	if (nth <= 0)
		return -1;
	cb.remaining = nth;
	strbuf_init(&cb.buf, 20);

	retval = 0;
	if (0 < for_each_reflog_ent_reverse("HEAD", grab_nth_branch_switch, &cb)) {
		strbuf_reset(buf);
		strbuf_addbuf(buf, &cb.buf);
		retval = brace - name + 1;
	}

	strbuf_release(&cb.buf);
	return retval;
}

int get_oid_mb(const char *name, struct object_id *oid)
{
	struct commit *one, *two;
	struct commit_list *mbs;
	struct object_id oid_tmp;
	const char *dots;
	int st;

	dots = strstr(name, "...");
	if (!dots)
		return get_oid(name, oid);
	if (dots == name)
		st = get_oid("HEAD", &oid_tmp);
	else {
		struct strbuf sb;
		strbuf_init(&sb, dots - name);
		strbuf_add(&sb, name, dots - name);
		st = get_sha1_committish(sb.buf, oid_tmp.hash);
		strbuf_release(&sb);
	}
	if (st)
		return st;
	one = lookup_commit_reference_gently(oid_tmp.hash, 0);
	if (!one)
		return -1;

	if (get_sha1_committish(dots[3] ? (dots + 3) : "HEAD", oid_tmp.hash))
		return -1;
	two = lookup_commit_reference_gently(oid_tmp.hash, 0);
	if (!two)
		return -1;
	mbs = get_merge_bases(one, two);
	if (!mbs || mbs->next)
		st = -1;
	else {
		st = 0;
		oidcpy(oid, &mbs->item->object.oid);
	}
	free_commit_list(mbs);
	return st;
}

/* parse @something syntax, when 'something' is not {.*} */
static int interpret_empty_at(const char *name, int namelen, int len, struct strbuf *buf)
{
	const char *next;

	if (len || name[1] == '{')
		return -1;

	/* make sure it's a single @, or @@{.*}, not @foo */
	next = memchr(name + len + 1, '@', namelen - len - 1);
	if (next && next[1] != '{')
		return -1;
	if (!next)
		next = name + namelen;
	if (next != name + 1)
		return -1;

	strbuf_reset(buf);
	strbuf_add(buf, "HEAD", 4);
	return 1;
}

static int reinterpret(const char *name, int namelen, int len,
		       struct strbuf *buf, unsigned allowed)
{
	/* we have extra data, which might need further processing */
	struct strbuf tmp = STRBUF_INIT;
	int used = buf->len;
	int ret;

	strbuf_add(buf, name + len, namelen - len);
	ret = interpret_branch_name(buf->buf, buf->len, &tmp, allowed);
	/* that data was not interpreted, remove our cruft */
	if (ret < 0) {
		strbuf_setlen(buf, used);
		return len;
	}
	strbuf_reset(buf);
	strbuf_addbuf(buf, &tmp);
	strbuf_release(&tmp);
	/* tweak for size of {-N} versus expanded ref name */
	return ret - used + len;
}

static void set_shortened_ref(struct strbuf *buf, const char *ref)
{
	char *s = shorten_unambiguous_ref(ref, 0);
	strbuf_reset(buf);
	strbuf_addstr(buf, s);
	free(s);
}

static int branch_interpret_allowed(const char *refname, unsigned allowed)
{
	if (!allowed)
		return 1;

	if ((allowed & INTERPRET_BRANCH_LOCAL) &&
	    starts_with(refname, "refs/heads/"))
		return 1;
	if ((allowed & INTERPRET_BRANCH_REMOTE) &&
	    starts_with(refname, "refs/remotes/"))
		return 1;

	return 0;
}

static int interpret_branch_mark(const char *name, int namelen,
				 int at, struct strbuf *buf,
				 int (*get_mark)(const char *, int),
				 const char *(*get_data)(struct branch *,
							 struct strbuf *),
				 unsigned allowed)
{
	int len;
	struct branch *branch;
	struct strbuf err = STRBUF_INIT;
	const char *value;

	len = get_mark(name + at, namelen - at);
	if (!len)
		return -1;

	if (memchr(name, ':', at))
		return -1;

	if (at) {
		char *name_str = xmemdupz(name, at);
		branch = branch_get(name_str);
		free(name_str);
	} else
		branch = branch_get(NULL);

	value = get_data(branch, &err);
	if (!value)
		die("%s", err.buf);

	if (!branch_interpret_allowed(value, allowed))
		return -1;

	set_shortened_ref(buf, value);
	return len + at;
}

int interpret_branch_name(const char *name, int namelen, struct strbuf *buf,
			  unsigned allowed)
{
	char *at;
	const char *start;
	int len;

	if (!namelen)
		namelen = strlen(name);

	if (!allowed || (allowed & INTERPRET_BRANCH_LOCAL)) {
		len = interpret_nth_prior_checkout(name, namelen, buf);
		if (!len) {
			return len; /* syntax Ok, not enough switches */
		} else if (len > 0) {
			if (len == namelen)
				return len; /* consumed all */
			else
				return reinterpret(name, namelen, len, buf, allowed);
		}
	}

	for (start = name;
	     (at = memchr(start, '@', namelen - (start - name)));
	     start = at + 1) {

		if (!allowed || (allowed & INTERPRET_BRANCH_HEAD)) {
			len = interpret_empty_at(name, namelen, at - name, buf);
			if (len > 0)
				return reinterpret(name, namelen, len, buf,
						   allowed);
		}

		len = interpret_branch_mark(name, namelen, at - name, buf,
					    upstream_mark, branch_get_upstream,
					    allowed);
		if (len > 0)
			return len;

		len = interpret_branch_mark(name, namelen, at - name, buf,
					    push_mark, branch_get_push,
					    allowed);
		if (len > 0)
			return len;
	}

	return -1;
}

void strbuf_branchname(struct strbuf *sb, const char *name, unsigned allowed)
{
	int len = strlen(name);
	int used = interpret_branch_name(name, len, sb, allowed);

	if (used < 0)
		used = 0;
	strbuf_add(sb, name + used, len - used);
}

int strbuf_check_branch_ref(struct strbuf *sb, const char *name)
{
	strbuf_branchname(sb, name, INTERPRET_BRANCH_LOCAL);
	if (name[0] == '-')
		return -1;
	strbuf_splice(sb, 0, 0, "refs/heads/", 11);
	return check_refname_format(sb->buf, 0);
}

/*
 * This is like "get_sha1_basic()", except it allows "sha1 expressions",
 * notably "xyz^" for "parent of xyz"
 */
int get_sha1(const char *name, unsigned char *sha1)
{
	struct object_context unused;
	return get_sha1_with_context(name, 0, sha1, &unused);
}

/*
 * This is like "get_sha1()", but for struct object_id.
 */
int get_oid(const char *name, struct object_id *oid)
{
	return get_sha1(name, oid->hash);
}


/*
 * Many callers know that the user meant to name a commit-ish by
 * syntactical positions where the object name appears.  Calling this
 * function allows the machinery to disambiguate shorter-than-unique
 * abbreviated object names between commit-ish and others.
 *
 * Note that this does NOT error out when the named object is not a
 * commit-ish. It is merely to give a hint to the disambiguation
 * machinery.
 */
int get_sha1_committish(const char *name, unsigned char *sha1)
{
	struct object_context unused;
	return get_sha1_with_context(name, GET_SHA1_COMMITTISH,
				     sha1, &unused);
}

int get_sha1_treeish(const char *name, unsigned char *sha1)
{
	struct object_context unused;
	return get_sha1_with_context(name, GET_SHA1_TREEISH,
				     sha1, &unused);
}

int get_sha1_commit(const char *name, unsigned char *sha1)
{
	struct object_context unused;
	return get_sha1_with_context(name, GET_SHA1_COMMIT,
				     sha1, &unused);
}

int get_sha1_tree(const char *name, unsigned char *sha1)
{
	struct object_context unused;
	return get_sha1_with_context(name, GET_SHA1_TREE,
				     sha1, &unused);
}

int get_sha1_blob(const char *name, unsigned char *sha1)
{
	struct object_context unused;
	return get_sha1_with_context(name, GET_SHA1_BLOB,
				     sha1, &unused);
}

/* Must be called only when object_name:filename doesn't exist. */
static void diagnose_invalid_sha1_path(const char *prefix,
				       const char *filename,
				       const unsigned char *tree_sha1,
				       const char *object_name,
				       int object_name_len)
{
	unsigned char sha1[20];
	unsigned mode;

	if (!prefix)
		prefix = "";

	if (file_exists(filename))
		die("Path '%s' exists on disk, but not in '%.*s'.",
		    filename, object_name_len, object_name);
	if (errno == ENOENT || errno == ENOTDIR) {
		char *fullname = xstrfmt("%s%s", prefix, filename);

		if (!get_tree_entry(tree_sha1, fullname,
				    sha1, &mode)) {
			die("Path '%s' exists, but not '%s'.\n"
			    "Did you mean '%.*s:%s' aka '%.*s:./%s'?",
			    fullname,
			    filename,
			    object_name_len, object_name,
			    fullname,
			    object_name_len, object_name,
			    filename);
		}
		die("Path '%s' does not exist in '%.*s'",
		    filename, object_name_len, object_name);
	}
}

/* Must be called only when :stage:filename doesn't exist. */
static void diagnose_invalid_index_path(int stage,
					const char *prefix,
					const char *filename)
{
	const struct cache_entry *ce;
	int pos;
	unsigned namelen = strlen(filename);
	struct strbuf fullname = STRBUF_INIT;

	if (!prefix)
		prefix = "";

	/* Wrong stage number? */
	pos = cache_name_pos(filename, namelen);
	if (pos < 0)
		pos = -pos - 1;
	if (pos < active_nr) {
		ce = active_cache[pos];
		if (ce_namelen(ce) == namelen &&
		    !memcmp(ce->name, filename, namelen))
			die("Path '%s' is in the index, but not at stage %d.\n"
			    "Did you mean ':%d:%s'?",
			    filename, stage,
			    ce_stage(ce), filename);
	}

	/* Confusion between relative and absolute filenames? */
	strbuf_addstr(&fullname, prefix);
	strbuf_addstr(&fullname, filename);
	pos = cache_name_pos(fullname.buf, fullname.len);
	if (pos < 0)
		pos = -pos - 1;
	if (pos < active_nr) {
		ce = active_cache[pos];
		if (ce_namelen(ce) == fullname.len &&
		    !memcmp(ce->name, fullname.buf, fullname.len))
			die("Path '%s' is in the index, but not '%s'.\n"
			    "Did you mean ':%d:%s' aka ':%d:./%s'?",
			    fullname.buf, filename,
			    ce_stage(ce), fullname.buf,
			    ce_stage(ce), filename);
	}

	if (file_exists(filename))
		die("Path '%s' exists on disk, but not in the index.", filename);
	if (errno == ENOENT || errno == ENOTDIR)
		die("Path '%s' does not exist (neither on disk nor in the index).",
		    filename);

	strbuf_release(&fullname);
}


static char *resolve_relative_path(const char *rel)
{
	if (!starts_with(rel, "./") && !starts_with(rel, "../"))
		return NULL;

	if (!is_inside_work_tree())
		die("relative path syntax can't be used outside working tree.");

	/* die() inside prefix_path() if resolved path is outside worktree */
	return prefix_path(startup_info->prefix,
			   startup_info->prefix ? strlen(startup_info->prefix) : 0,
			   rel);
}

static int get_sha1_with_context_1(const char *name,
				   unsigned flags,
				   const char *prefix,
				   unsigned char *sha1,
				   struct object_context *oc)
{
	int ret, bracket_depth;
	int namelen = strlen(name);
	const char *cp;
	int only_to_die = flags & GET_SHA1_ONLY_TO_DIE;

	if (only_to_die)
		flags |= GET_SHA1_QUIETLY;

	memset(oc, 0, sizeof(*oc));
	oc->mode = S_IFINVALID;
	ret = get_sha1_1(name, namelen, sha1, flags);
	if (!ret)
		return ret;
	/*
	 * sha1:path --> object name of path in ent sha1
	 * :path -> object name of absolute path in index
	 * :./path -> object name of path relative to cwd in index
	 * :[0-3]:path -> object name of path in index at stage
	 * :/foo -> recent commit matching foo
	 */
	if (name[0] == ':') {
		int stage = 0;
		const struct cache_entry *ce;
		char *new_path = NULL;
		int pos;
		if (!only_to_die && namelen > 2 && name[1] == '/') {
			struct commit_list *list = NULL;

			for_each_ref(handle_one_ref, &list);
			commit_list_sort_by_date(&list);
			return get_sha1_oneline(name + 2, sha1, list);
		}
		if (namelen < 3 ||
		    name[2] != ':' ||
		    name[1] < '0' || '3' < name[1])
			cp = name + 1;
		else {
			stage = name[1] - '0';
			cp = name + 3;
		}
		new_path = resolve_relative_path(cp);
		if (!new_path) {
			namelen = namelen - (cp - name);
		} else {
			cp = new_path;
			namelen = strlen(cp);
		}

		strlcpy(oc->path, cp, sizeof(oc->path));

		if (!active_cache)
			read_cache();
		pos = cache_name_pos(cp, namelen);
		if (pos < 0)
			pos = -pos - 1;
		while (pos < active_nr) {
			ce = active_cache[pos];
			if (ce_namelen(ce) != namelen ||
			    memcmp(ce->name, cp, namelen))
				break;
			if (ce_stage(ce) == stage) {
				hashcpy(sha1, ce->oid.hash);
				oc->mode = ce->ce_mode;
				free(new_path);
				return 0;
			}
			pos++;
		}
		if (only_to_die && name[1] && name[1] != '/')
			diagnose_invalid_index_path(stage, prefix, cp);
		free(new_path);
		return -1;
	}
	for (cp = name, bracket_depth = 0; *cp; cp++) {
		if (*cp == '{')
			bracket_depth++;
		else if (bracket_depth && *cp == '}')
			bracket_depth--;
		else if (!bracket_depth && *cp == ':')
			break;
	}
	if (*cp == ':') {
		unsigned char tree_sha1[20];
		int len = cp - name;
		unsigned sub_flags = flags;

		sub_flags &= ~GET_SHA1_DISAMBIGUATORS;
		sub_flags |= GET_SHA1_TREEISH;

		if (!get_sha1_1(name, len, tree_sha1, sub_flags)) {
			const char *filename = cp+1;
			char *new_filename = NULL;

			new_filename = resolve_relative_path(filename);
			if (new_filename)
				filename = new_filename;
			if (flags & GET_SHA1_FOLLOW_SYMLINKS) {
				ret = get_tree_entry_follow_symlinks(tree_sha1,
					filename, sha1, &oc->symlink_path,
					&oc->mode);
			} else {
				ret = get_tree_entry(tree_sha1, filename,
						     sha1, &oc->mode);
				if (ret && only_to_die) {
					diagnose_invalid_sha1_path(prefix,
								   filename,
								   tree_sha1,
								   name, len);
				}
			}
			hashcpy(oc->tree, tree_sha1);
			strlcpy(oc->path, filename, sizeof(oc->path));

			free(new_filename);
			return ret;
		} else {
			if (only_to_die)
				die("Invalid object name '%.*s'.", len, name);
		}
	}
	return ret;
}

/*
 * Call this function when you know "name" given by the end user must
 * name an object but it doesn't; the function _may_ die with a better
 * diagnostic message than "no such object 'name'", e.g. "Path 'doc' does not
 * exist in 'HEAD'" when given "HEAD:doc", or it may return in which case
 * you have a chance to diagnose the error further.
 */
void maybe_die_on_misspelt_object_name(const char *name, const char *prefix)
{
	struct object_context oc;
	unsigned char sha1[20];
	get_sha1_with_context_1(name, GET_SHA1_ONLY_TO_DIE, prefix, sha1, &oc);
}

int get_sha1_with_context(const char *str, unsigned flags, unsigned char *sha1, struct object_context *orc)
{
	if (flags & GET_SHA1_FOLLOW_SYMLINKS && flags & GET_SHA1_ONLY_TO_DIE)
		die("BUG: incompatible flags for get_sha1_with_context");
	return get_sha1_with_context_1(str, flags, NULL, sha1, orc);
}
