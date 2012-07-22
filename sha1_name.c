#include "cache.h"
#include "tag.h"
#include "commit.h"
#include "tree.h"
#include "blob.h"
#include "tree-walk.h"
#include "refs.h"
#include "remote.h"

static int get_sha1_oneline(const char *, unsigned char *, struct commit_list *);

typedef int (*disambiguate_hint_fn)(const unsigned char *, void *);

struct disambiguate_state {
	disambiguate_hint_fn fn;
	void *cb_data;
	unsigned char candidate[20];
	unsigned candidate_exists:1;
	unsigned candidate_checked:1;
	unsigned candidate_ok:1;
	unsigned disambiguate_fn_used:1;
	unsigned ambiguous:1;
	unsigned always_call_fn:1;
};

static void update_candidates(struct disambiguate_state *ds, const unsigned char *current)
{
	if (ds->always_call_fn) {
		ds->ambiguous = ds->fn(current, ds->cb_data) ? 1 : 0;
		return;
	}
	if (!ds->candidate_exists) {
		/* this is the first candidate */
		hashcpy(ds->candidate, current);
		ds->candidate_exists = 1;
		return;
	} else if (!hashcmp(ds->candidate, current)) {
		/* the same as what we already have seen */
		return;
	}

	if (!ds->fn) {
		/* cannot disambiguate between ds->candidate and current */
		ds->ambiguous = 1;
		return;
	}

	if (!ds->candidate_checked) {
		ds->candidate_ok = ds->fn(ds->candidate, ds->cb_data);
		ds->disambiguate_fn_used = 1;
		ds->candidate_checked = 1;
	}

	if (!ds->candidate_ok) {
		/* discard the candidate; we know it does not satisify fn */
		hashcpy(ds->candidate, current);
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

static void find_short_object_filename(int len, const char *hex_pfx, struct disambiguate_state *ds)
{
	struct alternate_object_database *alt;
	char hex[40];
	static struct alternate_object_database *fakeent;

	if (!fakeent) {
		/*
		 * Create a "fake" alternate object database that
		 * points to our own object database, to make it
		 * easier to get a temporary working space in
		 * alt->name/alt->base while iterating over the
		 * object databases including our own.
		 */
		const char *objdir = get_object_directory();
		int objdir_len = strlen(objdir);
		int entlen = objdir_len + 43;
		fakeent = xmalloc(sizeof(*fakeent) + entlen);
		memcpy(fakeent->base, objdir, objdir_len);
		fakeent->name = fakeent->base + objdir_len + 1;
		fakeent->name[-1] = '/';
	}
	fakeent->next = alt_odb_list;

	sprintf(hex, "%.2s", hex_pfx);
	for (alt = fakeent; alt && !ds->ambiguous; alt = alt->next) {
		struct dirent *de;
		DIR *dir;
		sprintf(alt->name, "%.2s/", hex_pfx);
		dir = opendir(alt->base);
		if (!dir)
			continue;

		while (!ds->ambiguous && (de = readdir(dir)) != NULL) {
			unsigned char sha1[20];

			if (strlen(de->d_name) != 38)
				continue;
			if (memcmp(de->d_name, hex_pfx + 2, len - 2))
				continue;
			memcpy(hex + 2, de->d_name, 38);
			if (!get_sha1_hex(hex, sha1))
				update_candidates(ds, sha1);
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

static void unique_in_pack(int len,
			  const unsigned char *bin_pfx,
			   struct packed_git *p,
			   struct disambiguate_state *ds)
{
	uint32_t num, last, i, first = 0;
	const unsigned char *current = NULL;

	open_pack_index(p);
	num = p->num_objects;
	last = num;
	while (first < last) {
		uint32_t mid = (first + last) / 2;
		const unsigned char *current;
		int cmp;

		current = nth_packed_object_sha1(p, mid);
		cmp = hashcmp(bin_pfx, current);
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
		current = nth_packed_object_sha1(p, i);
		if (!match_sha(len, bin_pfx, current))
			break;
		update_candidates(ds, current);
	}
}

static void find_short_packed_object(int len, const unsigned char *bin_pfx,
				     struct disambiguate_state *ds)
{
	struct packed_git *p;

	prepare_packed_git();
	for (p = packed_git; p && !ds->ambiguous; p = p->next)
		unique_in_pack(len, bin_pfx, p, ds);
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
				    ds->fn(ds->candidate, ds->cb_data));

	if (!ds->candidate_ok)
		return SHORT_NAME_AMBIGUOUS;

	hashcpy(sha1, ds->candidate);
	return 0;
}

static int disambiguate_commit_only(const unsigned char *sha1, void *cb_data_unused)
{
	int kind = sha1_object_info(sha1, NULL);
	return kind == OBJ_COMMIT;
}

static int disambiguate_committish_only(const unsigned char *sha1, void *cb_data_unused)
{
	struct object *obj;
	int kind;

	kind = sha1_object_info(sha1, NULL);
	if (kind == OBJ_COMMIT)
		return 1;
	if (kind != OBJ_TAG)
		return 0;

	/* We need to do this the hard way... */
	obj = deref_tag(lookup_object(sha1), NULL, 0);
	if (obj && obj->type == OBJ_COMMIT)
		return 1;
	return 0;
}

static int disambiguate_tree_only(const unsigned char *sha1, void *cb_data_unused)
{
	int kind = sha1_object_info(sha1, NULL);
	return kind == OBJ_TREE;
}

static int disambiguate_treeish_only(const unsigned char *sha1, void *cb_data_unused)
{
	struct object *obj;
	int kind;

	kind = sha1_object_info(sha1, NULL);
	if (kind == OBJ_TREE || kind == OBJ_COMMIT)
		return 1;
	if (kind != OBJ_TAG)
		return 0;

	/* We need to do this the hard way... */
	obj = deref_tag(lookup_object(sha1), NULL, 0);
	if (obj && (obj->type == OBJ_TREE || obj->type == OBJ_COMMIT))
		return 1;
	return 0;
}

static int disambiguate_blob_only(const unsigned char *sha1, void *cb_data_unused)
{
	int kind = sha1_object_info(sha1, NULL);
	return kind == OBJ_BLOB;
}

static int prepare_prefixes(const char *name, int len,
			    unsigned char *bin_pfx,
			    char *hex_pfx)
{
	int i;

	hashclr(bin_pfx);
	memset(hex_pfx, 'x', 40);
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
		hex_pfx[i] = c;
		if (!(i & 1))
			val <<= 4;
		bin_pfx[i >> 1] |= val;
	}
	return 0;
}

static int get_short_sha1(const char *name, int len, unsigned char *sha1,
			  unsigned flags)
{
	int status;
	char hex_pfx[40];
	unsigned char bin_pfx[20];
	struct disambiguate_state ds;
	int quietly = !!(flags & GET_SHA1_QUIETLY);

	if (len < MINIMUM_ABBREV || len > 40)
		return -1;
	if (prepare_prefixes(name, len, bin_pfx, hex_pfx) < 0)
		return -1;

	prepare_alt_odb();

	memset(&ds, 0, sizeof(ds));
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

	find_short_object_filename(len, hex_pfx, &ds);
	find_short_packed_object(len, bin_pfx, &ds);
	status = finish_object_disambiguation(&ds, sha1);

	if (!quietly && (status == SHORT_NAME_AMBIGUOUS))
		return error("short SHA1 %.*s is ambiguous.", len, hex_pfx);
	return status;
}


int for_each_abbrev(const char *prefix, each_abbrev_fn fn, void *cb_data)
{
	char hex_pfx[40];
	unsigned char bin_pfx[20];
	struct disambiguate_state ds;
	int len = strlen(prefix);

	if (len < MINIMUM_ABBREV || len > 40)
		return -1;
	if (prepare_prefixes(prefix, len, bin_pfx, hex_pfx) < 0)
		return -1;

	prepare_alt_odb();

	memset(&ds, 0, sizeof(ds));
	ds.always_call_fn = 1;
	ds.cb_data = cb_data;
	ds.fn = fn;

	find_short_object_filename(len, hex_pfx, &ds);
	find_short_packed_object(len, bin_pfx, &ds);
	return ds.ambiguous;
}

const char *find_unique_abbrev(const unsigned char *sha1, int len)
{
	int status, exists;
	static char hex[41];

	exists = has_sha1_file(sha1);
	memcpy(hex, sha1_to_hex(sha1), 40);
	if (len == 40 || !len)
		return hex;
	while (len < 40) {
		unsigned char sha1_ret[20];
		status = get_short_sha1(hex, len, sha1_ret, GET_SHA1_QUIETLY);
		if (exists
		    ? !status
		    : status == SHORT_NAME_NOT_FOUND) {
			hex[len] = 0;
			return hex;
		}
		len++;
	}
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

static inline int upstream_mark(const char *string, int len)
{
	const char *suffix[] = { "@{upstream}", "@{u}" };
	int i;

	for (i = 0; i < ARRAY_SIZE(suffix); i++) {
		int suffix_len = strlen(suffix[i]);
		if (suffix_len <= len
		    && !memcmp(string, suffix[i], suffix_len))
			return suffix_len;
	}
	return 0;
}

static int get_sha1_1(const char *name, int len, unsigned char *sha1, unsigned lookup_flags);

static int get_sha1_basic(const char *str, int len, unsigned char *sha1)
{
	static const char *warn_msg = "refname '%.*s' is ambiguous.";
	char *real_ref = NULL;
	int refs_found = 0;
	int at, reflog_len;

	if (len == 40 && !get_sha1_hex(str, sha1))
		return 0;

	/* basic@{time or number or -number} format to query ref-log */
	reflog_len = at = 0;
	if (len && str[len-1] == '}') {
		for (at = len-2; at >= 0; at--) {
			if (str[at] == '@' && str[at+1] == '{') {
				if (!upstream_mark(str + at, len - at)) {
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

	if (!len && reflog_len) {
		struct strbuf buf = STRBUF_INIT;
		int ret;
		/* try the @{-N} syntax for n-th checkout */
		ret = interpret_branch_name(str+at, &buf);
		if (ret > 0) {
			/* substitute this branch name and restart */
			return get_sha1_1(buf.buf, buf.len, sha1, 0);
		} else if (ret == 0) {
			return -1;
		}
		/* allow "@{...}" to mean the current branch reflog */
		refs_found = dwim_ref("HEAD", 4, sha1, &real_ref);
	} else if (reflog_len)
		refs_found = dwim_log(str, len, sha1, &real_ref);
	else
		refs_found = dwim_ref(str, len, sha1, &real_ref);

	if (!refs_found)
		return -1;

	if (warn_ambiguous_refs && refs_found > 1)
		warning(warn_msg, len, str);

	if (reflog_len) {
		int nth, i;
		unsigned long at_time;
		unsigned long co_time;
		int co_tz, co_cnt;

		/* a @{-N} placed anywhere except the start is an error */
		if (str[at+2] == '-')
			return -1;

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
			if (errors)
				return -1;
		}
		if (read_ref_at(real_ref, at_time, nth, sha1, NULL,
				&co_time, &co_tz, &co_cnt)) {
			if (at_time)
				warning("Log for '%.*s' only goes "
					"back to %s.", len, str,
					show_date(co_time, co_tz, DATE_RFC2822));
			else {
				free(real_ref);
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
	if (!commit)
		return -1;
	if (parse_commit(commit))
		return -1;
	if (!idx) {
		hashcpy(result, commit->object.sha1);
		return 0;
	}
	p = commit->parents;
	while (p) {
		if (!--idx) {
			hashcpy(result, p->item->object.sha1);
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
	hashcpy(result, commit->object.sha1);
	return 0;
}

struct object *peel_to_type(const char *name, int namelen,
			    struct object *o, enum object_type expected_type)
{
	if (name && !namelen)
		namelen = strlen(name);
	while (1) {
		if (!o || (!o->parsed && !parse_object(o->sha1)))
			return NULL;
		if (o->type == expected_type)
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

static int peel_onion(const char *name, int len, unsigned char *sha1)
{
	unsigned char outer[20];
	const char *sp;
	unsigned int expected_type = 0;
	unsigned lookup_flags = 0;
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
	if (!strncmp(commit_type, sp, 6) && sp[6] == '}')
		expected_type = OBJ_COMMIT;
	else if (!strncmp(tree_type, sp, 4) && sp[4] == '}')
		expected_type = OBJ_TREE;
	else if (!strncmp(blob_type, sp, 4) && sp[4] == '}')
		expected_type = OBJ_BLOB;
	else if (sp[0] == '}')
		expected_type = OBJ_NONE;
	else if (sp[0] == '/')
		expected_type = OBJ_COMMIT;
	else
		return -1;

	if (expected_type == OBJ_COMMIT)
		lookup_flags = GET_SHA1_COMMITTISH;

	if (get_sha1_1(name, sp - name - 2, outer, lookup_flags))
		return -1;

	o = parse_object(outer);
	if (!o)
		return -1;
	if (!expected_type) {
		o = deref_tag(o, name, sp - name - 2);
		if (!o || (!o->parsed && !parse_object(o->sha1)))
			return -1;
		hashcpy(sha1, o->sha1);
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

	hashcpy(sha1, o->sha1);
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
		if (hexval(ch) & ~0377) {
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

	ret = peel_onion(name, len, sha1);
	if (!ret)
		return 0;

	ret = get_sha1_basic(name, len, sha1);
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
 * For future extension, ':/!' is reserved. If you want to match a message
 * beginning with a '!', you have to repeat the exclamation mark.
 */
#define ONELINE_SEEN (1u<<20)

static int handle_one_ref(const char *path,
		const unsigned char *sha1, int flag, void *cb_data)
{
	struct commit_list **list = cb_data;
	struct object *object = parse_object(sha1);
	if (!object)
		return 0;
	if (object->type == OBJ_TAG) {
		object = deref_tag(object, path, strlen(path));
		if (!object)
			return 0;
	}
	if (object->type != OBJ_COMMIT)
		return 0;
	commit_list_insert_by_date((struct commit *)object, list);
	return 0;
}

static int get_sha1_oneline(const char *prefix, unsigned char *sha1,
			    struct commit_list *list)
{
	struct commit_list *backup = NULL, *l;
	int found = 0;
	regex_t regex;

	if (prefix[0] == '!') {
		if (prefix[1] != '!')
			die ("Invalid search pattern: %s", prefix);
		prefix++;
	}

	if (regcomp(&regex, prefix, REG_EXTENDED))
		die("Invalid search pattern: %s", prefix);

	for (l = list; l; l = l->next) {
		l->item->object.flags |= ONELINE_SEEN;
		commit_list_insert(l->item, &backup);
	}
	while (list) {
		char *p, *to_free = NULL;
		struct commit *commit;
		enum object_type type;
		unsigned long size;
		int matches;

		commit = pop_most_recent_commit(&list, ONELINE_SEEN);
		if (!parse_object(commit->object.sha1))
			continue;
		if (commit->buffer)
			p = commit->buffer;
		else {
			p = read_sha1_file(commit->object.sha1, &type, &size);
			if (!p)
				continue;
			to_free = p;
		}

		p = strstr(p, "\n\n");
		matches = p && !regexec(&regex, p + 2, 0, NULL, 0);
		free(to_free);

		if (matches) {
			hashcpy(sha1, commit->object.sha1);
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
	long cnt, alloc;
	struct strbuf *buf;
};

static int grab_nth_branch_switch(unsigned char *osha1, unsigned char *nsha1,
				  const char *email, unsigned long timestamp, int tz,
				  const char *message, void *cb_data)
{
	struct grab_nth_branch_switch_cbdata *cb = cb_data;
	const char *match = NULL, *target = NULL;
	size_t len;
	int nth;

	if (!prefixcmp(message, "checkout: moving from ")) {
		match = message + strlen("checkout: moving from ");
		target = strstr(match, " to ");
	}

	if (!match || !target)
		return 0;

	len = target - match;
	nth = cb->cnt++ % cb->alloc;
	strbuf_reset(&cb->buf[nth]);
	strbuf_add(&cb->buf[nth], match, len);
	return 0;
}

/*
 * Parse @{-N} syntax, return the number of characters parsed
 * if successful; otherwise signal an error with negative value.
 */
static int interpret_nth_prior_checkout(const char *name, struct strbuf *buf)
{
	long nth;
	int i, retval;
	struct grab_nth_branch_switch_cbdata cb;
	const char *brace;
	char *num_end;

	if (name[0] != '@' || name[1] != '{' || name[2] != '-')
		return -1;
	brace = strchr(name, '}');
	if (!brace)
		return -1;
	nth = strtol(name+3, &num_end, 10);
	if (num_end != brace)
		return -1;
	if (nth <= 0)
		return -1;
	cb.alloc = nth;
	cb.buf = xmalloc(nth * sizeof(struct strbuf));
	for (i = 0; i < nth; i++)
		strbuf_init(&cb.buf[i], 20);
	cb.cnt = 0;
	retval = 0;
	for_each_recent_reflog_ent("HEAD", grab_nth_branch_switch, 40960, &cb);
	if (cb.cnt < nth) {
		cb.cnt = 0;
		for_each_reflog_ent("HEAD", grab_nth_branch_switch, &cb);
	}
	if (cb.cnt < nth)
		goto release_return;
	i = cb.cnt % nth;
	strbuf_reset(buf);
	strbuf_add(buf, cb.buf[i].buf, cb.buf[i].len);
	retval = brace-name+1;

release_return:
	for (i = 0; i < nth; i++)
		strbuf_release(&cb.buf[i]);
	free(cb.buf);

	return retval;
}

int get_sha1_mb(const char *name, unsigned char *sha1)
{
	struct commit *one, *two;
	struct commit_list *mbs;
	unsigned char sha1_tmp[20];
	const char *dots;
	int st;

	dots = strstr(name, "...");
	if (!dots)
		return get_sha1(name, sha1);
	if (dots == name)
		st = get_sha1("HEAD", sha1_tmp);
	else {
		struct strbuf sb;
		strbuf_init(&sb, dots - name);
		strbuf_add(&sb, name, dots - name);
		st = get_sha1_committish(sb.buf, sha1_tmp);
		strbuf_release(&sb);
	}
	if (st)
		return st;
	one = lookup_commit_reference_gently(sha1_tmp, 0);
	if (!one)
		return -1;

	if (get_sha1_committish(dots[3] ? (dots + 3) : "HEAD", sha1_tmp))
		return -1;
	two = lookup_commit_reference_gently(sha1_tmp, 0);
	if (!two)
		return -1;
	mbs = get_merge_bases(one, two, 1);
	if (!mbs || mbs->next)
		st = -1;
	else {
		st = 0;
		hashcpy(sha1, mbs->item->object.sha1);
	}
	free_commit_list(mbs);
	return st;
}

/*
 * This reads short-hand syntax that not only evaluates to a commit
 * object name, but also can act as if the end user spelled the name
 * of the branch from the command line.
 *
 * - "@{-N}" finds the name of the Nth previous branch we were on, and
 *   places the name of the branch in the given buf and returns the
 *   number of characters parsed if successful.
 *
 * - "<branch>@{upstream}" finds the name of the other ref that
 *   <branch> is configured to merge with (missing <branch> defaults
 *   to the current branch), and places the name of the branch in the
 *   given buf and returns the number of characters parsed if
 *   successful.
 *
 * If the input is not of the accepted format, it returns a negative
 * number to signal an error.
 *
 * If the input was ok but there are not N branch switches in the
 * reflog, it returns 0.
 */
int interpret_branch_name(const char *name, struct strbuf *buf)
{
	char *cp;
	struct branch *upstream;
	int namelen = strlen(name);
	int len = interpret_nth_prior_checkout(name, buf);
	int tmp_len;

	if (!len)
		return len; /* syntax Ok, not enough switches */
	if (0 < len && len == namelen)
		return len; /* consumed all */
	else if (0 < len) {
		/* we have extra data, which might need further processing */
		struct strbuf tmp = STRBUF_INIT;
		int used = buf->len;
		int ret;

		strbuf_add(buf, name + len, namelen - len);
		ret = interpret_branch_name(buf->buf, &tmp);
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

	cp = strchr(name, '@');
	if (!cp)
		return -1;
	tmp_len = upstream_mark(cp, namelen - (cp - name));
	if (!tmp_len)
		return -1;
	len = cp + tmp_len - name;
	cp = xstrndup(name, cp - name);
	upstream = branch_get(*cp ? cp : NULL);
	/*
	 * Upstream can be NULL only if cp refers to HEAD and HEAD
	 * points to something different than a branch.
	 */
	if (!upstream)
		return error(_("HEAD does not point to a branch"));
	if (!upstream->merge || !upstream->merge[0]->dst) {
		if (!ref_exists(upstream->refname))
			return error(_("No such branch: '%s'"), cp);
		if (!upstream->merge)
			return error(_("No upstream configured for branch '%s'"),
				     upstream->name);
		return error(
			_("Upstream branch '%s' not stored as a remote-tracking branch"),
			upstream->merge[0]->src);
	}
	free(cp);
	cp = shorten_unambiguous_ref(upstream->merge[0]->dst, 0);
	strbuf_reset(buf);
	strbuf_addstr(buf, cp);
	free(cp);
	return len;
}

int strbuf_branchname(struct strbuf *sb, const char *name)
{
	int len = strlen(name);
	if (interpret_branch_name(name, sb) == len)
		return 0;
	strbuf_add(sb, name, len);
	return len;
}

int strbuf_check_branch_ref(struct strbuf *sb, const char *name)
{
	strbuf_branchname(sb, name);
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
 * Many callers know that the user meant to name a committish by
 * syntactical positions where the object name appears.  Calling this
 * function allows the machinery to disambiguate shorter-than-unique
 * abbreviated object names between committish and others.
 *
 * Note that this does NOT error out when the named object is not a
 * committish. It is merely to give a hint to the disambiguation
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
				       const char *object_name)
{
	struct stat st;
	unsigned char sha1[20];
	unsigned mode;

	if (!prefix)
		prefix = "";

	if (!lstat(filename, &st))
		die("Path '%s' exists on disk, but not in '%s'.",
		    filename, object_name);
	if (errno == ENOENT || errno == ENOTDIR) {
		char *fullname = xmalloc(strlen(filename)
					     + strlen(prefix) + 1);
		strcpy(fullname, prefix);
		strcat(fullname, filename);

		if (!get_tree_entry(tree_sha1, fullname,
				    sha1, &mode)) {
			die("Path '%s' exists, but not '%s'.\n"
			    "Did you mean '%s:%s' aka '%s:./%s'?",
			    fullname,
			    filename,
			    object_name,
			    fullname,
			    object_name,
			    filename);
		}
		die("Path '%s' does not exist in '%s'",
		    filename, object_name);
	}
}

/* Must be called only when :stage:filename doesn't exist. */
static void diagnose_invalid_index_path(int stage,
					const char *prefix,
					const char *filename)
{
	struct stat st;
	struct cache_entry *ce;
	int pos;
	unsigned namelen = strlen(filename);
	unsigned fullnamelen;
	char *fullname;

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
	fullnamelen = namelen + strlen(prefix);
	fullname = xmalloc(fullnamelen + 1);
	strcpy(fullname, prefix);
	strcat(fullname, filename);
	pos = cache_name_pos(fullname, fullnamelen);
	if (pos < 0)
		pos = -pos - 1;
	if (pos < active_nr) {
		ce = active_cache[pos];
		if (ce_namelen(ce) == fullnamelen &&
		    !memcmp(ce->name, fullname, fullnamelen))
			die("Path '%s' is in the index, but not '%s'.\n"
			    "Did you mean ':%d:%s' aka ':%d:./%s'?",
			    fullname, filename,
			    ce_stage(ce), fullname,
			    ce_stage(ce), filename);
	}

	if (!lstat(filename, &st))
		die("Path '%s' exists on disk, but not in the index.", filename);
	if (errno == ENOENT || errno == ENOTDIR)
		die("Path '%s' does not exist (neither on disk nor in the index).",
		    filename);

	free(fullname);
}


static char *resolve_relative_path(const char *rel)
{
	if (prefixcmp(rel, "./") && prefixcmp(rel, "../"))
		return NULL;

	if (!startup_info)
		die("BUG: startup_info struct is not initialized.");

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
		struct cache_entry *ce;
		char *new_path = NULL;
		int pos;
		if (!only_to_die && namelen > 2 && name[1] == '/') {
			struct commit_list *list = NULL;
			for_each_ref(handle_one_ref, &list);
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

		strncpy(oc->path, cp,
			sizeof(oc->path));
		oc->path[sizeof(oc->path)-1] = '\0';

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
				hashcpy(sha1, ce->sha1);
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
		char *object_name = NULL;
		if (only_to_die) {
			object_name = xmalloc(cp-name+1);
			strncpy(object_name, name, cp-name);
			object_name[cp-name] = '\0';
		}
		if (!get_sha1_1(name, cp-name, tree_sha1, GET_SHA1_TREEISH)) {
			const char *filename = cp+1;
			char *new_filename = NULL;

			new_filename = resolve_relative_path(filename);
			if (new_filename)
				filename = new_filename;
			ret = get_tree_entry(tree_sha1, filename, sha1, &oc->mode);
			if (ret && only_to_die) {
				diagnose_invalid_sha1_path(prefix, filename,
							   tree_sha1, object_name);
				free(object_name);
			}
			hashcpy(oc->tree, tree_sha1);
			strncpy(oc->path, filename,
				sizeof(oc->path));
			oc->path[sizeof(oc->path)-1] = '\0';

			free(new_filename);
			return ret;
		} else {
			if (only_to_die)
				die("Invalid object name '%s'.", object_name);
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
	return get_sha1_with_context_1(str, flags, NULL, sha1, orc);
}
