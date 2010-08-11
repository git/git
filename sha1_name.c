#include "cache.h"
#include "tag.h"
#include "commit.h"
#include "tree.h"
#include "blob.h"
#include "tree-walk.h"
#include "refs.h"
#include "remote.h"

static int find_short_object_filename(int len, const char *name, unsigned char *sha1)
{
	struct alternate_object_database *alt;
	char hex[40];
	int found = 0;
	static struct alternate_object_database *fakeent;

	if (!fakeent) {
		const char *objdir = get_object_directory();
		int objdir_len = strlen(objdir);
		int entlen = objdir_len + 43;
		fakeent = xmalloc(sizeof(*fakeent) + entlen);
		memcpy(fakeent->base, objdir, objdir_len);
		fakeent->name = fakeent->base + objdir_len + 1;
		fakeent->name[-1] = '/';
	}
	fakeent->next = alt_odb_list;

	sprintf(hex, "%.2s", name);
	for (alt = fakeent; alt && found < 2; alt = alt->next) {
		struct dirent *de;
		DIR *dir;
		sprintf(alt->name, "%.2s/", name);
		dir = opendir(alt->base);
		if (!dir)
			continue;
		while ((de = readdir(dir)) != NULL) {
			if (strlen(de->d_name) != 38)
				continue;
			if (memcmp(de->d_name, name + 2, len - 2))
				continue;
			if (!found) {
				memcpy(hex + 2, de->d_name, 38);
				found++;
			}
			else if (memcmp(hex + 2, de->d_name, 38)) {
				found = 2;
				break;
			}
		}
		closedir(dir);
	}
	if (found == 1)
		return get_sha1_hex(hex, sha1) == 0;
	return found;
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

static int find_short_packed_object(int len, const unsigned char *match, unsigned char *sha1)
{
	struct packed_git *p;
	const unsigned char *found_sha1 = NULL;
	int found = 0;

	prepare_packed_git();
	for (p = packed_git; p && found < 2; p = p->next) {
		uint32_t num, last;
		uint32_t first = 0;
		open_pack_index(p);
		num = p->num_objects;
		last = num;
		while (first < last) {
			uint32_t mid = (first + last) / 2;
			const unsigned char *now;
			int cmp;

			now = nth_packed_object_sha1(p, mid);
			cmp = hashcmp(match, now);
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
		if (first < num) {
			const unsigned char *now, *next;
		       now = nth_packed_object_sha1(p, first);
			if (match_sha(len, match, now)) {
				next = nth_packed_object_sha1(p, first+1);
			       if (!next|| !match_sha(len, match, next)) {
					/* unique within this pack */
					if (!found) {
						found_sha1 = now;
						found++;
					}
					else if (hashcmp(found_sha1, now)) {
						found = 2;
						break;
					}
				}
				else {
					/* not even unique within this pack */
					found = 2;
					break;
				}
			}
		}
	}
	if (found == 1)
		hashcpy(sha1, found_sha1);
	return found;
}

#define SHORT_NAME_NOT_FOUND (-1)
#define SHORT_NAME_AMBIGUOUS (-2)

static int find_unique_short_object(int len, char *canonical,
				    unsigned char *res, unsigned char *sha1)
{
	int has_unpacked, has_packed;
	unsigned char unpacked_sha1[20], packed_sha1[20];

	prepare_alt_odb();
	has_unpacked = find_short_object_filename(len, canonical, unpacked_sha1);
	has_packed = find_short_packed_object(len, res, packed_sha1);
	if (!has_unpacked && !has_packed)
		return SHORT_NAME_NOT_FOUND;
	if (1 < has_unpacked || 1 < has_packed)
		return SHORT_NAME_AMBIGUOUS;
	if (has_unpacked != has_packed) {
		hashcpy(sha1, (has_packed ? packed_sha1 : unpacked_sha1));
		return 0;
	}
	/* Both have unique ones -- do they match? */
	if (hashcmp(packed_sha1, unpacked_sha1))
		return SHORT_NAME_AMBIGUOUS;
	hashcpy(sha1, packed_sha1);
	return 0;
}

static int get_short_sha1(const char *name, int len, unsigned char *sha1,
			  int quietly)
{
	int i, status;
	char canonical[40];
	unsigned char res[20];

	if (len < MINIMUM_ABBREV || len > 40)
		return -1;
	hashclr(res);
	memset(canonical, 'x', 40);
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
		canonical[i] = c;
		if (!(i & 1))
			val <<= 4;
		res[i >> 1] |= val;
	}

	status = find_unique_short_object(i, canonical, res, sha1);
	if (!quietly && (status == SHORT_NAME_AMBIGUOUS))
		return error("short SHA1 %.*s is ambiguous.", len, canonical);
	return status;
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
		status = get_short_sha1(hex, len, sha1_ret, 1);
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

/*
 * *string and *len will only be substituted, and *string returned (for
 * later free()ing) if the string passed in is a magic short-hand form
 * to name a branch.
 */
static char *substitute_branch_name(const char **string, int *len)
{
	struct strbuf buf = STRBUF_INIT;
	int ret = interpret_branch_name(*string, &buf);

	if (ret == *len) {
		size_t size;
		*string = strbuf_detach(&buf, &size);
		*len = size;
		return (char *)*string;
	}

	return NULL;
}

int dwim_ref(const char *str, int len, unsigned char *sha1, char **ref)
{
	char *last_branch = substitute_branch_name(&str, &len);
	const char **p, *r;
	int refs_found = 0;

	*ref = NULL;
	for (p = ref_rev_parse_rules; *p; p++) {
		char fullref[PATH_MAX];
		unsigned char sha1_from_ref[20];
		unsigned char *this_result;
		int flag;

		this_result = refs_found ? sha1_from_ref : sha1;
		mksnpath(fullref, sizeof(fullref), *p, len, str);
		r = resolve_ref(fullref, this_result, 1, &flag);
		if (r) {
			if (!refs_found++)
				*ref = xstrdup(r);
			if (!warn_ambiguous_refs)
				break;
		} else if ((flag & REF_ISSYMREF) && strcmp(fullref, "HEAD"))
			warning("ignoring dangling symref %s.", fullref);
	}
	free(last_branch);
	return refs_found;
}

int dwim_log(const char *str, int len, unsigned char *sha1, char **log)
{
	char *last_branch = substitute_branch_name(&str, &len);
	const char **p;
	int logs_found = 0;

	*log = NULL;
	for (p = ref_rev_parse_rules; *p; p++) {
		struct stat st;
		unsigned char hash[20];
		char path[PATH_MAX];
		const char *ref, *it;

		mksnpath(path, sizeof(path), *p, len, str);
		ref = resolve_ref(path, hash, 1, NULL);
		if (!ref)
			continue;
		if (!stat(git_path("logs/%s", path), &st) &&
		    S_ISREG(st.st_mode))
			it = path;
		else if (strcmp(ref, path) &&
			 !stat(git_path("logs/%s", ref), &st) &&
			 S_ISREG(st.st_mode))
			it = ref;
		else
			continue;
		if (!logs_found++) {
			*log = xstrdup(it);
			hashcpy(sha1, hash);
		}
		if (!warn_ambiguous_refs)
			break;
	}
	free(last_branch);
	return logs_found;
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

static int get_sha1_1(const char *name, int len, unsigned char *sha1);

static int get_sha1_basic(const char *str, int len, unsigned char *sha1)
{
	static const char *warning = "warning: refname '%.*s' is ambiguous.\n";
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
			return get_sha1_1(buf.buf, buf.len, sha1);
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
		fprintf(stderr, warning, len, str);

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
				fprintf(stderr,
					"warning: Log for '%.*s' only goes "
					"back to %s.\n", len, str,
					show_date(co_time, co_tz, DATE_RFC2822));
			else
				fprintf(stderr,
					"warning: Log for '%.*s' only has "
					"%d entries.\n", len, str, co_cnt);
		}
	}

	free(real_ref);
	return 0;
}

static int get_parent(const char *name, int len,
		      unsigned char *result, int idx)
{
	unsigned char sha1[20];
	int ret = get_sha1_1(name, len, sha1);
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

	ret = get_sha1_1(name, len, sha1);
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
	if (!o) {
		unsigned char sha1[20];
		if (get_sha1_1(name, namelen, sha1))
			return NULL;
		o = parse_object(sha1);
	}
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
	else
		return -1;

	if (get_sha1_1(name, sp - name - 2, outer))
		return -1;

	o = parse_object(outer);
	if (!o)
		return -1;
	if (!expected_type) {
		o = deref_tag(o, name, sp - name - 2);
		if (!o || (!o->parsed && !parse_object(o->sha1)))
			return -1;
		hashcpy(sha1, o->sha1);
	}
	else {
		/*
		 * At this point, the syntax look correct, so
		 * if we do not get the needed object, we should
		 * barf.
		 */
		o = peel_to_type(name, len, o, expected_type);
		if (o) {
			hashcpy(sha1, o->sha1);
			return 0;
		}
		return -1;
	}
	return 0;
}

static int get_describe_name(const char *name, int len, unsigned char *sha1)
{
	const char *cp;

	for (cp = name + len - 1; name + 2 <= cp; cp--) {
		char ch = *cp;
		if (hexval(ch) & ~0377) {
			/* We must be looking at g in "SOMETHING-g"
			 * for it to be describe output.
			 */
			if (ch == 'g' && cp[-1] == '-') {
				cp++;
				len -= cp - name;
				return get_short_sha1(cp, len, sha1, 1);
			}
		}
	}
	return -1;
}

static int get_sha1_1(const char *name, int len, unsigned char *sha1)
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

	return get_short_sha1(name, len, sha1, 0);
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
	insert_by_date((struct commit *)object, list);
	object->flags |= ONELINE_SEEN;
	return 0;
}

