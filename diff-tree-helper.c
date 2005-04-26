#include "cache.h"
#include "strbuf.h"
#include "diff.h"

static int matches_pathspec(const char *name, char **spec, int cnt)
{
	int i;
	int namelen = strlen(name);
	for (i = 0; i < cnt; i++) {
		int speclen = strlen(spec[i]);
		if (! strncmp(spec[i], name, speclen) &&
		    speclen <= namelen &&
		    (name[speclen] == 0 ||
		     name[speclen] == '/'))
			return 1;
	}
	return 0;
}

static int parse_oneside_change(const char *cp, unsigned char *sha1,
				char *path) {
	int ch;
	while ((ch = *cp) && '0' <= ch && ch <= '7')
		cp++; /* skip mode bits */
	if (strncmp(cp, "\tblob\t", 6))
		return -1;
	cp += 6;
	if (get_sha1_hex(cp, sha1))
		return -1;
	cp += 40;
	if (*cp++ != '\t')
		return -1;
	strcpy(path, cp);
	return 0;
}

#define STATUS_CACHED    0 /* cached and sha1 valid */
#define STATUS_ABSENT    1 /* diff-tree says old removed or new added */
#define STATUS_UNCACHED  2 /* diff-cache output: read from working tree */

static int parse_diff_tree_output(const char *buf,
				  unsigned char *old_sha1,
				  int *old_status,
				  unsigned char *new_sha1,
				  int *new_status,
				  char *path) {
	const char *cp = buf;
	int ch;
	static unsigned char null_sha[20] = { 0, };

	switch (*cp++) {
	case '+':
		*old_status = STATUS_ABSENT;
		*new_status = (memcmp(new_sha1, null_sha, sizeof(null_sha)) ?
			       STATUS_CACHED : STATUS_UNCACHED);
		return parse_oneside_change(cp, new_sha1, path);
	case '-':
		*new_status = STATUS_ABSENT;
		*old_status = (memcmp(old_sha1, null_sha, sizeof(null_sha)) ?
			       STATUS_CACHED : STATUS_UNCACHED);
		return parse_oneside_change(cp, old_sha1, path);
	case '*':
		break;
	default:
		return -1;
	}
	
	/* This is for '*' entries */
	while ((ch = *cp) && ('0' <= ch && ch <= '7'))
		cp++; /* skip mode bits */
	if (strncmp(cp, "->", 2))
		return -1;
	cp += 2;
	while ((ch = *cp) && ('0' <= ch && ch <= '7'))
		cp++; /* skip mode bits */
	if (strncmp(cp, "\tblob\t", 6))
		return -1;
	cp += 6;
	if (get_sha1_hex(cp, old_sha1))
		return -1;
	cp += 40;
	if (strncmp(cp, "->", 2))
		return -1;
	cp += 2;
	if (get_sha1_hex(cp, new_sha1))
		return -1;
	cp += 40;
	if (*cp++ != '\t')
		return -1;
	strcpy(path, cp);
	*old_status = (memcmp(old_sha1, null_sha, sizeof(null_sha)) ?
		       STATUS_CACHED : STATUS_UNCACHED);
	*new_status = (memcmp(new_sha1, null_sha, sizeof(null_sha)) ?
		       STATUS_CACHED : STATUS_UNCACHED);
	return 0;
}

static int sha1err(const char *path, const unsigned char *sha1)
{
	return error("diff-tree-helper: unable to read sha1 file of %s (%s)",
		     path, sha1_to_hex(sha1));
}

static int fserr(const char *path)
{
	return error("diff-tree-helper: unable to read file %s", path);
}

static char *map_whole_file(const char *path, unsigned long *size) {
	int fd;
	struct stat st;
	void *buf;

	if ((fd = open(path, O_RDONLY)) < 0) {
		error("diff-tree-helper: unable to read file %s", path);
		return 0;
	}
	if (fstat(fd, &st) < 0) {
		close(fd);
		error("diff-tree-helper: unable to stat file %s", path);
		return 0;
	}
	*size = st.st_size;
	buf = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
	close(fd);
	return buf;
}

