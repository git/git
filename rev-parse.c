/*
 * rev-parse.c
 *
 * Copyright (C) Linus Torvalds, 2005
 */
#include "cache.h"
#include "commit.h"
#include "refs.h"

static char *def = NULL;
static int no_revs = 0;
static int single_rev = 0;
static int revs_only = 0;
static int do_rev_argument = 1;
static int output_revs = 0;
static int flags_only = 0;
static int no_flags = 0;

#define NORMAL 0
#define REVERSED 1
static int show_type = NORMAL;

static int get_extended_sha1(char *name, unsigned char *sha1);

/*
 * Some arguments are relevant "revision" arguments,
 * others are about output format or other details.
 * This sorts it all out.
 */
static int is_rev_argument(const char *arg)
{
	static const char *rev_args[] = {
		"--max-count=",
		"--max-age=",
		"--min-age=",
		"--merge-order",
		NULL
	};
	const char **p = rev_args;

	for (;;) {
		const char *str = *p++;
		int len;
		if (!str)
			return 0;
		len = strlen(str);
		if (!strncmp(arg, str, len))
			return 1;
	}
}

static void show_rev(int type, const unsigned char *sha1)
{
	if (no_revs)
		return;
	output_revs++;
	printf("%s%s\n", type == show_type ? "" : "^", sha1_to_hex(sha1));
}

static void show_rev_arg(char *rev)
{
	if (no_revs)
		return;
	puts(rev);
}

static void show_norev(char *norev)
{
	if (flags_only)
		return;
	if (revs_only)
		return;
	puts(norev);
}

static void show_arg(char *arg)
{
	if (no_flags)
		return;
	if (do_rev_argument && is_rev_argument(arg))
		show_rev_arg(arg);
	else
		show_norev(arg);
}

static int get_parent(char *name, unsigned char *result, int idx)
{
	unsigned char sha1[20];
	int ret = get_extended_sha1(name, sha1);
	struct commit *commit;
	struct commit_list *p;

	if (ret)
		return ret;
	commit = lookup_commit_reference(sha1);
	if (!commit)
		return -1;
	if (parse_commit(commit))
		return -1;
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

static int get_short_sha1(char *name, unsigned char *sha1)
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

/*
 * This is like "get_sha1()", except it allows "sha1 expressions",
 * notably "xyz^" for "parent of xyz"
 */
static int get_extended_sha1(char *name, unsigned char *sha1)
{
	int parent, ret;
	int len = strlen(name);

	parent = 1;
	if (len > 2 && name[len-1] >= '1' && name[len-1] <= '9') {
		parent = name[len-1] - '0';
		len--;
	}
	if (len > 1 && name[len-1] == '^') {
		name[len-1] = 0;
		ret = get_parent(name, sha1, parent);
		name[len-1] = '^';
		if (!ret)
			return 0;
	}
	ret = get_sha1(name, sha1);
	if (!ret)
		return 0;
	return get_short_sha1(name, sha1);
}

static void show_default(void)
{
	char *s = def;

	if (s) {
		unsigned char sha1[20];

		def = NULL;
		if (!get_extended_sha1(s, sha1)) {
			show_rev(NORMAL, sha1);
			return;
		}
		show_arg(s);
	}
}

static int show_reference(const char *refname, const unsigned char *sha1)
{
	show_rev(NORMAL, sha1);
	return 0;
}

int main(int argc, char **argv)
{
	int i, as_is = 0;
	unsigned char sha1[20];

	for (i = 1; i < argc; i++) {
		char *arg = argv[i];
		char *dotdot;
	
		if (as_is) {
			show_norev(arg);
			continue;
		}
		if (*arg == '-') {
			if (!strcmp(arg, "--")) {
				show_default();
				if (revs_only)
					break;
				as_is = 1;
			}
			if (!strcmp(arg, "--default")) {
				def = argv[i+1];
				i++;
				continue;
			}
			if (!strcmp(arg, "--revs-only")) {
				revs_only = 1;
				continue;
			}
			if (!strcmp(arg, "--no-revs")) {
				no_revs = 1;
				continue;
			}
			if (!strcmp(arg, "--flags")) {
				flags_only = 1;
				continue;
			}
			if (!strcmp(arg, "--no-flags")) {
				no_flags = 1;
				continue;
			}
			if (!strcmp(arg, "--verify")) {
				revs_only = 1;
				do_rev_argument = 0;
				single_rev = 1;
				continue;
			}
			if (!strcmp(arg, "--not")) {
				show_type ^= REVERSED;
				continue;
			}
			if (!strcmp(arg, "--all")) {
				for_each_ref(show_reference);
				continue;
			}
			show_arg(arg);
			continue;
		}
		dotdot = strstr(arg, "..");
		if (dotdot) {
			unsigned char end[20];
			char *n = dotdot+2;
			*dotdot = 0;
			if (!get_extended_sha1(arg, sha1)) {
				if (!*n)
					n = "HEAD";
				if (!get_extended_sha1(n, end)) {
					if (no_revs)
						continue;
					def = NULL;
					show_rev(NORMAL, end);
					show_rev(REVERSED, sha1);
					continue;
				}
			}
			*dotdot = '.';
		}
		if (!get_extended_sha1(arg, sha1)) {
			if (no_revs)
				continue;
			def = NULL;
			show_rev(NORMAL, sha1);
			continue;
		}
		if (*arg == '^' && !get_extended_sha1(arg+1, sha1)) {
			if (no_revs)
				continue;
			def = NULL;
			show_rev(REVERSED, sha1);
			continue;
		}
		show_default();
		show_norev(arg);
	}
	show_default();
	if (single_rev && output_revs != 1) {
		fprintf(stderr, "Needed a single revision\n");
		exit(1);
	}
	return 0;
}