static int get_sha1_oneline(const char *prefix, unsigned char *sha1)
{
	struct commit_list *list = NULL, *backup = NULL, *l;
	int retval = -1;
	char *temp_commit_buffer = NULL;
	regex_t regex;

	if (prefix[0] == '!') {
		if (prefix[1] != '!')
			die ("Invalid search pattern: %s", prefix);
		prefix++;
	}

	if (regcomp(&regex, prefix, REG_EXTENDED))
		die("Invalid search pattern: %s", prefix);

	for_each_ref(handle_one_ref, &list);
	for (l = list; l; l = l->next)
		commit_list_insert(l->item, &backup);
	while (list) {
		char *p;
		struct commit *commit;
		enum object_type type;
		unsigned long size;

		commit = pop_most_recent_commit(&list, ONELINE_SEEN);
		if (!parse_object(commit->object.sha1))
			continue;
		free(temp_commit_buffer);
		if (commit->buffer)
			p = commit->buffer;
		else {
			p = read_sha1_file(commit->object.sha1, &type, &size);
			if (!p)
				continue;
			temp_commit_buffer = p;
		}
		if (!(p = strstr(p, "\n\n")))
			continue;
		if (!regexec(&regex, p + 2, 0, NULL, 0)) {
			hashcpy(sha1, commit->object.sha1);
			retval = 0;
			break;
		}
	}
	regfree(&regex);
	free(temp_commit_buffer);
	free_commit_list(list);
	for (l = backup; l; l = l->next)
		clear_commit_marks(l->item, ONELINE_SEEN);
	return retval;
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
		st = get_sha1(sb.buf, sha1_tmp);
		strbuf_release(&sb);
	}
	if (st)
		return st;
	one = lookup_commit_reference_gently(sha1_tmp, 0);
	if (!one)
		return -1;

	if (get_sha1(dots[3] ? (dots + 3) : "HEAD", sha1_tmp))
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
	if (!upstream
	    || !upstream->merge
	    || !upstream->merge[0]->dst)
		return error("No upstream branch found for '%s'", cp);
	free(cp);
	cp = shorten_unambiguous_ref(upstream->merge[0]->dst, 0);
	strbuf_reset(buf);
	strbuf_addstr(buf, cp);
	free(cp);
	return len;
}