static int show_diff(const unsigned char *old_sha1, int old_status,
		     const unsigned char *new_sha1, int new_status,
		     const char *path, int reverse_diff)
{
	char other[PATH_MAX];
	unsigned long size;
	char type[20];
	int fd;
	int reverse;
	void *blob = 0;
	const char *fs = 0;
	int need_unmap = 0;
	int need_unlink = 0;


	switch (old_status) {
	case STATUS_CACHED:
		blob = read_sha1_file(old_sha1, type, &size);
		if (! blob)
			return sha1err(path, old_sha1);
			
		switch (new_status) {
		case STATUS_CACHED:
			strcpy(other, ".diff_tree_helper_XXXXXX");
			fd = mkstemp(other);
			if (fd < 0)
				die("unable to create temp-file");
			if (write(fd, blob, size) != size)
				die("unable to write temp-file");
			close(fd);
			free(blob);

			blob = read_sha1_file(new_sha1, type, &size);
			if (! blob)
				return sha1err(path, new_sha1);

			need_unlink = 1;
			/* new = blob, old = fs */
			reverse = !reverse_diff;
			fs = other;
			break;

		case STATUS_ABSENT:
		case STATUS_UNCACHED:
			fs = ((new_status == STATUS_ABSENT) ?
			      "/dev/null" : path);
			reverse = reverse_diff;
			break;

		default:
 			reverse = reverse_diff;
		}
		break;

	case STATUS_ABSENT:
		switch (new_status) {
		case STATUS_CACHED:
			blob = read_sha1_file(new_sha1, type, &size);
			if (! blob)
				return sha1err(path, new_sha1);
			/* old = fs, new = blob */
			fs = "/dev/null";
			reverse = !reverse_diff;
			break;

		case STATUS_ABSENT:
			return error("diff-tree-helper: absent from both old and new?");
		case STATUS_UNCACHED:
			fs = path;
			blob = strdup("");
			size = 0;
			/* old = blob, new = fs */
			reverse = reverse_diff;
			break;
		default:
			reverse = reverse_diff;
		}
		break;

	case STATUS_UNCACHED:
		fs = path; /* old = fs, new = blob */
		reverse = !reverse_diff;

		switch (new_status) {
		case STATUS_CACHED:
			blob = read_sha1_file(new_sha1, type, &size);
			if (! blob)
				return sha1err(path, new_sha1);
			break;

		case STATUS_ABSENT:
			blob = strdup("");
			size = 0;
			break;

		case STATUS_UNCACHED:
			/* old = fs */
			blob = map_whole_file(path, &size);
			if (! blob)
				return fserr(path);
			need_unmap = 1;
			break;
		default:
			reverse = reverse_diff;
		}
		break;

	default:
		reverse = reverse_diff;
	}
	
	if (fs)
		show_differences(fs,
				 path, /* label */
				 blob,
				 size,
				 reverse /* 0: diff blob fs
					    1: diff fs blob */);

	if (need_unlink)
		unlink(other);
	if (need_unmap && blob)
		munmap(blob, size);
	else
		free(blob);
	return 0;
}

static const char *diff_tree_helper_usage =
"diff-tree-helper [-R] [-z] paths...";

int main(int ac, char **av) {
	struct strbuf sb;
	int reverse_diff = 0;
	int line_termination = '\n';

	strbuf_init(&sb);

	while (1 < ac && av[1][0] == '-') {
		if (av[1][1] == 'R')
			reverse_diff = 1;
		else if (av[1][1] == 'z')
			line_termination = 0;
		else
			usage(diff_tree_helper_usage);
		ac--; av++;
	}
	/* the remaining parameters are paths patterns */

	prepare_diff_cmd();

	while (1) {
		int old_status, new_status;
		unsigned char old_sha1[20], new_sha1[20];
		char path[PATH_MAX];
		read_line(&sb, stdin, line_termination);
		if (sb.eof)
			break;
		if (parse_diff_tree_output(sb.buf,
					   old_sha1, &old_status,
					   new_sha1, &new_status,
					   path)) {
			fprintf(stderr, "cannot parse %s\n", sb.buf);
			continue;
		}
		if (1 < ac && ! matches_pathspec(path, av+1, ac-1))
			continue;

		show_diff(old_sha1, old_status,
			  new_sha1, new_status,
			  path, reverse_diff);
	}
	return 0;
}
