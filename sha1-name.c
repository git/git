#include "cache.h"
#include "config.h"
#include "tag.h"
#include "commit.h"
#include "tree.h"
#include "blob.h"
#include "tree-walk.h"
#include "refs.h"
#include "remote.h"
#include "dir.h"
#include "sha1-array.h"
#include "packfile.h"
#include "object-store.h"
#include "repository.h"
#include "submodule.h"
#include "midx.h"
#include "commit-reach.h"

static int get_oid_oneline(struct repository *r, const char *, struct object_id *, struct commit_list *);

typedef int (*disambiguate_hint_fn)(struct repository *, const struct object_id *, void *);

struct disambiguate_state {
	int len; /* length of prefix in hex chars */
	char hex_pfx[GIT_MAX_HEXSZ + 1];
	struct object_id bin_pfx;

	struct repository *repo;
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
		ds->ambiguous = ds->fn(ds->repo, current, ds->cb_data) ? 1 : 0;
		return;
	}
	if (!ds->candidate_exists) {
		/* this is the first candidate */
		oidcpy(&ds->candidate, current);
		ds->candidate_exists = 1;
		return;
	} else if (oideq(&ds->candidate, current)) {
		/* the same as what we already have seen */
		return;
	}

	if (!ds->fn) {
		/* cannot disambiguate between ds->candidate and current */
		ds->ambiguous = 1;
		return;
	}

	if (!ds->candidate_checked) {
		ds->candidate_ok = ds->fn(ds->repo, &ds->candidate, ds->cb_data);
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
	if (ds->fn(ds->repo, current, ds->cb_data)) {
		/*
		 * if both current and candidate satisfy fn, we cannot
		 * disambiguate.
		 */
		ds->candidate_ok = 0;
		ds->ambiguous = 1;
	}

	/* otherwise, current can be discarded and candidate is still good */
}

static int match_sha(unsigned, const unsigned char *, const unsigned char *);

