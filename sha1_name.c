#include "cache.h"
#include "commit.h"

static int find_short_object_filename(int len, const char *name, unsigned char *sha1)
{
	static char dirname[PATH_MAX];
	char hex[40];
	DIR *dir;
	int found;

	snprintf(dirname, sizeof(dirname), "%s/%.2s", get_object_directory(), name);
	dir = opendir(dirname);
	sprintf(hex, "%.2s", name);
	found = 0;
	if (dir) {
		struct dirent *de;
		while ((de = readdir(dir)) != NULL) {
			if (strlen(de->d_name) != 38)
				continue;
			if (memcmp(de->d_name, name + 2, len-2))
				continue;
			memcpy(hex + 2, de->d_name, 38);
			if (++found > 1)
				break;
		}
		closedir(dir);
	}
	if (found == 1)
		return get_sha1_hex(hex, sha1) == 0;
	return 0;
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

	prepare_packed_git();
	for (p = packed_git; p; p = p->next) {
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
				if (nth_packed_object_sha1(p, first+1, next) || !match_sha(len, match, next)) {
					memcpy(sha1, now, 20);
					return 1;
				}
			}
		}
	}
	return 0;
}

static int get_short_sha1(const char *name, unsigned char *sha1)
{
	int i;
	char canonical[40];
	unsigned char res[20];

	memset(res, 0, 20);
	memset(canonical, 'x', 40);
	for (i = 0;;i++) {
		unsigned char c = name[i];
		unsigned char val;
		if (!c || i > 40)
			break;
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
	if (i < 4)
		return -1;
	if (find_short_object_filename(i, canonical, sha1))
		return 0;
	if (find_short_packed_object(i, res, sha1))
		return 0;
	return -1;
}

static int get_sha1_file(const char *path, unsigned char *result)
{
	char buffer[60];
	int fd = open(path, O_RDONLY);
	int len;

	if (fd < 0)
		return -1;
	len = read(fd, buffer, sizeof(buffer));
	close(fd);
	if (len < 40)
		return -1;
	return get_sha1_hex(buffer, result);
}

static int get_sha1_basic(const char *str, int len, unsigned char *sha1)
{
	static const char *prefix[] = {
		"",
		"refs",
		"refs/tags",
		"refs/heads",
		"refs/snap",
		NULL
	};
	const char **p;

	if (!get_sha1_hex(str, sha1))
		return 0;

	for (p = prefix; *p; p++) {
		char *pathname = git_path("%s/%.*s", *p, len, str);
		if (!get_sha1_file(pathname, sha1))
			return 0;
	}

	return -1;
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

static int get_sha1_1(const char *name, int len, unsigned char *sha1)
{
	int parent, ret;

	/* foo^[0-9] or foo^ (== foo^1); we do not do more than 9 parents. */
	if (len > 2 && name[len-2] == '^' &&
	    name[len-1] >= '0' && name[len-1] <= '9') {
		parent = name[len-1] - '0';
		len -= 2;
	}
	else if (len > 1 && name[len-1] == '^')
		parent = 1;
	else
		parent = -1;

	if (0 <= parent) {
		ret = get_parent(name, len-1, sha1, parent);
		if (!ret)
			return 0;
	}
	ret = get_sha1_basic(name, len, sha1);
	if (!ret)
		return 0;
	return get_short_sha1(name, sha1);
}

/*
 * This is like "get_sha1_basic()", except it allows "sha1 expressions",
 * notably "xyz^" for "parent of xyz"
 */
int get_sha1(const char *name, unsigned char *sha1)
{
	return get_sha1_1(name, strlen(name), sha1);
}
