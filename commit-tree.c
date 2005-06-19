/*
 * GIT - The information manager from hell
 *
 * Copyright (C) Linus Torvalds, 2005
 */
#include "cache.h"

#include <pwd.h>
#include <time.h>
#include <ctype.h>

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
	newsize = size + len;
	alloc = (size + 32767) & ~32767;
	buf = *bufp;
	if (newsize > alloc) {
		alloc = (newsize + 32767) & ~32767;
		buf = xrealloc(buf, alloc);
		*bufp = buf;
	}
	*sizep = newsize;
	memcpy(buf + size, one_line, len);
}

static void remove_special(char *p)
{
	char c;
	char *dst = p, *src = p;

	for (;;) {
		c = *src;
		src++;
		switch(c) {
		case '\n': case '<': case '>':
			continue;
		}
		*dst++ = c;
		if (!c)
			break;
	}

	/*
	 * Go back, and remove crud from the end: some people
	 * have commas etc in their gecos field
	 */
	dst--;
	while (--dst >= p) {
		unsigned char c = *dst;
		switch (c) {
		case ',': case ';': case '.':
			*dst = 0;
			continue;
		}
		break;
	}
}

static void check_valid(unsigned char *sha1, const char *expect)
{
	void *buf;
	char type[20];
	unsigned long size;

	buf = read_sha1_file(sha1, type, &size);
	if (!buf || strcmp(type, expect))
		die("%s is not a valid '%s' object", sha1_to_hex(sha1), expect);
	free(buf);
}

/*
 * Having more than two parents is not strange at all, and this is
 * how multi-way merges are represented.
 */
#define MAXPARENT (16)
static unsigned char parent_sha1[MAXPARENT][20];

static char *commit_tree_usage = "git-commit-tree <sha1> [-p <sha1>]* < changelog";

static int new_parent(int idx)
{
	int i;
	unsigned char *sha1 = parent_sha1[idx];
	for (i = 0; i < idx; i++) {
		if (!memcmp(parent_sha1[i], sha1, 20)) {
			error("duplicate parent %s ignored", sha1_to_hex(sha1));
			return 0;
		}
	}
	return 1;
}

int main(int argc, char **argv)
{
	int i, len;
	int parents = 0;
	unsigned char tree_sha1[20];
	unsigned char commit_sha1[20];
	char *gecos, *realgecos, *commitgecos;
	char *email, *commitemail, realemail[1000];
	char date[50], realdate[50];
	char *audate, *cmdate;
	char comment[1000];
	struct passwd *pw;
	char *buffer;
	unsigned int size;

	if (argc < 2 || get_sha1_hex(argv[1], tree_sha1) < 0)
		usage(commit_tree_usage);

	check_valid(tree_sha1, "tree");
	for (i = 2; i < argc; i += 2) {
		char *a, *b;
		a = argv[i]; b = argv[i+1];
		if (!b || strcmp(a, "-p") || get_sha1(b, parent_sha1[parents]))
			usage(commit_tree_usage);
		check_valid(parent_sha1[parents], "commit");
		if (new_parent(parents))
			parents++;
	}
	if (!parents)
		fprintf(stderr, "Committing initial tree %s\n", argv[1]);
	pw = getpwuid(getuid());
	if (!pw)
		die("You don't exist. Go away!");
	realgecos = pw->pw_gecos;
	len = strlen(pw->pw_name);
	memcpy(realemail, pw->pw_name, len);
	realemail[len] = '@';
	gethostname(realemail+len+1, sizeof(realemail)-len-1);
	if (!strchr(realemail+len+1, '.')) {
		strcat(realemail, ".");
		getdomainname(realemail+strlen(realemail), sizeof(realemail)-strlen(realemail)-1);
	}

	datestamp(realdate, sizeof(realdate));
	strcpy(date, realdate);

	commitgecos = gitenv("GIT_COMMITTER_NAME") ? : realgecos;
	commitemail = gitenv("GIT_COMMITTER_EMAIL") ? : realemail;
	gecos = gitenv("GIT_AUTHOR_NAME") ? : realgecos;
	email = gitenv("GIT_AUTHOR_EMAIL") ? : realemail;
	audate = gitenv("GIT_AUTHOR_DATE");
	if (audate)
		parse_date(audate, date, sizeof(date));
	cmdate = gitenv("GIT_COMMITTER_DATE");
	if (cmdate)
		parse_date(cmdate, realdate, sizeof(realdate));

	remove_special(gecos); remove_special(realgecos); remove_special(commitgecos);
	remove_special(email); remove_special(realemail); remove_special(commitemail);

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
	add_buffer(&buffer, &size, "author %s <%s> %s\n", gecos, email, date);
	add_buffer(&buffer, &size, "committer %s <%s> %s\n\n", commitgecos, commitemail, realdate);

	/* And add the comment */
	while (fgets(comment, sizeof(comment), stdin) != NULL)
		add_buffer(&buffer, &size, "%s", comment);

	write_sha1_file(buffer, size, "commit", commit_sha1);
	printf("%s\n", sha1_to_hex(commit_sha1));
	return 0;
}
