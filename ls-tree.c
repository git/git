/*
 * GIT - The information manager from hell
 *
 * Copyright (C) Linus Torvalds, 2005
 */
#include "cache.h"

static int line_termination = '\n';
static int recursive = 0;

struct path_prefix {
	struct path_prefix *prev;
	const char *name;
};

#define DEBUG(fmt, ...)	

static int string_path_prefix(char *buff, size_t blen, struct path_prefix *prefix)
{
	int len = 0;
	if (prefix) {
		if (prefix->prev) {
			len = string_path_prefix(buff,blen,prefix->prev);
			buff += len;
			blen -= len;
			if (blen > 0) {
				*buff = '/';
				len++;
				buff++;
				blen--;
			}
		}
		strncpy(buff,prefix->name,blen);
		return len + strlen(prefix->name);
	}

	return 0;
}

static void print_path_prefix(struct path_prefix *prefix)
{
	if (prefix) {
		if (prefix->prev) {
			print_path_prefix(prefix->prev);
			putchar('/');
		}
		fputs(prefix->name, stdout);
	}
}

/*
 * return:
 * 	-1 if prefix is *not* a subset of path
 * 	 0 if prefix == path
 * 	 1 if prefix is a subset of path
 */
static int pathcmp(const char *path, struct path_prefix *prefix)
{
	char buff[PATH_MAX];
	int len,slen;

	if (prefix == NULL)
		return 1;

	len = string_path_prefix(buff, sizeof buff, prefix);
	slen = strlen(path);

	if (slen < len)
		return -1;

	if (strncmp(path,buff,len) == 0) {
		if (slen == len)
			return 0;
		else
			return 1;
	}

	return -1;
}	

/*
 * match may be NULL, or a *sorted* list of paths
 */
static void list_recursive(void *buffer,
			   const char *type,
			   unsigned long size,
			   struct path_prefix *prefix,
			   char **match, int matches)
{
	struct path_prefix this_prefix;
	this_prefix.prev = prefix;

	if (strcmp(type, "tree"))
		die("expected a 'tree' node");

	if (matches)
		recursive = 1;

	while (size) {
		int namelen = strlen(buffer)+1;
		void *eltbuf = NULL;
		char elttype[20];
		unsigned long eltsize;
		unsigned char *sha1 = buffer + namelen;
		char *path = strchr(buffer, ' ') + 1;
		unsigned int mode;
		const char *matched = NULL;
		int mtype = -1;
		int mindex;

		if (size < namelen + 20 || sscanf(buffer, "%o", &mode) != 1)
			die("corrupt 'tree' file");
		buffer = sha1 + 20;
		size -= namelen + 20;

		this_prefix.name = path;
		for ( mindex = 0; mindex < matches; mindex++) {
			mtype = pathcmp(match[mindex],&this_prefix);
			if (mtype >= 0) {
				matched = match[mindex];
				break;
			}
		}

		/*
		 * If we're not matching, or if this is an exact match,
		 * print out the info
		 */
		if (!matches || (matched != NULL && mtype == 0)) {
			printf("%06o\t%s\t%s\t", mode,
			       S_ISDIR(mode) ? "tree" : "blob",
			       sha1_to_hex(sha1));
			print_path_prefix(&this_prefix);
			putchar(line_termination);
		}

		if (! recursive || ! S_ISDIR(mode))
			continue;

		if (matches && ! matched)
			continue;

		if (! (eltbuf = read_sha1_file(sha1, elttype, &eltsize)) ) {
			error("cannot read %s", sha1_to_hex(sha1));
			continue;
		}

		/* If this is an exact directory match, we may have
		 * directory files following this path. Match on them.
		 * Otherwise, we're at a pach subcomponent, and we need
		 * to try to match again.
		 */
		if (mtype == 0)
			mindex++;

		list_recursive(eltbuf, elttype, eltsize, &this_prefix, &match[mindex], matches-mindex);
		free(eltbuf);
	}
}

static int qcmp(const void *a, const void *b)
{
	return strcmp(*(char **)a, *(char **)b);
}

static int list(unsigned char *sha1,char **path)
{
	void *buffer;
	unsigned long size;
	int npaths;

	for (npaths = 0; path[npaths] != NULL; npaths++)
		;

	qsort(path,npaths,sizeof(char *),qcmp);

	buffer = read_object_with_reference(sha1, "tree", &size, NULL);
	if (!buffer)
		die("unable to read sha1 file");
	list_recursive(buffer, "tree", size, NULL, path, npaths);
	free(buffer);
	return 0;
}

static const char *ls_tree_usage = "git-ls-tree [-r] [-z] <key> [paths...]";

int main(int argc, char **argv)
{
	unsigned char sha1[20];

	while (1 < argc && argv[1][0] == '-') {
		switch (argv[1][1]) {
		case 'z':
			line_termination = 0;
			break;
		case 'r':
			recursive = 1;
			break;
		default:
			usage(ls_tree_usage);
		}
		argc--; argv++;
	}

	if (argc < 2)
		usage(ls_tree_usage);
	if (get_sha1(argv[1], sha1) < 0)
		usage(ls_tree_usage);
	if (list(sha1, &argv[2]) < 0)
		die("list failed");
	return 0;
}
