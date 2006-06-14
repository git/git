#include "cache.h"
#include "tag.h"
#include "commit.h"
#include "tree.h"
#include "blob.h"
#include "tree-walk.h"
#include "refs.h"

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
	unsigned char found_sha1[20];
	int found = 0;

	prepare_packed_git();
	for (p = packed_git; p && found < 2; p = p->next) {
		unsigned num = num_packed_objects(p);
		unsigned first = 0, last = num;
		while (first < last) {
			unsigned mid = (first + last) / 2;
			unsigned char now[20];
			int cmp;

			nth_packed_object_sha1(p, mid, now);
			cmp = memcmp(match, now, 20);
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
			unsigned char now[20], next[20];
			nth_packed_object_sha1(p, first, now);
			if (match_sha(len, match, now)) {
				if (nth_packed_object_sha1(p, first+1, next) ||
				    !match_sha(len, match, next)) {
					/* unique within this pack */
					if (!found) {
						memcpy(found_sha1, now, 20);
						found++;
					}
					else if (memcmp(found_sha1, now, 20)) {
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
		memcpy(sha1, found_sha1, 20);
	return found;
}

#define SHORT_NAME_NOT_FOUND (-1)
#define SHORT_NAME_AMBIGUOUS (-2)

static int find_unique_short_object(int len, char *canonical,
				    unsigned char *res, unsigned char *sha1)
{
	int has_unpacked, has_packed;
	unsigned char unpacked_sha1[20], packed_sha1[20];

	has_unpacked = find_short_object_filename(len, canonical, unpacked_sha1);
	has_packed = find_short_packed_object(len, res, packed_sha1);
	if (!has_unpacked && !has_packed)
		return SHORT_NAME_NOT_FOUND;
	if (1 < has_unpacked || 1 < has_packed)
		return SHORT_NAME_AMBIGUOUS;
	if (has_unpacked != has_packed) {
		memcpy(sha1, (has_packed ? packed_sha1 : unpacked_sha1), 20);
		return 0;
	}
	/* Both have unique ones -- do they match? */
	if (memcmp(packed_sha1, unpacked_sha1, 20))
		return SHORT_NAME_AMBIGUOUS;
	memcpy(sha1, packed_sha1, 20);
	return 0;
}

static int get_short_sha1(const char *name, int len, unsigned char *sha1,
			  int quietly)
{
	int i, status;
	char canonical[40];
	unsigned char res[20];

	if (len < MINIMUM_ABBREV)
		return -1;
	memset(res, 0, 20);
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
	int status, is_null;
	static char hex[41];

	is_null = !memcmp(sha1, null_sha1, 20);
	memcpy(hex, sha1_to_hex(sha1), 40);
	if (len == 40)
		return hex;
	while (len < 40) {
		unsigned char sha1_ret[20];
		status = get_short_sha1(hex, len, sha1_ret, 1);
		if (!status ||
		    (is_null && status != SHORT_NAME_AMBIGUOUS)) {
			hex[len] = 0;
			return hex;
		}
		if (status != SHORT_NAME_AMBIGUOUS)
			return NULL;
		len++;
	}
	return NULL;
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

static int get_sha1_basic(const char *str, int len, unsigned char *sha1)
{
	static const char *fmt[] = {
		"%.*s",
		"refs/%.*s",
		"refs/tags/%.*s",
		"refs/heads/%.*s",
		"refs/remotes/%.*s",
		"refs/remotes/%.*s/HEAD",
		NULL
	};
	static const char *warning = "warning: refname '%.*s' is ambiguous.\n";
	const char **p, *pathname;
	char *real_path = NULL;
	int refs_found = 0, am;
	unsigned long at_time = (unsigned long)-1;
	unsigned char *this_result;
	unsigned char sha1_from_ref[20];

	if (len == 40 && !get_sha1_hex(str, sha1))
		return 0;

	/* At a given period of time? "@{2 hours ago}" */
	for (am = 1; am < len - 1; am++) {
		if (str[am] == '@' && str[am+1] == '{' && str[len-1] == '}') {
			int date_len = len - am - 3;
			char *date_spec = xmalloc(date_len + 1);
			safe_strncpy(date_spec, str + am + 2, date_len + 1);
			at_time = approxidate(date_spec);
			free(date_spec);
			len = am;
			break;
		}
	}

	/* Accept only unambiguous ref paths. */
	if (ambiguous_path(str, len))
		return -1;

	for (p = fmt; *p; p++) {
		this_result = refs_found ? sha1_from_ref : sha1;
		pathname = resolve_ref(git_path(*p, len, str), this_result, 1);
		if (pathname) {
			if (!refs_found++)
				real_path = strdup(pathname);
			if (!warn_ambiguous_refs)
				break;
		}
	}

	if (!refs_found)
		return -1;

	if (warn_ambiguous_refs && refs_found > 1)
		fprintf(stderr, warning, len, str);

	if (at_time != (unsigned long)-1) {
		read_ref_at(
			real_path + strlen(git_path(".")) - 1,
			at_time,
			sha1);
	}

	free(real_path);
	return 0;
}

static int get_sha1_1(const char *name, int len, unsigned char *sha1);

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
		memcpy(result, commit->object.sha1, 20);
		return 0;
	}
	p = commit->parents;
	while (p) {
		if (!--idx) {
			memcpy(result, p->item->object.sha1, 20);
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
	int ret = get_sha1_1(name, len, sha1);
	if (ret)
		return ret;

	while (generation--) {
		struct commit *commit = lookup_commit_reference(sha1);

		if (!commit || parse_commit(commit) || !commit->parents)
			return -1;
		memcpy(sha1, commit->parents->item->object.sha1, 20);
	}
	memcpy(result, sha1, 20);
	return 0;
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
		expected_type = TYPE_COMMIT;
	else if (!strncmp(tree_type, sp, 4) && sp[4] == '}')
		expected_type = TYPE_TREE;
	else if (!strncmp(blob_type, sp, 4) && sp[4] == '}')
		expected_type = TYPE_BLOB;
	else if (sp[0] == '}')
		expected_type = TYPE_NONE;
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
		memcpy(sha1, o->sha1, 20);
	}
	else {
		/* At this point, the syntax look correct, so
		 * if we do not get the needed object, we should
		 * barf.
		 */

		while (1) {
			if (!o || (!o->parsed && !parse_object(o->sha1)))
				return -1;
			if (o->type == expected_type) {
				memcpy(sha1, o->sha1, 20);
				return 0;
			}
			if (o->type == TYPE_TAG)
				o = ((struct tag*) o)->tagged;
			else if (o->type == TYPE_COMMIT)
				o = &(((struct commit *) o)->tree->object);
			else
				return error("%.*s: expected %s type, but the object dereferences to %s type",
					     len, name, typename(expected_type),
					     typename(o->type));
			if (!o->parsed)
				parse_object(o->sha1);
		}
	}
	return 0;
}

static int get_sha1_1(const char *name, int len, unsigned char *sha1)
{
	int ret, has_suffix;
	const char *cp;

	/* "name~3" is "name^^^",
	 * "name~" and "name~0" are name -- not "name^0"!
	 * "name^" is not "name^0"; it is "name^1".
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
		if (has_suffix == '^') {
			if (!num && len1 == len - 1)
				num = 1;
			return get_parent(name, len1, sha1, num);
		}
		/* else if (has_suffix == '~') -- goes without saying */
		return get_nth_ancestor(name, len1, sha1, num);
	}

	ret = peel_onion(name, len, sha1);
	if (!ret)
		return 0;

	ret = get_sha1_basic(name, len, sha1);
	if (!ret)
		return 0;
	return get_short_sha1(name, len, sha1, 0);
}

/*
 * This is like "get_sha1_basic()", except it allows "sha1 expressions",
 * notably "xyz^" for "parent of xyz"
 */
int get_sha1(const char *name, unsigned char *sha1)
{
	int ret, bracket_depth;
	unsigned unused;
	int namelen = strlen(name);
	const char *cp;

	prepare_alt_odb();
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
		if (namelen < 3 ||
		    name[2] != ':' ||
		    name[1] < '0' || '3' < name[1])
			cp = name + 1;
		else {
			stage = name[1] - '0';
			cp = name + 3;
		}
		namelen = namelen - (cp - name);
		if (!active_cache)
			read_cache();
		if (active_nr < 0)
			return -1;
		pos = cache_name_pos(cp, namelen);
		if (pos < 0)
			pos = -pos - 1;
		while (pos < active_nr) {
			ce = active_cache[pos];
			if (ce_namelen(ce) != namelen ||
			    memcmp(ce->name, cp, namelen))
				break;
			if (ce_stage(ce) == stage) {
				memcpy(sha1, ce->sha1, 20);
				return 0;
			}
			pos++;
		}
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
		if (!get_sha1_1(name, cp-name, tree_sha1))
			return get_tree_entry(tree_sha1, cp+1, sha1,
					      &unused);
	}
	return ret;
}
