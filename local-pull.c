/*
 * Copyright (C) 2005 Junio C Hamano
 */
#include "cache.h"
#include "commit.h"
#include "pull.h"

static int use_link = 0;
static int use_symlink = 0;
static int use_filecopy = 1;

static char *path; /* "Remote" git repository */

void prefetch(unsigned char *sha1)
{
}

int fetch(unsigned char *sha1)
{
	static int object_name_start = -1;
	static char filename[PATH_MAX];
	char *hex = sha1_to_hex(sha1);
	const char *dest_filename = sha1_file_name(sha1);

	if (object_name_start < 0) {
		strcpy(filename, path); /* e.g. git.git */
		strcat(filename, "/objects/");
		object_name_start = strlen(filename);
	}
	filename[object_name_start+0] = hex[0];
	filename[object_name_start+1] = hex[1];
	filename[object_name_start+2] = '/';
	strcpy(filename + object_name_start + 3, hex + 2);
	if (use_link) {
		if (!link(filename, dest_filename)) {
			pull_say("link %s\n", hex);
			return 0;
		}
		/* If we got ENOENT there is no point continuing. */
		if (errno == ENOENT) {
			fprintf(stderr, "does not exist %s\n", filename);
			return -1;
		}
	}
	if (use_symlink && !symlink(filename, dest_filename)) {
		pull_say("symlink %s\n", hex);
		return 0;
	}
	if (use_filecopy) {
		int ifd, ofd, status;
		struct stat st;
		void *map;
		ifd = open(filename, O_RDONLY);
		if (ifd < 0 || fstat(ifd, &st) < 0) {
			close(ifd);
			fprintf(stderr, "cannot open %s\n", filename);
			return -1;
		}
		map = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, ifd, 0);
		close(ifd);
		if (map == MAP_FAILED) {
			fprintf(stderr, "cannot mmap %s\n", filename);
			return -1;
		}
		ofd = open(dest_filename, O_WRONLY | O_CREAT | O_EXCL, 0666);
		status = ((ofd < 0) ||
			  (write(ofd, map, st.st_size) != st.st_size));
		munmap(map, st.st_size);
		close(ofd);
		if (status)
			fprintf(stderr, "cannot write %s\n", dest_filename);
		else
			pull_say("copy %s\n", hex);
		return status;
	}
	fprintf(stderr, "failed to copy %s with given copy methods.\n", hex);
	return -1;
}

int fetch_ref(char *ref, unsigned char *sha1)
{
	static int ref_name_start = -1;
	static char filename[PATH_MAX];
	static char hex[41];
	int ifd;

	if (ref_name_start < 0) {
		sprintf(filename, "%s/refs/", path);
		ref_name_start = strlen(filename);
	}
	strcpy(filename + ref_name_start, ref);
	ifd = open(filename, O_RDONLY);
	if (ifd < 0) {
		close(ifd);
		fprintf(stderr, "cannot open %s\n", filename);
		return -1;
	}
	if (read(ifd, hex, 40) != 40 || get_sha1_hex(hex, sha1)) {
		close(ifd);
		fprintf(stderr, "cannot read from %s\n", filename);
		return -1;
	}
	close(ifd);
	pull_say("ref %s\n", sha1_to_hex(sha1));
	return 0;
}

static const char local_pull_usage[] =
"git-local-pull [-c] [-t] [-a] [-d] [-v] [-w filename] [--recover] [-l] [-s] [-n] commit-id path";

/* 
 * By default we only use file copy.
 * If -l is specified, a hard link is attempted.
 * If -s is specified, then a symlink is attempted.
 * If -n is _not_ specified, then a regular file-to-file copy is done.
 */
int main(int argc, char **argv)
{
	char *commit_id;
	int arg = 1;

	while (arg < argc && argv[arg][0] == '-') {
		if (argv[arg][1] == 't')
			get_tree = 1;
		else if (argv[arg][1] == 'c')
			get_history = 1;
		else if (argv[arg][1] == 'a') {
			get_all = 1;
			get_tree = 1;
			get_history = 1;
		}
		else if (argv[arg][1] == 'l')
			use_link = 1;
		else if (argv[arg][1] == 's')
			use_symlink = 1;
		else if (argv[arg][1] == 'n')
			use_filecopy = 0;
		else if (argv[arg][1] == 'v')
			get_verbosely = 1;
		else if (argv[arg][1] == 'w')
			write_ref = argv[++arg];
		else
			usage(local_pull_usage);
		arg++;
	}
	if (argc < arg + 2)
		usage(local_pull_usage);
	commit_id = argv[arg];
	path = argv[arg + 1];

	if (pull(commit_id))
		return 1;

	return 0;
}
