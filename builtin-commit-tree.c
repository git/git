/*
 * GIT - The information manager from hell
 *
 * Copyright (C) Linus Torvalds, 2005
 */
#include "cache.h"
#include "commit.h"
#include "tree.h"
#include "builtin.h"
#include "utf8.h"

#define BLOCKING (1ul << 14)

/*
 * FIXME! Share the code with "write-tree.c"
 */
static void init_buffer(char **bufp, unsigned int *sizep)
{
	char *buf = xmalloc(BLOCKING);
	*sizep = 0;
	*bufp = buf;
}

static void add_buffer(char **bufp, unsigned int *sizep, const char *fmt, ...)
{
	char one_line[2048];
	va_list args;
	int len;
	unsigned long alloc, size, newsize;
	char *buf;

	va_start(args, fmt);
	len = vsnprintf(one_line, sizeof(one_line), fmt, args);
	va_end(args);
	size = *sizep;
	newsize = size + len + 1;
	alloc = (size + 32767) & ~32767;
	buf = *bufp;
	if (newsize > alloc) {
		alloc = (newsize + 32767) & ~32767;
		buf = xrealloc(buf, alloc);
		*bufp = buf;
	}
	*sizep = newsize - 1;
	memcpy(buf + size, one_line, len);
}

static void check_valid(unsigned char *sha1, enum object_type expect)
{
	enum object_type type = sha1_object_info(sha1, NULL);
	if (type < 0)
		die("%s is not a valid object", sha1_to_hex(sha1));
	if (type != expect)
		die("%s is not a valid '%s' object", sha1_to_hex(sha1),
		    typename(expect));
}

/*
 * Having more than two parents is not strange at all, and this is
 * how multi-way merges are represented.
 */
#define MAXPARENT (16)
static unsigned char parent_sha1[MAXPARENT][20];

static const char commit_tree_usage[] = "git-commit-tree <sha1> [-p <sha1>]* < changelog";

static int new_parent(int idx)
{
	int i;
	unsigned char *sha1 = parent_sha1[idx];
	for (i = 0; i < idx; i++) {
		if (!hashcmp(parent_sha1[i], sha1)) {
			error("duplicate parent %s ignored", sha1_to_hex(sha1));
			return 0;
		}
	}
	return 1;
}

static const char commit_utf8_warn[] =
"Warning: commit message does not conform to UTF-8.\n"
"You may want to amend it after fixing the message, or set the config\n"
"variable i18n.commitencoding to the encoding your project uses.\n";

int cmd_commit_tree(int argc, const char **argv, const char *prefix)
{
	int i;
	int parents = 0;
	unsigned char tree_sha1[20];
	unsigned char commit_sha1[20];
	char comment[1000];
	char *buffer;
	unsigned int size;
	int encoding_is_utf8;

	git_config(git_default_config);

	if (argc < 2)
		usage(commit_tree_usage);
	if (get_sha1(argv[1], tree_sha1))
		die("Not a valid object name %s", argv[1]);

	check_valid(tree_sha1, OBJ_TREE);
	for (i = 2; i < argc; i += 2) {
		const char *a, *b;
		a = argv[i]; b = argv[i+1];
		if (!b || strcmp(a, "-p"))
			usage(commit_tree_usage);

		if (parents >= MAXPARENT)
			die("Too many parents (%d max)", MAXPARENT);
		if (get_sha1(b, parent_sha1[parents]))
			die("Not a valid object name %s", b);
		check_valid(parent_sha1[parents], OBJ_COMMIT);
		if (new_parent(parents))
			parents++;
	}

	/* Not having i18n.commitencoding is the same as having utf-8 */
	encoding_is_utf8 = is_encoding_utf8(git_commit_encoding);

	init_buffer(&buffer, &size);
	add_buffer(&buffer, &size, "tree %s\n", sha1_to_hex(tree_sha1));

	/*
	 * NOTE! This ordering means that the same exact tree merged with a
	 * different order of parents will be a _different_ changeset even
	 * if everything else stays the same.
	 */
	for (i = 0; i < parents; i++)
		add_buffer(&buffer, &size, "parent %s\n", sha1_to_hex(parent_sha1[i]));

	/* Person/date information */
	add_buffer(&buffer, &size, "author %s\n", git_author_info(1));
	add_buffer(&buffer, &size, "committer %s\n", git_committer_info(1));
	if (!encoding_is_utf8)
		add_buffer(&buffer, &size,
				"encoding %s\n", git_commit_encoding);
	add_buffer(&buffer, &size, "\n");

	/* And add the comment */
	while (fgets(comment, sizeof(comment), stdin) != NULL)
		add_buffer(&buffer, &size, "%s", comment);

	/* And check the encoding */
	buffer[size] = '\0';
	if (encoding_is_utf8 && !is_utf8(buffer))
		fprintf(stderr, commit_utf8_warn);

	if (!write_sha1_file(buffer, size, commit_type, commit_sha1)) {
		printf("%s\n", sha1_to_hex(commit_sha1));
		return 0;
	}
	else
		return 1;
}
