/*
 * Copyright (C) 2005 Junio C Hamano
 */
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include "cache.h"
#include "commit.h"
#include <errno.h>
#include <stdio.h>
#include "pull.h"

static int use_link = 0;
static int use_symlink = 0;
static int use_filecopy = 1;
static int verbose = 0;

static char *path;

static void say(const char *fmt, const char *hex) {
	if (verbose)
		fprintf(stderr, fmt, hex);
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
	if (use_link && !link(filename, dest_filename)) {
		say("Hardlinked %s.\n", hex);
		return 0;
	}
	if (use_symlink && !symlink(filename, dest_filename)) {
		say("Symlinked %s.\n", hex);
		return 0;
	}
	if (use_filecopy) {
		int ifd, ofd, status;
		struct stat st;
		void *map;
		ifd = open(filename, O_RDONLY);
		if (ifd < 0 || fstat(ifd, &st) < 0) {
			close(ifd);
			fprintf(stderr, "Cannot open %s\n", filename);
			return -1;
		}
		map = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, ifd, 0);
		close(ifd);
		if (-1 == (int)(long)map) {
			fprintf(stderr, "Cannot mmap %s\n", filename);
			return -1;
		}
		ofd = open(dest_filename, O_WRONLY | O_CREAT | O_EXCL, 0666);
		status = ((ofd < 0) ||
			  (write(ofd, map, st.st_size) != st.st_size));
		munmap(map, st.st_size);
		close(ofd);
		if (status)
			fprintf(stderr, "Cannot write %s (%ld bytes)\n",
				dest_filename, st.st_size);
		else
			say("Copied %s.\n", hex);
		return status;
	}
	fprintf(stderr, "No copy method was provided to copy %s.\n", hex);
	return -1;
}

static const char *local_pull_usage = 
"git-local-pull [-c] [-t] [-a] [-l] [-s] [-n] [-v] commit-id path";

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
			verbose = 1;
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
