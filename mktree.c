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
	hashcpy(ent->sha1, sha1);
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
	struct strbuf buf;
	size_t size;
	int i;

	qsort(entries, used, sizeof(*entries), ent_compare);
	for (size = i = 0; i < used; i++)
		size += 32 + entries[i]->len;
	strbuf_init(&buf);
	strbuf_grow(&buf, size);

	for (i = 0; i < used; i++) {
		struct treeent *ent = entries[i];
		strbuf_addf(&buf, "%o %s%c", ent->mode, ent->name, '\0');
		strbuf_add(&buf, ent->sha1, 20);
	}

	write_sha1_file(buf.buf, buf.len, tree_type, sha1);
	strbuf_release(&buf);
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
		char *ptr, *ntr;
		unsigned mode;
		enum object_type type;
		char *path;

		read_line(&sb, stdin, line_termination);
		if (sb.eof)
			break;
		ptr = sb.buf;
		/* Input is non-recursive ls-tree output format
		 * mode SP type SP sha1 TAB name
		 */
		mode = strtoul(ptr, &ntr, 8);
		if (ptr == ntr || !ntr || *ntr != ' ')
			die("input format error: %s", sb.buf);
		ptr = ntr + 1; /* type */
		ntr = strchr(ptr, ' ');
		if (!ntr || sb.buf + sb.len <= ntr + 40 ||
		    ntr[41] != '\t' ||
		    get_sha1_hex(ntr + 1, sha1))
			die("input format error: %s", sb.buf);
		type = sha1_object_info(sha1, NULL);
		if (type < 0)
			die("object %s unavailable", sha1_to_hex(sha1));
		*ntr++ = 0; /* now at the beginning of SHA1 */
		if (type != type_from_string(ptr))
			die("object type %s mismatch (%s)", ptr, typename(type));
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
