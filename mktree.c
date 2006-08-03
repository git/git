/*
 * GIT - the stupid content tracker
 *
 * Copyright (c) Junio C Hamano, 2006
 */
#include "cache.h"
#include "strbuf.h"
#include "quote.h"
#include "tree.h"

static struct treeent {
	unsigned mode;
	unsigned char sha1[20];
	int len;
	char name[FLEX_ARRAY];
} **entries;
static int alloc, used;

static void append_to_tree(unsigned mode, unsigned char *sha1, char *path)
{
	struct treeent *ent;
	int len = strlen(path);
	if (strchr(path, '/'))
		die("path %s contains slash", path);

	if (alloc <= used) {
		alloc = alloc_nr(used);
		entries = xrealloc(entries, sizeof(*entries) * alloc);
	}
	ent = entries[used++] = xmalloc(sizeof(**entries) + len + 1);
	ent->mode = mode;
	ent->len = len;
	memcpy(ent->sha1, sha1, 20);
	memcpy(ent->name, path, len+1);
}

static int ent_compare(const void *a_, const void *b_)
{
	struct treeent *a = *(struct treeent **)a_;
	struct treeent *b = *(struct treeent **)b_;
	return base_name_compare(a->name, a->len, a->mode,
				 b->name, b->len, b->mode);
}

static void write_tree(unsigned char *sha1)
{
	char *buffer;
	unsigned long size, offset;
	int i;

	qsort(entries, used, sizeof(*entries), ent_compare);
	size = 100;
	for (size = i = 0; i < used; i++)
		size += 32 + entries[i]->len;
	buffer = xmalloc(size);
	offset = 0;

	for (i = 0; i < used; i++) {
		struct treeent *ent = entries[i];

		if (offset + ent->len + 100 < size) {
			size = alloc_nr(offset + ent->len + 100);
			buffer = xrealloc(buffer, size);
		}
		offset += sprintf(buffer + offset, "%o ", ent->mode);
		offset += sprintf(buffer + offset, "%s", ent->name);
		buffer[offset++] = 0;
		memcpy(buffer + offset, ent->sha1, 20);
		offset += 20;
	}
	write_sha1_file(buffer, offset, tree_type, sha1);
}

static const char mktree_usage[] = "git-mktree [-z]";

int main(int ac, char **av)
{
	struct strbuf sb;
	unsigned char sha1[20];
	int line_termination = '\n';

	setup_git_directory();

	while ((1 < ac) && av[1][0] == '-') {
		char *arg = av[1];
		if (!strcmp("-z", arg))
			line_termination = 0;
		else
			usage(mktree_usage);
		ac--;
		av++;
	}

	strbuf_init(&sb);
	while (1) {
		int len;
		char *ptr, *ntr;
		unsigned mode;
		char type[20];
		char *path;

		read_line(&sb, stdin, line_termination);
		if (sb.eof)
			break;
		len = sb.len;
		ptr = sb.buf;
		/* Input is non-recursive ls-tree output format
		 * mode SP type SP sha1 TAB name
		 */
		mode = strtoul(ptr, &ntr, 8);
		if (ptr == ntr || !ntr || *ntr != ' ')
			die("input format error: %s", sb.buf);
		ptr = ntr + 1; /* type */
		ntr = strchr(ptr, ' ');
		if (!ntr || sb.buf + len <= ntr + 41 ||
		    ntr[41] != '\t' ||
		    get_sha1_hex(ntr + 1, sha1))
			die("input format error: %s", sb.buf);
		if (sha1_object_info(sha1, type, NULL))
			die("object %s unavailable", sha1_to_hex(sha1));
		*ntr++ = 0; /* now at the beginning of SHA1 */
		if (strcmp(ptr, type))
			die("object type %s mismatch (%s)", ptr, type);
		ntr += 41; /* at the beginning of name */
		if (line_termination && ntr[0] == '"')
			path = unquote_c_style(ntr, NULL);
		else
			path = ntr;

		append_to_tree(mode, sha1, path);

		if (path != ntr)
			free(path);
	}
	write_tree(sha1);
	puts(sha1_to_hex(sha1));
	exit(0);
}