static void find_short_object_filename(struct disambiguate_state *ds)
{
	struct object_directory *odb;

	for (odb = ds->repo->objects->odb; odb && !ds->ambiguous; odb = odb->next) {
		int pos;
		struct oid_array *loose_objects;

		loose_objects = odb_loose_cache(odb, &ds->bin_pfx);
		pos = oid_array_lookup(loose_objects, &ds->bin_pfx);
		if (pos < 0)
			pos = -1 - pos;
		while (!ds->ambiguous && pos < loose_objects->nr) {
			const struct object_id *oid;
			oid = loose_objects->oid + pos;
			if (!match_sha(ds->len, ds->bin_pfx.hash, oid->hash))
				break;
			update_candidates(ds, oid);
			pos++;
		}
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

static void unique_in_midx(struct multi_pack_index *m,
			   struct disambiguate_state *ds)
{
	uint32_t num, i, first = 0;
	const struct object_id *current = NULL;
	num = m->num_objects;

	if (!num)
		return;

	bsearch_midx(&ds->bin_pfx, m, &first);

	/*
	 * At this point, "first" is the location of the lowest object
	 * with an object name that could match "bin_pfx".  See if we have
	 * 0, 1 or more objects that actually match(es).
	 */
	for (i = first; i < num && !ds->ambiguous; i++) {
		struct object_id oid;
		current = nth_midxed_object_oid(&oid, m, i);
		if (!match_sha(ds->len, ds->bin_pfx.hash, current->hash))
			break;
		update_candidates(ds, current);
	}
}

static void unique_in_pack(struct packed_git *p,
			   struct disambiguate_state *ds)
{
	uint32_t num, i, first = 0;

	if (p->multi_pack_index)
		return;

	if (open_pack_index(p) || !p->num_objects)
		return;

	num = p->num_objects;
	bsearch_pack(&ds->bin_pfx, p, &first);

	/*
	 * At this point, "first" is the location of the lowest object
	 * with an object name that could match "bin_pfx".  See if we have
	 * 0, 1 or more objects that actually match(es).
	 */
	for (i = first; i < num && !ds->ambiguous; i++) {
		struct object_id oid;
		nth_packed_object_id(&oid, p, i);
		if (!match_sha(ds->len, ds->bin_pfx.hash, oid.hash))
			break;
		update_candidates(ds, &oid);
	}
}

static void find_short_packed_object(struct disambiguate_state *ds)
{
	struct multi_pack_index *m;
	struct packed_git *p;

	for (m = get_multi_pack_index(ds->repo); m && !ds->ambiguous;
	     m = m->next)
		unique_in_midx(m, ds);
	for (p = get_packed_git(ds->repo); p && !ds->ambiguous;
	     p = p->next)
		unique_in_pack(p, ds);
}

static int finish_object_disambiguation(struct disambiguate_state *ds,
					struct object_id *oid)
{
	if (ds->ambiguous)
		return SHORT_NAME_AMBIGUOUS;

	if (!ds->candidate_exists)
		return MISSING_OBJECT;

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
				    ds->fn(ds->repo, &ds->candidate, ds->cb_data));

	if (!ds->candidate_ok)
		return SHORT_NAME_AMBIGUOUS;

	oidcpy(oid, &ds->candidate);
	return 0;
}

static int disambiguate_commit_only(struct repository *r,
				    const struct object_id *oid,
				    void *cb_data_unused)
{
	int kind = oid_object_info(r, oid, NULL);
	return kind == OBJ_COMMIT;
}

static int disambiguate_committish_only(struct repository *r,
					const struct object_id *oid,
					void *cb_data_unused)
{
	struct object *obj;
	int kind;

	kind = oid_object_info(r, oid, NULL);
	if (kind == OBJ_COMMIT)
		return 1;
	if (kind != OBJ_TAG)
		return 0;

	/* We need to do this the hard way... */
	obj = deref_tag(r, parse_object(r, oid), NULL, 0);
	if (obj && obj->type == OBJ_COMMIT)
		return 1;
	return 0;
}

static int disambiguate_tree_only(struct repository *r,
				  const struct object_id *oid,
				  void *cb_data_unused)
{
	int kind = oid_object_info(r, oid, NULL);
	return kind == OBJ_TREE;
}

static int disambiguate_treeish_only(struct repository *r,
				     const struct object_id *oid,
				     void *cb_data_unused)
{
	struct object *obj;
	int kind;

	kind = oid_object_info(r, oid, NULL);
	if (kind == OBJ_TREE || kind == OBJ_COMMIT)
		return 1;
	if (kind != OBJ_TAG)
		return 0;

	/* We need to do this the hard way... */
	obj = deref_tag(r, parse_object(r, oid), NULL, 0);
	if (obj && (obj->type == OBJ_TREE || obj->type == OBJ_COMMIT))
		return 1;
	return 0;
}

static int disambiguate_blob_only(struct repository *r,
				  const struct object_id *oid,
				  void *cb_data_unused)
{
	int kind = oid_object_info(r, oid, NULL);
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

static int init_object_disambiguation(struct repository *r,
				      const char *name, int len,
				      struct disambiguate_state *ds)
{
	int i;

	if (len < MINIMUM_ABBREV || len > the_hash_algo->hexsz)
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
	ds->repo = r;
	prepare_alt_odb(r);
	return 0;
}

static int show_ambiguous_object(const struct object_id *oid, void *data)
{
	const struct disambiguate_state *ds = data;
	struct strbuf desc = STRBUF_INIT;
	int type;

	if (ds->fn && !ds->fn(ds->repo, oid, ds->cb_data))
		return 0;

	type = oid_object_info(ds->repo, oid, NULL);
	if (type == OBJ_COMMIT) {
		struct commit *commit = lookup_commit(ds->repo, oid);
		if (commit) {
			struct pretty_print_context pp = {0};
			pp.date_mode.type = DATE_SHORT;
			format_commit_message(commit, " %ad - %s", &desc, &pp);
		}
	} else if (type == OBJ_TAG) {
		struct tag *tag = lookup_tag(ds->repo, oid);
		if (!parse_tag(tag) && tag->tag)
			strbuf_addf(&desc, " %s", tag->tag);
	}

	advise("  %s %s%s",
	       repo_find_unique_abbrev(ds->repo, oid, DEFAULT_ABBREV),
	       type_name(type) ? type_name(type) : "unknown type",
	       desc.buf);

	strbuf_release(&desc);
	return 0;
}

static int collect_ambiguous(const struct object_id *oid, void *data)
{
	oid_array_append(data, oid);
	return 0;
}

static int repo_collect_ambiguous(struct repository *r,
				  const struct object_id *oid,
				  void *data)
{
	return collect_ambiguous(oid, data);
}

static int sort_ambiguous(const void *a, const void *b, void *ctx)
{
	struct repository *sort_ambiguous_repo = ctx;
	int a_type = oid_object_info(sort_ambiguous_repo, a, NULL);
	int b_type = oid_object_info(sort_ambiguous_repo, b, NULL);
	int a_type_sort;
	int b_type_sort;

	/*
	 * Sorts by hash within the same object type, just as
	 * oid_array_for_each_unique() would do.
	 */
	if (a_type == b_type)
		return oidcmp(a, b);

	/*
	 * Between object types show tags, then commits, and finally
	 * trees and blobs.
	 *
	 * The object_type enum is commit, tree, blob, tag, but we
	 * want tag, commit, tree blob. Cleverly (perhaps too
	 * cleverly) do that with modulus, since the enum assigns 1 to
	 * commit, so tag becomes 0.
	 */
	a_type_sort = a_type % 4;
	b_type_sort = b_type % 4;
	return a_type_sort > b_type_sort ? 1 : -1;
}

static void sort_ambiguous_oid_array(struct repository *r, struct oid_array *a)
{
	QSORT_S(a->oid, a->nr, sort_ambiguous, r);
}

static enum get_oid_result get_short_oid(struct repository *r,
					 const char *name, int len,
					 struct object_id *oid,
					 unsigned flags)
{
	int status;
	struct disambiguate_state ds;
	int quietly = !!(flags & GET_OID_QUIETLY);

	if (init_object_disambiguation(r, name, len, &ds) < 0)
		return -1;

	if (HAS_MULTI_BITS(flags & GET_OID_DISAMBIGUATORS))
		BUG("multiple get_short_oid disambiguator flags");

	if (flags & GET_OID_COMMIT)
		ds.fn = disambiguate_commit_only;
	else if (flags & GET_OID_COMMITTISH)
		ds.fn = disambiguate_committish_only;
	else if (flags & GET_OID_TREE)
		ds.fn = disambiguate_tree_only;
	else if (flags & GET_OID_TREEISH)
		ds.fn = disambiguate_treeish_only;
	else if (flags & GET_OID_BLOB)
		ds.fn = disambiguate_blob_only;
	else
		ds.fn = default_disambiguate_hint;

	find_short_object_filename(&ds);
	find_short_packed_object(&ds);
	status = finish_object_disambiguation(&ds, oid);

	/*
	 * If we didn't find it, do the usual reprepare() slow-path,
	 * since the object may have recently been added to the repository
	 * or migrated from loose to packed.
	 */
	if (status == MISSING_OBJECT) {
		reprepare_packed_git(r);
		find_short_object_filename(&ds);
		find_short_packed_object(&ds);
		status = finish_object_disambiguation(&ds, oid);
	}

	if (!quietly && (status == SHORT_NAME_AMBIGUOUS)) {
		struct oid_array collect = OID_ARRAY_INIT;

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
		repo_for_each_abbrev(r, ds.hex_pfx, collect_ambiguous, &collect);
		sort_ambiguous_oid_array(r, &collect);

		if (oid_array_for_each(&collect, show_ambiguous_object, &ds))
			BUG("show_ambiguous_object shouldn't return non-zero");
		oid_array_clear(&collect);
	}

	return status;
}

int repo_for_each_abbrev(struct repository *r, const char *prefix,
			 each_abbrev_fn fn, void *cb_data)
{
	struct oid_array collect = OID_ARRAY_INIT;
	struct disambiguate_state ds;
	int ret;

	if (init_object_disambiguation(r, prefix, strlen(prefix), &ds) < 0)
		return -1;

	ds.always_call_fn = 1;
	ds.fn = repo_collect_ambiguous;
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

struct min_abbrev_data {
	unsigned int init_len;
	unsigned int cur_len;
	char *hex;
	struct repository *repo;
	const struct object_id *oid;
};

static inline char get_hex_char_from_oid(const struct object_id *oid,
					 unsigned int pos)
{
	static const char hex[] = "0123456789abcdef";

	if ((pos & 1) == 0)
		return hex[oid->hash[pos >> 1] >> 4];
	else
		return hex[oid->hash[pos >> 1] & 0xf];
}

static int extend_abbrev_len(const struct object_id *oid, void *cb_data)
{
	struct min_abbrev_data *mad = cb_data;

	unsigned int i = mad->init_len;
	while (mad->hex[i] && mad->hex[i] == get_hex_char_from_oid(oid, i))
		i++;

	if (i < GIT_MAX_RAWSZ && i >= mad->cur_len)
		mad->cur_len = i + 1;

	return 0;
}

static int repo_extend_abbrev_len(struct repository *r,
				  const struct object_id *oid,
				  void *cb_data)
{
	return extend_abbrev_len(oid, cb_data);
}

static void find_abbrev_len_for_midx(struct multi_pack_index *m,
				     struct min_abbrev_data *mad)
{
	int match = 0;
	uint32_t num, first = 0;
	struct object_id oid;
	const struct object_id *mad_oid;

	if (!m->num_objects)
		return;

	num = m->num_objects;
	mad_oid = mad->oid;
	match = bsearch_midx(mad_oid, m, &first);

	/*
	 * first is now the position in the packfile where we would insert
	 * mad->hash if it does not exist (or the position of mad->hash if
	 * it does exist). Hence, we consider a maximum of two objects
	 * nearby for the abbreviation length.
	 */
	mad->init_len = 0;
	if (!match) {
		if (nth_midxed_object_oid(&oid, m, first))
			extend_abbrev_len(&oid, mad);
	} else if (first < num - 1) {
		if (nth_midxed_object_oid(&oid, m, first + 1))
			extend_abbrev_len(&oid, mad);
	}
	if (first > 0) {
		if (nth_midxed_object_oid(&oid, m, first - 1))
			extend_abbrev_len(&oid, mad);
	}
	mad->init_len = mad->cur_len;
}

static void find_abbrev_len_for_pack(struct packed_git *p,
				     struct min_abbrev_data *mad)
{
	int match = 0;
	uint32_t num, first = 0;
	struct object_id oid;
	const struct object_id *mad_oid;

	if (p->multi_pack_index)
		return;

	if (open_pack_index(p) || !p->num_objects)
		return;

	num = p->num_objects;
	mad_oid = mad->oid;
	match = bsearch_pack(mad_oid, p, &first);

	/*
	 * first is now the position in the packfile where we would insert
	 * mad->hash if it does not exist (or the position of mad->hash if
	 * it does exist). Hence, we consider a maximum of two objects
	 * nearby for the abbreviation length.
	 */
	mad->init_len = 0;
	if (!match) {
		if (!nth_packed_object_id(&oid, p, first))
			extend_abbrev_len(&oid, mad);
	} else if (first < num - 1) {
		if (!nth_packed_object_id(&oid, p, first + 1))
			extend_abbrev_len(&oid, mad);
	}
	if (first > 0) {
		if (!nth_packed_object_id(&oid, p, first - 1))
			extend_abbrev_len(&oid, mad);
	}
	mad->init_len = mad->cur_len;
}

static void find_abbrev_len_packed(struct min_abbrev_data *mad)
{
	struct multi_pack_index *m;
	struct packed_git *p;

	for (m = get_multi_pack_index(mad->repo); m; m = m->next)
		find_abbrev_len_for_midx(m, mad);
	for (p = get_packed_git(mad->repo); p; p = p->next)
		find_abbrev_len_for_pack(p, mad);
}

int repo_find_unique_abbrev_r(struct repository *r, char *hex,
			      const struct object_id *oid, int len)
{
	struct disambiguate_state ds;
	struct min_abbrev_data mad;
	struct object_id oid_ret;
	const unsigned hexsz = r->hash_algo->hexsz;

	if (len < 0) {
		unsigned long count = repo_approximate_object_count(r);
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
		 * together we need to divide by 2 and round up.
		 */
		len = DIV_ROUND_UP(len, 2);
		/*
		 * For very small repos, we stick with our regular fallback.
		 */
		if (len < FALLBACK_DEFAULT_ABBREV)
			len = FALLBACK_DEFAULT_ABBREV;
	}

	oid_to_hex_r(hex, oid);
	if (len == hexsz || !len)
		return hexsz;

	mad.repo = r;
	mad.init_len = len;
	mad.cur_len = len;
	mad.hex = hex;
	mad.oid = oid;

	find_abbrev_len_packed(&mad);

	if (init_object_disambiguation(r, hex, mad.cur_len, &ds) < 0)
		return -1;

	ds.fn = repo_extend_abbrev_len;
	ds.always_call_fn = 1;
	ds.cb_data = (void *)&mad;

	find_short_object_filename(&ds);
	(void)finish_object_disambiguation(&ds, &oid_ret);

	hex[mad.cur_len] = 0;
	return mad.cur_len;
}

const char *repo_find_unique_abbrev(struct repository *r,
				    const struct object_id *oid,
				    int len)
{
	static int bufno;
	static char hexbuffer[4][GIT_MAX_HEXSZ + 1];
	char *hex = hexbuffer[bufno];
	bufno = (bufno + 1) % ARRAY_SIZE(hexbuffer);
	repo_find_unique_abbrev_r(r, hex, oid, len);
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
		    && !strncasecmp(string, suffix[i], suffix_len))
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

static enum get_oid_result get_oid_1(struct repository *r, const char *name, int len, struct object_id *oid, unsigned lookup_flags);
static int interpret_nth_prior_checkout(struct repository *r, const char *name, int namelen, struct strbuf *buf);

static int get_oid_basic(struct repository *r, const char *str, int len,
			 struct object_id *oid, unsigned int flags)
{
	static const char *warn_msg = "refname '%.*s' is ambiguous.";
	static const char *object_name_msg = N_(
	"Git normally never creates a ref that ends with 40 hex characters\n"
	"because it will be ignored when you just specify 40-hex. These refs\n"
	"may be created by mistake. For example,\n"
	"\n"
	"  git switch -c $br $(git rev-parse ...)\n"
	"\n"
	"where \"$br\" is somehow empty and a 40-hex ref is created. Please\n"
	"examine these refs and maybe delete them. Turn this message off by\n"
	"running \"git config advice.objectNameWarning false\"");
	struct object_id tmp_oid;
	char *real_ref = NULL;
	int refs_found = 0;
	int at, reflog_len, nth_prior = 0;

	if (len == r->hash_algo->hexsz && !get_oid_hex(str, oid)) {
		if (warn_ambiguous_refs && warn_on_object_refname_ambiguity) {
			refs_found = repo_dwim_ref(r, str, len, &tmp_oid, &real_ref);
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

		if (interpret_nth_prior_checkout(r, str, len, &buf) > 0) {
			detached = (buf.len == r->hash_algo->hexsz && !get_oid_hex(buf.buf, oid));
			strbuf_release(&buf);
			if (detached)
				return 0;
		}
	}

	if (!len && reflog_len)
		/* allow "@{...}" to mean the current branch reflog */
		refs_found = repo_dwim_ref(r, "HEAD", 4, oid, &real_ref);
	else if (reflog_len)
		refs_found = repo_dwim_log(r, str, len, oid, &real_ref);
	else
		refs_found = repo_dwim_ref(r, str, len, oid, &real_ref);

	if (!refs_found)
		return -1;

	if (warn_ambiguous_refs && !(flags & GET_OID_QUIETLY) &&
	    (refs_found > 1 ||
	     !get_short_oid(r, str, len, &tmp_oid, GET_OID_QUIETLY)))
		warning(warn_msg, len, str);

	if (reflog_len) {
		int nth, i;
		timestamp_t at_time;
		timestamp_t co_time;
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
		if (read_ref_at(get_main_ref_store(r),
				real_ref, flags, at_time, nth, oid, NULL,
				&co_time, &co_tz, &co_cnt)) {
			if (!len) {
				if (!skip_prefix(real_ref, "refs/heads/", &str))
					str = "HEAD";
				len = strlen(str);
			}
			if (at_time) {
				if (!(flags & GET_OID_QUIETLY)) {
					warning(_("log for '%.*s' only goes back to %s"),
						len, str,
						show_date(co_time, co_tz, DATE_MODE(RFC2822)));
				}
			} else {
				if (flags & GET_OID_QUIETLY) {
					exit(128);
				}
				die(_("log for '%.*s' only has %d entries"),
				    len, str, co_cnt);
			}
		}
	}

	free(real_ref);
	return 0;
}

static enum get_oid_result get_parent(struct repository *r,
				      const char *name, int len,
				      struct object_id *result, int idx)
{
	struct object_id oid;
	enum get_oid_result ret = get_oid_1(r, name, len, &oid,
					    GET_OID_COMMITTISH);
	struct commit *commit;
	struct commit_list *p;

	if (ret)
		return ret;
	commit = lookup_commit_reference(r, &oid);
	if (parse_commit(commit))
		return MISSING_OBJECT;
	if (!idx) {
		oidcpy(result, &commit->object.oid);
		return FOUND;
	}
	p = commit->parents;
	while (p) {
		if (!--idx) {
			oidcpy(result, &p->item->object.oid);
			return FOUND;
		}
		p = p->next;
	}
	return MISSING_OBJECT;
}

static enum get_oid_result get_nth_ancestor(struct repository *r,
					    const char *name, int len,
					    struct object_id *result,
					    int generation)
{
	struct object_id oid;
	struct commit *commit;
	int ret;

	ret = get_oid_1(r, name, len, &oid, GET_OID_COMMITTISH);
	if (ret)
		return ret;
	commit = lookup_commit_reference(r, &oid);
	if (!commit)
		return MISSING_OBJECT;

	while (generation--) {
		if (parse_commit(commit) || !commit->parents)
			return MISSING_OBJECT;
		commit = commit->parents->item;
	}
	oidcpy(result, &commit->object.oid);
	return FOUND;
}

struct object *repo_peel_to_type(struct repository *r, const char *name, int namelen,
				 struct object *o, enum object_type expected_type)
{
	if (name && !namelen)
		namelen = strlen(name);
	while (1) {
		if (!o || (!o->parsed && !parse_object(r, &o->oid)))
			return NULL;
		if (expected_type == OBJ_ANY || o->type == expected_type)
			return o;
		if (o->type == OBJ_TAG)
			o = ((struct tag*) o)->tagged;
		else if (o->type == OBJ_COMMIT)
			o = &(repo_get_commit_tree(r, ((struct commit *)o))->object);
		else {
			if (name)
				error("%.*s: expected %s type, but the object "
				      "dereferences to %s type",
				      namelen, name, type_name(expected_type),
				      type_name(o->type));
			return NULL;
		}
	}
}

static int peel_onion(struct repository *r, const char *name, int len,
		      struct object_id *oid, unsigned lookup_flags)
{
	struct object_id outer;
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

	lookup_flags &= ~GET_OID_DISAMBIGUATORS;
	if (expected_type == OBJ_COMMIT)
		lookup_flags |= GET_OID_COMMITTISH;
	else if (expected_type == OBJ_TREE)
		lookup_flags |= GET_OID_TREEISH;

	if (get_oid_1(r, name, sp - name - 2, &outer, lookup_flags))
		return -1;

	o = parse_object(r, &outer);
	if (!o)
		return -1;
	if (!expected_type) {
		o = deref_tag(r, o, name, sp - name - 2);
		if (!o || (!o->parsed && !parse_object(r, &o->oid)))
			return -1;
		oidcpy(oid, &o->oid);
		return 0;
	}

	/*
	 * At this point, the syntax look correct, so
	 * if we do not get the needed object, we should
	 * barf.
	 */
	o = repo_peel_to_type(r, name, len, o, expected_type);
	if (!o)
		return -1;

	oidcpy(oid, &o->oid);
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
		ret = get_oid_oneline(r, prefix, oid, list);
		free(prefix);
		return ret;
	}
	return 0;
}

static int get_describe_name(struct repository *r,
			     const char *name, int len,
			     struct object_id *oid)
{
	const char *cp;
	unsigned flags = GET_OID_QUIETLY | GET_OID_COMMIT;

	for (cp = name + len - 1; name + 2 <= cp; cp--) {
		char ch = *cp;
		if (!isxdigit(ch)) {
			/* We must be looking at g in "SOMETHING-g"
			 * for it to be describe output.
			 */
			if (ch == 'g' && cp[-1] == '-') {
				cp++;
				len -= cp - name;
				return get_short_oid(r,
						     cp, len, oid, flags);
			}
		}
	}
	return -1;
}

static enum get_oid_result get_oid_1(struct repository *r,
				     const char *name, int len,
				     struct object_id *oid,
				     unsigned lookup_flags)
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
		unsigned int num = 0;
		int len1 = cp - name;
		cp++;
		while (cp < name + len) {
			unsigned int digit = *cp++ - '0';
			if (unsigned_mult_overflows(num, 10))
				return MISSING_OBJECT;
			num *= 10;
			if (unsigned_add_overflows(num, digit))
				return MISSING_OBJECT;
			num += digit;
		}
		if (!num && len1 == len - 1)
			num = 1;
		else if (num > INT_MAX)
			return MISSING_OBJECT;
		if (has_suffix == '^')
			return get_parent(r, name, len1, oid, num);
		/* else if (has_suffix == '~') -- goes without saying */
		return get_nth_ancestor(r, name, len1, oid, num);
	}

	ret = peel_onion(r, name, len, oid, lookup_flags);
	if (!ret)
		return FOUND;

	ret = get_oid_basic(r, name, len, oid, lookup_flags);
	if (!ret)
		return FOUND;

	/* It could be describe output that is "SOMETHING-gXXXX" */
	ret = get_describe_name(r, name, len, oid);
	if (!ret)
		return FOUND;

	return get_short_oid(r, name, len, oid, lookup_flags);
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

struct handle_one_ref_cb {
	struct repository *repo;
	struct commit_list **list;
};

static int handle_one_ref(const char *path, const struct object_id *oid,
			  int flag, void *cb_data)
{
	struct handle_one_ref_cb *cb = cb_data;
	struct commit_list **list = cb->list;
	struct object *object = parse_object(cb->repo, oid);
	if (!object)
		return 0;
	if (object->type == OBJ_TAG) {
		object = deref_tag(cb->repo, object, path,
				   strlen(path));
		if (!object)
			return 0;
	}
	if (object->type != OBJ_COMMIT)
		return 0;
	commit_list_insert((struct commit *)object, list);
	return 0;
}

static int get_oid_oneline(struct repository *r,
			   const char *prefix, struct object_id *oid,
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
		if (!parse_object(r, &commit->object.oid))
			continue;
		buf = get_commit_buffer(commit, NULL);
		p = strstr(buf, "\n\n");
		matches = negative ^ (p && !regexec(&regex, p + 2, 0, NULL, 0));
		unuse_commit_buffer(commit, buf);

		if (matches) {
			oidcpy(oid, &commit->object.oid);
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
	struct strbuf *sb;
};

static int grab_nth_branch_switch(struct object_id *ooid, struct object_id *noid,
				  const char *email, timestamp_t timestamp, int tz,
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
		strbuf_reset(cb->sb);
		strbuf_add(cb->sb, match, len);
		return 1; /* we are done */
	}
	return 0;
}

/*
 * Parse @{-N} syntax, return the number of characters parsed
 * if successful; otherwise signal an error with negative value.
 */
static int interpret_nth_prior_checkout(struct repository *r,
					const char *name, int namelen,
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
	cb.sb = buf;

	retval = refs_for_each_reflog_ent_reverse(get_main_ref_store(r),
			"HEAD", grab_nth_branch_switch, &cb);
	if (0 < retval) {
		retval = brace - name + 1;
	} else
		retval = 0;

	return retval;
}

int repo_get_oid_mb(struct repository *r,
		    const char *name,
		    struct object_id *oid)
{
	struct commit *one, *two;
	struct commit_list *mbs;
	struct object_id oid_tmp;
	const char *dots;
	int st;

	dots = strstr(name, "...");
	if (!dots)
		return repo_get_oid(r, name, oid);
	if (dots == name)
		st = repo_get_oid(r, "HEAD", &oid_tmp);
	else {
		struct strbuf sb;
		strbuf_init(&sb, dots - name);
		strbuf_add(&sb, name, dots - name);
		st = repo_get_oid_committish(r, sb.buf, &oid_tmp);
		strbuf_release(&sb);
	}
	if (st)
		return st;
	one = lookup_commit_reference_gently(r, &oid_tmp, 0);
	if (!one)
		return -1;

	if (repo_get_oid_committish(r, dots[3] ? (dots + 3) : "HEAD", &oid_tmp))
		return -1;
	two = lookup_commit_reference_gently(r, &oid_tmp, 0);
	if (!two)
		return -1;
	mbs = repo_get_merge_bases(r, one, two);
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

static int reinterpret(struct repository *r,
		       const char *name, int namelen, int len,
		       struct strbuf *buf, unsigned allowed)
{
	/* we have extra data, which might need further processing */
	struct strbuf tmp = STRBUF_INIT;
	int used = buf->len;
	int ret;

	strbuf_add(buf, name + len, namelen - len);
	ret = repo_interpret_branch_name(r, buf->buf, buf->len, &tmp, allowed);
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

static void set_shortened_ref(struct repository *r, struct strbuf *buf, const char *ref)
{
	char *s = refs_shorten_unambiguous_ref(get_main_ref_store(r), ref, 0);
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

static int interpret_branch_mark(struct repository *r,
				 const char *name, int namelen,
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

	set_shortened_ref(r, buf, value);
	return len + at;
}

int repo_interpret_branch_name(struct repository *r,
			       const char *name, int namelen,
			       struct strbuf *buf,
			       unsigned allowed)
{
	char *at;
	const char *start;
	int len;

	if (!namelen)
		namelen = strlen(name);

	if (!allowed || (allowed & INTERPRET_BRANCH_LOCAL)) {
		len = interpret_nth_prior_checkout(r, name, namelen, buf);
		if (!len) {
			return len; /* syntax Ok, not enough switches */
		} else if (len > 0) {
			if (len == namelen)
				return len; /* consumed all */
			else
				return reinterpret(r, name, namelen, len, buf, allowed);
		}
	}

	for (start = name;
	     (at = memchr(start, '@', namelen - (start - name)));
	     start = at + 1) {

		if (!allowed || (allowed & INTERPRET_BRANCH_HEAD)) {
			len = interpret_empty_at(name, namelen, at - name, buf);
			if (len > 0)
				return reinterpret(r, name, namelen, len, buf,
						   allowed);
		}

		len = interpret_branch_mark(r, name, namelen, at - name, buf,
					    upstream_mark, branch_get_upstream,
					    allowed);
		if (len > 0)
			return len;

		len = interpret_branch_mark(r, name, namelen, at - name, buf,
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
	if (startup_info->have_repository)
		strbuf_branchname(sb, name, INTERPRET_BRANCH_LOCAL);
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

/*
 * This is like "get_oid_basic()", except it allows "object ID expressions",
 * notably "xyz^" for "parent of xyz"
 */
int repo_get_oid(struct repository *r, const char *name, struct object_id *oid)
{
	struct object_context unused;
	return get_oid_with_context(r, name, 0, oid, &unused);
}

/*
 * This returns a non-zero value if the string (built using printf
 * format and the given arguments) is not a valid object.
 */
int get_oidf(struct object_id *oid, const char *fmt, ...)
{
	va_list ap;
	int ret;
	struct strbuf sb = STRBUF_INIT;

	va_start(ap, fmt);
	strbuf_vaddf(&sb, fmt, ap);
	va_end(ap);

	ret = get_oid(sb.buf, oid);
	strbuf_release(&sb);

	return ret;
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
int repo_get_oid_committish(struct repository *r,
			    const char *name,
			    struct object_id *oid)
{
	struct object_context unused;
	return get_oid_with_context(r, name, GET_OID_COMMITTISH,
				    oid, &unused);
}

int repo_get_oid_treeish(struct repository *r,
			 const char *name,
			 struct object_id *oid)
{
	struct object_context unused;
	return get_oid_with_context(r, name, GET_OID_TREEISH,
				    oid, &unused);
}

int repo_get_oid_commit(struct repository *r,
			const char *name,
			struct object_id *oid)
{
	struct object_context unused;
	return get_oid_with_context(r, name, GET_OID_COMMIT,
				    oid, &unused);
}

int repo_get_oid_tree(struct repository *r,
		      const char *name,
		      struct object_id *oid)
{
	struct object_context unused;
	return get_oid_with_context(r, name, GET_OID_TREE,
				    oid, &unused);
}

int repo_get_oid_blob(struct repository *r,
		      const char *name,
		      struct object_id *oid)
{
	struct object_context unused;
	return get_oid_with_context(r, name, GET_OID_BLOB,
				    oid, &unused);
}

/* Must be called only when object_name:filename doesn't exist. */
static void diagnose_invalid_oid_path(struct repository *r,
				      const char *prefix,
				      const char *filename,
				      const struct object_id *tree_oid,
				      const char *object_name,
				      int object_name_len)
{
	struct object_id oid;
	unsigned short mode;

	if (!prefix)
		prefix = "";

	if (file_exists(filename))
		die(_("path '%s' exists on disk, but not in '%.*s'"),
		    filename, object_name_len, object_name);
	if (is_missing_file_error(errno)) {
		char *fullname = xstrfmt("%s%s", prefix, filename);

		if (!get_tree_entry(r, tree_oid, fullname, &oid, &mode)) {
			die(_("path '%s' exists, but not '%s'\n"
			    "hint: Did you mean '%.*s:%s' aka '%.*s:./%s'?"),
			    fullname,
			    filename,
			    object_name_len, object_name,
			    fullname,
			    object_name_len, object_name,
			    filename);
		}
		die(_("path '%s' does not exist in '%.*s'"),
		    filename, object_name_len, object_name);
	}
}

/* Must be called only when :stage:filename doesn't exist. */
static void diagnose_invalid_index_path(struct repository *r,
					int stage,
					const char *prefix,
					const char *filename)
{
	struct index_state *istate = r->index;
	const struct cache_entry *ce;
	int pos;
	unsigned namelen = strlen(filename);
	struct strbuf fullname = STRBUF_INIT;

	if (!prefix)
		prefix = "";

	/* Wrong stage number? */
	pos = index_name_pos(istate, filename, namelen);
	if (pos < 0)
		pos = -pos - 1;
	if (pos < istate->cache_nr) {
		ce = istate->cache[pos];
		if (ce_namelen(ce) == namelen &&
		    !memcmp(ce->name, filename, namelen))
			die(_("path '%s' is in the index, but not at stage %d\n"
			    "hint: Did you mean ':%d:%s'?"),
			    filename, stage,
			    ce_stage(ce), filename);
	}

	/* Confusion between relative and absolute filenames? */
	strbuf_addstr(&fullname, prefix);
	strbuf_addstr(&fullname, filename);
	pos = index_name_pos(istate, fullname.buf, fullname.len);
	if (pos < 0)
		pos = -pos - 1;
	if (pos < istate->cache_nr) {
		ce = istate->cache[pos];
		if (ce_namelen(ce) == fullname.len &&
		    !memcmp(ce->name, fullname.buf, fullname.len))
			die(_("path '%s' is in the index, but not '%s'\n"
			    "hint: Did you mean ':%d:%s' aka ':%d:./%s'?"),
			    fullname.buf, filename,
			    ce_stage(ce), fullname.buf,
			    ce_stage(ce), filename);
	}

	if (repo_file_exists(r, filename))
		die(_("path '%s' exists on disk, but not in the index"), filename);
	if (is_missing_file_error(errno))
		die(_("path '%s' does not exist (neither on disk nor in the index)"),
		    filename);

	strbuf_release(&fullname);
}


static char *resolve_relative_path(struct repository *r, const char *rel)
{
	if (!starts_with(rel, "./") && !starts_with(rel, "../"))
		return NULL;

	if (r != the_repository || !is_inside_work_tree())
		die(_("relative path syntax can't be used outside working tree"));

	/* die() inside prefix_path() if resolved path is outside worktree */
	return prefix_path(startup_info->prefix,
			   startup_info->prefix ? strlen(startup_info->prefix) : 0,
			   rel);
}

static enum get_oid_result get_oid_with_context_1(struct repository *repo,
				  const char *name,
				  unsigned flags,
				  const char *prefix,
				  struct object_id *oid,
				  struct object_context *oc)
{
	int ret, bracket_depth;
	int namelen = strlen(name);
	const char *cp;
	int only_to_die = flags & GET_OID_ONLY_TO_DIE;

	if (only_to_die)
		flags |= GET_OID_QUIETLY;

	memset(oc, 0, sizeof(*oc));
	oc->mode = S_IFINVALID;
	strbuf_init(&oc->symlink_path, 0);
	ret = get_oid_1(repo, name, namelen, oid, flags);
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
			struct handle_one_ref_cb cb;
			struct commit_list *list = NULL;

			cb.repo = repo;
			cb.list = &list;
			refs_for_each_ref(repo->refs, handle_one_ref, &cb);
			refs_head_ref(repo->refs, handle_one_ref, &cb);
			commit_list_sort_by_date(&list);
			return get_oid_oneline(repo, name + 2, oid, list);
		}
		if (namelen < 3 ||
		    name[2] != ':' ||
		    name[1] < '0' || '3' < name[1])
			cp = name + 1;
		else {
			stage = name[1] - '0';
			cp = name + 3;
		}
		new_path = resolve_relative_path(repo, cp);
		if (!new_path) {
			namelen = namelen - (cp - name);
		} else {
			cp = new_path;
			namelen = strlen(cp);
		}

		if (flags & GET_OID_RECORD_PATH)
			oc->path = xstrdup(cp);

		if (!repo->index || !repo->index->cache)
			repo_read_index(repo);
		pos = index_name_pos(repo->index, cp, namelen);
		if (pos < 0)
			pos = -pos - 1;
		while (pos < repo->index->cache_nr) {
			ce = repo->index->cache[pos];
			if (ce_namelen(ce) != namelen ||
			    memcmp(ce->name, cp, namelen))
				break;
			if (ce_stage(ce) == stage) {
				oidcpy(oid, &ce->oid);
				oc->mode = ce->ce_mode;
				free(new_path);
				return 0;
			}
			pos++;
		}
		if (only_to_die && name[1] && name[1] != '/')
			diagnose_invalid_index_path(repo, stage, prefix, cp);
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
		struct object_id tree_oid;
		int len = cp - name;
		unsigned sub_flags = flags;

		sub_flags &= ~GET_OID_DISAMBIGUATORS;
		sub_flags |= GET_OID_TREEISH;

		if (!get_oid_1(repo, name, len, &tree_oid, sub_flags)) {
			const char *filename = cp+1;
			char *new_filename = NULL;

			new_filename = resolve_relative_path(repo, filename);
			if (new_filename)
				filename = new_filename;
			if (flags & GET_OID_FOLLOW_SYMLINKS) {
				ret = get_tree_entry_follow_symlinks(repo, &tree_oid,
					filename, oid, &oc->symlink_path,
					&oc->mode);
			} else {
				ret = get_tree_entry(repo, &tree_oid, filename, oid,
						     &oc->mode);
				if (ret && only_to_die) {
					diagnose_invalid_oid_path(repo, prefix,
								   filename,
								   &tree_oid,
								   name, len);
				}
			}
			if (flags & GET_OID_RECORD_PATH)
				oc->path = xstrdup(filename);

			free(new_filename);
			return ret;
		} else {
			if (only_to_die)
				die(_("invalid object name '%.*s'."), len, name);
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
void maybe_die_on_misspelt_object_name(struct repository *r,
				       const char *name,
				       const char *prefix)
{
	struct object_context oc;
	struct object_id oid;
	get_oid_with_context_1(r, name, GET_OID_ONLY_TO_DIE,
			       prefix, &oid, &oc);
}

enum get_oid_result get_oid_with_context(struct repository *repo,
					 const char *str,
					 unsigned flags,
					 struct object_id *oid,
					 struct object_context *oc)
{
	if (flags & GET_OID_FOLLOW_SYMLINKS && flags & GET_OID_ONLY_TO_DIE)
		BUG("incompatible flags for get_sha1_with_context");
	return get_oid_with_context_1(repo, str, flags, NULL, oid, oc);
}
