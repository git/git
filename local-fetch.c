/*
 * Copyright (C) 2005 Junio C Hamano
 */
#include "cache.h"
#include "commit.h"
#include "fetch.h"

static int use_link = 0;
static int use_symlink = 0;
static int use_filecopy = 1;

static char *path; /* "Remote" git repository */

void prefetch(unsigned char *sha1)
{
}

static struct packed_git *packs = NULL;

static void setup_index(unsigned char *sha1)
{
	struct packed_git *new_pack;
	char filename[PATH_MAX];
	strcpy(filename, path);
	strcat(filename, "/objects/pack/pack-");
	strcat(filename, sha1_to_hex(sha1));
	strcat(filename, ".idx");
	new_pack = parse_pack_index_file(sha1, filename);
	new_pack->next = packs;
	packs = new_pack;
}

static int setup_indices(void)
{
	DIR *dir;
	struct dirent *de;
	char filename[PATH_MAX];
	unsigned char sha1[20];
	sprintf(filename, "%s/objects/pack/", path);
	dir = opendir(filename);
	if (!dir)
		return -1;
	while ((de = readdir(dir)) != NULL) {
		int namelen = strlen(de->d_name);
		if (namelen != 50 || 
		    strcmp(de->d_name + namelen - 5, ".pack"))
			continue;
		get_sha1_hex(de->d_name + 5, sha1);
		setup_index(sha1);
	}
	closedir(dir);
	return 0;
}

static int copy_file(const char *source, char *dest, const char *hex,
		     int warn_if_not_exists)
{
	safe_create_leading_directories(dest);
	if (use_link) {
		if (!link(source, dest)) {
			pull_say("link %s\n", hex);
			return 0;
		}
		/* If we got ENOENT there is no point continuing. */
		if (errno == ENOENT) {
			if (warn_if_not_exists)
				fprintf(stderr, "does not exist %s\n", source);
			return -1;
		}
	}
	if (use_symlink) {
		struct stat st;
		if (stat(source, &st)) {
			if (!warn_if_not_exists && errno == ENOENT)
				return -1;
			fprintf(stderr, "cannot stat %s: %s\n", source,
				strerror(errno));
			return -1;
		}
		if (!symlink(source, dest)) {
			pull_say("symlink %s\n", hex);
			return 0;
		}
	}
	if (use_filecopy) {
		int ifd, ofd, status = 0;

		ifd = open(source, O_RDONLY);
		if (ifd < 0) {
			if (!warn_if_not_exists && errno == ENOENT)
				return -1;
			fprintf(stderr, "cannot open %s\n", source);
			return -1;
		}
		ofd = open(dest, O_WRONLY | O_CREAT | O_EXCL, 0666);
		if (ofd < 0) {
			fprintf(stderr, "cannot open %s\n", dest);
			close(ifd);
			return -1;
		}
		status = copy_fd(ifd, ofd);
		close(ofd);
		if (status)
			fprintf(stderr, "cannot write %s\n", dest);
		else
			pull_say("copy %s\n", hex);
		return status;
	}
	fprintf(stderr, "failed to copy %s with given copy methods.\n", hex);
	return -1;
}

static int fetch_pack(const unsigned char *sha1)
{
	struct packed_git *target;
	char filename[PATH_MAX];
	if (setup_indices())
		return -1;
	target = find_sha1_pack(sha1, packs);
	if (!target)
		return error("Couldn't find %s: not separate or in any pack", 
			     sha1_to_hex(sha1));
	if (get_verbosely) {
		fprintf(stderr, "Getting pack %s\n",
			sha1_to_hex(target->sha1));
		fprintf(stderr, " which contains %s\n",
			sha1_to_hex(sha1));
	}
	sprintf(filename, "%s/objects/pack/pack-%s.pack", 
		path, sha1_to_hex(target->sha1));
	copy_file(filename, sha1_pack_name(target->sha1),
		  sha1_to_hex(target->sha1), 1);
	sprintf(filename, "%s/objects/pack/pack-%s.idx", 
		path, sha1_to_hex(target->sha1));
	copy_file(filename, sha1_pack_index_name(target->sha1),
		  sha1_to_hex(target->sha1), 1);
	install_packed_git(target);
	return 0;
}

static int fetch_file(const unsigned char *sha1)
{
	static int object_name_start = -1;
	static char filename[PATH_MAX];
	char *hex = sha1_to_hex(sha1);
	char *dest_filename = sha1_file_name(sha1);

 	if (object_name_start < 0) {
		strcpy(filename, path); /* e.g. git.git */
		strcat(filename, "/objects/");
		object_name_start = strlen(filename);
	}
	filename[object_name_start+0] = hex[0];
	filename[object_name_start+1] = hex[1];
	filename[object_name_start+2] = '/';
	strcpy(filename + object_name_start + 3, hex + 2);
	return copy_file(filename, dest_filename, hex, 0);
}

int fetch(unsigned char *sha1)
{
	if (has_sha1_file(sha1))
		return 0;
	else
		return fetch_file(sha1) && fetch_pack(sha1);
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
"git-local-fetch [-c] [-t] [-a] [-v] [-w filename] [--recover] [-l] [-s] [-n] commit-id path";

/*
 * By default we only use file copy.
 * If -l is specified, a hard link is attempted.
 * If -s is specified, then a symlink is attempted.
 * If -n is _not_ specified, then a regular file-to-file copy is done.
 */
int main(int argc, char **argv)
{
	const char *write_ref = NULL;
	char *commit_id;
	int arg = 1;

	setup_git_directory();
	git_config(git_default_config);

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
		else if (!strcmp(argv[arg], "--recover"))
			get_recover = 1;
		else
			usage(local_pull_usage);
		arg++;
	}
	if (argc < arg + 2)
		usage(local_pull_usage);
	commit_id = argv[arg];
	path = argv[arg + 1];

	if (pull(commit_id, write_ref, path))
		return 1;

	return 0;
}
