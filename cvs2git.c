/*
 * cvs2git
 *
 * Copyright (C) Linus Torvalds 2005
 */

#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

static int verbose = 0;

/*
 * This is a really stupid program that takes cvsps output, and
 * generates a a long _shell_script_ that will create the GIT archive
 * from it. 
 *
 * You've been warned. I told you it was stupid.
 *
 * NOTE NOTE NOTE! In order to do branches correctly, this needs
 * the fixed cvsps that has the "Ancestor branch" tag output.
 * Hopefully David Mansfield will update his distribution soon
 * enough (he's the one who wrote the patch, so at least we don't
 * have to figt maintainer issues ;)
 *
 * Usage:
 *
 *	TZ=UTC cvsps -A |
 *		git-cvs2git --cvsroot=[root] --module=[module] > script
 *
 * Creates a shell script that will generate the .git archive of
 * the names CVS repository.
 *
 *	TZ=UTC cvsps -s 1234- -A |
 *		git-cvs2git -u --cvsroot=[root] --module=[module] > script
 *
 * Creates a shell script that will update the .git archive with
 * CVS changes from patchset 1234 until the last one.
 *
 * IMPORTANT NOTE ABOUT "cvsps"! This requires version 2.1 or better,
 * and the "TZ=UTC" and the "-A" flag is required for sane results!
 */
enum state {
	Header,
	Log,
	Members
};

static const char *cvsroot;
static const char *cvsmodule;

static char date[100];
static char author[100];
static char branch[100];
static char ancestor[100];
static char tag[100];
static char log[32768];
static int loglen = 0;
static int initial_commit = 1;

static void lookup_author(char *n, char **name, char **email)
{
	/*
	 * FIXME!!! I'm lazy and stupid.
	 *
	 * This could be something like
	 *
	 *	printf("lookup_author '%s'\n", n);
	 *	*name = "$author_name";
	 *	*email = "$author_email";
	 *
	 * and that would allow the script to do its own
	 * lookups at run-time.
	 */
	*name = n;
	*email = n;
}

static void prepare_commit(void)
{
	char *author_name, *author_email;
	char *src_branch;

	lookup_author(author, &author_name, &author_email);

	printf("export GIT_COMMITTER_NAME=%s\n", author_name);
	printf("export GIT_COMMITTER_EMAIL=%s\n", author_email);
	printf("export GIT_COMMITTER_DATE='+0000 %s'\n", date);

	printf("export GIT_AUTHOR_NAME=%s\n", author_name);
	printf("export GIT_AUTHOR_EMAIL=%s\n", author_email);
	printf("export GIT_AUTHOR_DATE='+0000 %s'\n", date);

	if (initial_commit)
		return;

	src_branch = *ancestor ? ancestor : branch;
	if (!strcmp(src_branch, "HEAD"))
		src_branch = "master";
	printf("ln -sf refs/heads/'%s' .git/HEAD\n", src_branch);

	/*
	 * Even if cvsps claims an ancestor, we'll let the new
	 * branch name take precedence if it already exists
	 */
	if (*ancestor) {
		src_branch = branch;
		if (!strcmp(src_branch, "HEAD"))
			src_branch = "master";
		printf("[ -e .git/refs/heads/'%s' ] && ln -sf refs/heads/'%s' .git/HEAD\n",
			src_branch, src_branch);
	}

	printf("git-read-tree -m HEAD || exit 1\n");
	printf("git-checkout-cache -f -u -a\n");
}

static void commit(void)
{
	const char *cmit_parent = initial_commit ? "" : "-p HEAD";
	const char *dst_branch;
	char *space;
	int i;

	printf("tree=$(git-write-tree)\n");
	printf("cat > .cmitmsg <<EOFMSG\n");

	/* Escape $ characters, and remove control characters */
	for (i = 0; i < loglen; i++) {
		unsigned char c = log[i];

		switch (c) {
		case '$':
		case '\\':
		case '`':
			putchar('\\');
			break;
		case 0 ... 31:
			if (c == '\n' || c == '\t')
				break;
		case 128 ... 159:
			continue;
		}
		putchar(c);
	}
	printf("\nEOFMSG\n");
	printf("commit=$(cat .cmitmsg | git-commit-tree $tree %s)\n", cmit_parent);

	dst_branch = branch;
	if (!strcmp(dst_branch, "HEAD"))
		dst_branch = "master";

	printf("echo $commit > .git/refs/heads/'%s'\n", dst_branch);

	space = strchr(tag, ' ');
	if (space)
		*space = 0;
	if (strcmp(tag, "(none)"))
		printf("echo $commit > .git/refs/tags/'%s'\n", tag);

	printf("echo 'Committed (to %s):' ; cat .cmitmsg; echo\n", dst_branch);

	*date = 0;
	*author = 0;
	*branch = 0;
	*ancestor = 0;
	*tag = 0;
	loglen = 0;

	initial_commit = 0;
}