/*
 * This is like "get_sha1_basic()", except it allows "sha1 expressions",
 * notably "xyz^" for "parent of xyz"
 */
int get_sha1(const char *name, unsigned char *sha1)
{
	struct object_context unused;
	return get_sha1_with_context(name, sha1, &unused);
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
			    "Did you mean '%s:%s'?",
			    fullname,
			    filename,
			    object_name,
			    fullname);
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
			    "Did you mean ':%d:%s'?",
			    fullname, filename,
			    ce_stage(ce), fullname);
	}

	if (!lstat(filename, &st))
		die("Path '%s' exists on disk, but not in the index.", filename);
	if (errno == ENOENT || errno == ENOTDIR)
		die("Path '%s' does not exist (neither on disk nor in the index).",
		    filename);

	free(fullname);
}


int get_sha1_with_mode_1(const char *name, unsigned char *sha1, unsigned *mode, int gently, const char *prefix)
{
	struct object_context oc;
	int ret;
	ret = get_sha1_with_context_1(name, sha1, &oc, gently, prefix);
	*mode = oc.mode;
	return ret;
}

int get_sha1_with_context_1(const char *name, unsigned char *sha1,
			    struct object_context *oc,
			    int gently, const char *prefix)
{
	int ret, bracket_depth;
	int namelen = strlen(name);
	const char *cp;

	memset(oc, 0, sizeof(*oc));
	oc->mode = S_IFINVALID;
	ret = get_sha1_1(name, namelen, sha1);
	if (!ret)
		return ret;
	/* sha1:path --> object name of path in ent sha1
	 * :path -> object name of path in index
	 * :[0-3]:path -> object name of path in index at stage
	 */
	if (name[0] == ':') {
		int stage = 0;
		struct cache_entry *ce;
		int pos;
		if (namelen > 2 && name[1] == '/')
			return get_sha1_oneline(name + 2, sha1);
		if (namelen < 3 ||
		    name[2] != ':' ||
		    name[1] < '0' || '3' < name[1])
			cp = name + 1;
		else {
			stage = name[1] - '0';
			cp = name + 3;
		}
		namelen = namelen - (cp - name);

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
				return 0;
			}
			pos++;
		}
		if (!gently)
			diagnose_invalid_index_path(stage, prefix, cp);
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
		if (!gently) {
			object_name = xmalloc(cp-name+1);
			strncpy(object_name, name, cp-name);
			object_name[cp-name] = '\0';
		}
		if (!get_sha1_1(name, cp-name, tree_sha1)) {
			const char *filename = cp+1;
			ret = get_tree_entry(tree_sha1, filename, sha1, &oc->mode);
			if (!gently) {
				diagnose_invalid_sha1_path(prefix, filename,
							   tree_sha1, object_name);
				free(object_name);
			}
			hashcpy(oc->tree, tree_sha1);
			strncpy(oc->path, filename,
				sizeof(oc->path));
			oc->path[sizeof(oc->path)-1] = '\0';

			return ret;
		} else {
			if (!gently)
				die("Invalid object name '%s'.", object_name);
		}
	}
	return ret;
}