static void update_file(char *line)
{
	char *name, *version;
	char *dir;

	while (isspace(*line))
		line++;
	name = line;
	line = strchr(line, ':');
	if (!line)
		return;
	*line++ = 0;
	line = strchr(line, '>');
	if (!line)
		return;
	*line++ = 0;
	version = line;
	line = strchr(line, '(');
	if (line) {	/* "(DEAD)" */
		printf("git-update-cache --force-remove '%s'\n", name);
		return;
	}

	dir = strrchr(name, '/');
	if (dir)
		printf("mkdir -p %.*s\n", (int)(dir - name), name);

	printf("cvs -q -d %s checkout -d .git-tmp -r%s '%s/%s'\n", 
		cvsroot, version, cvsmodule, name);
	printf("mv -f .git-tmp/%s %s\n", dir ? dir+1 : name, name);
	printf("rm -rf .git-tmp\n");
	printf("git-update-cache --add -- '%s'\n", name);
}

static struct hdrentry {
	const char *name;
	char *dest;
} hdrs[] = {
	{ "Date:", date },
	{ "Author:", author },
	{ "Branch:", branch },
	{ "Ancestor branch:", ancestor },
	{ "Tag:", tag },
	{ "Log:", NULL },
	{ NULL, NULL }
};

int main(int argc, char **argv)
{
	static char line[1000];
	enum state state = Header;
	int i;

	for (i = 1; i < argc; i++) {
		const char *arg = argv[i];
		if (!memcmp(arg, "--cvsroot=", 10)) {
			cvsroot = arg + 10;
			continue;
		}
		if (!memcmp(arg, "--module=", 9)) {
			cvsmodule = arg+9;
			continue;
		} 
		if (!strcmp(arg, "-v")) {
			verbose = 1;
			continue;
		}
		if (!strcmp(arg, "-u")) {
			initial_commit = 0;
			continue;
		}
	}


	if (!cvsroot)
		cvsroot = getenv("CVSROOT");

	if (!cvsmodule || !cvsroot) {
		fprintf(stderr, "I need a CVSROOT and module name\n");
		exit(1);
	}

	if (initial_commit) {
		printf("[ -d .git ] && exit 1\n");
		    printf("git-init-db\n");
		printf("mkdir -p .git/refs/heads\n");
		printf("mkdir -p .git/refs/tags\n");
		printf("ln -sf refs/heads/master .git/HEAD\n");
	}

	while (fgets(line, sizeof(line), stdin) != NULL) {
		int linelen = strlen(line);

		while (linelen && isspace(line[linelen-1]))
			line[--linelen] = 0;

		switch (state) {
		struct hdrentry *entry;

		case Header:
			if (verbose)
				printf("# H: %s\n", line);
			for (entry = hdrs ; entry->name ; entry++) {
				int len = strlen(entry->name);
				char *val;

				if (memcmp(entry->name, line, len))
					continue;
				if (!entry->dest) {
					state = Log;
					break;
				}
				val = line + len;
				linelen -= len;
				while (isspace(*val)) {
					val++;
					linelen--;
				}
				memcpy(entry->dest, val, linelen+1);
				break;
			}
			continue;

		case Log:
			if (verbose)
				printf("# L: %s\n", line);
			if (!strcmp(line, "Members:")) {
				while (loglen && isspace(log[loglen-1]))
					log[--loglen] = 0;
				prepare_commit();
				state = Members;
				continue;
			}
				
			if (loglen + linelen + 5 > sizeof(log))
				continue;
			memcpy(log + loglen, line, linelen);
			loglen += linelen;
			log[loglen++] = '\n';
			continue;

		case Members:
			if (verbose)
				printf("# M: %s\n", line);
			if (!linelen) {
				commit();
				state = Header;
				continue;
			}
			update_file(line);
			continue;
		}
	}
	return 0;
}
