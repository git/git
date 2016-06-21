/*
 * GIT - The information manager from hell
 *
 * Copyright (C) Linus Torvalds, 2005
 */
#include "cache.h"

static int missing_ok = 0;

static int check_valid_sha1(unsigned char *sha1)
{
	int ret;

	/* If we were anal, we'd check that the sha1 of the contents actually matches */
	ret = has_sha1_file(sha1);
	if (ret == 0)
		perror(sha1_file_name(sha1));
	return ret ? 0 : -1;
}

static int write_tree(struct cache_entry **cachep, int maxentries, const char *base, int baselen, unsigned char *returnsha1)
{
	unsigned char subdir_sha1[20];
	unsigned long size, offset;
	char *buffer;
	int nr;

	/* Guess at some random initial size */
	size = 8192;
	buffer = xmalloc(size);
	offset = 0;

	nr = 0;
	while (nr < maxentries) {
		struct cache_entry *ce = cachep[nr];
		const char *pathname = ce->name, *filename, *dirname;
		int pathlen = ce_namelen(ce), entrylen;
		unsigned char *sha1;
		unsigned int mode;

		/* Did we hit the end of the directory? Return how many we wrote */
		if (baselen >= pathlen || memcmp(base, pathname, baselen))
			break;

		sha1 = ce->sha1;
		mode = ntohl(ce->ce_mode);

		/* Do we have _further_ subdirectories? */
		filename = pathname + baselen;
		dirname = strchr(filename, '/');
		if (dirname) {
			int subdir_written;

			subdir_written = write_tree(cachep + nr, maxentries - nr, pathname, dirname-pathname+1, subdir_sha1);
			nr += subdir_written;

			/* Now we need to write out the directory entry into this tree.. */
			mode = S_IFDIR;
			pathlen = dirname - pathname;

			/* ..but the directory entry doesn't count towards the total count */
			nr--;
			sha1 = subdir_sha1;
		}

		if (!missing_ok && check_valid_sha1(sha1) < 0)
			exit(1);

		entrylen = pathlen - baselen;
		if (offset + entrylen + 100 > size) {
			size = alloc_nr(offset + entrylen + 100);
			buffer = xrealloc(buffer, size);
		}
		offset += sprintf(buffer + offset, "%o %.*s", mode, entrylen, filename);
		buffer[offset++] = 0;
		memcpy(buffer + offset, sha1, 20);
		offset += 20;
		nr++;
	}

	write_sha1_file(buffer, offset, "tree", returnsha1);
	free(buffer);
	return nr;
}

int main(int argc, char **argv)
{
	int i, funny;
	int entries = read_cache();
	unsigned char sha1[20];
	
	if (argc == 2) {
		if (!strcmp(argv[1], "--missing-ok"))
			missing_ok = 1;
		else
			die("unknown option %s", argv[1]);
	}
	
	if (argc > 2)
		die("too many options");

	if (entries < 0)
		die("git-write-tree: error reading cache");

	/* Verify that the tree is merged */
	funny = 0;
	for (i = 0; i < entries; i++) {
		struct cache_entry *ce = active_cache[i];
		if (ntohs(ce->ce_flags) & ~CE_NAMEMASK) {
			if (10 < ++funny) {
				fprintf(stderr, "...\n");
				break;
			}
			fprintf(stderr, "%s: unmerged (%s)\n", ce->name, sha1_to_hex(ce->sha1));
		}
	}
	if (funny)
		die("git-write-tree: not able to write tree");

	/* Also verify that the cache does not have path and path/file
	 * at the same time.  At this point we know the cache has only
	 * stage 0 entries.
	 */
	funny = 0;
	for (i = 0; i < entries - 1; i++) {
		/* path/file always comes after path because of the way
		 * the cache is sorted.  Also path can appear only once,
		 * which means conflicting one would immediately follow.
		 */
		const char *this_name = active_cache[i]->name;
		const char *next_name = active_cache[i+1]->name;
		int this_len = strlen(this_name);
		if (this_len < strlen(next_name) &&
		    strncmp(this_name, next_name, this_len) == 0 &&
		    next_name[this_len] == '/') {
			if (10 < ++funny) {
				fprintf(stderr, "...\n");
				break;
			}
			fprintf(stderr, "You have both %s and %s\n",
				this_name, next_name);
		}
	}
	if (funny)
		die("git-write-tree: not able to write tree");

	/* Ok, write it out */
	if (write_tree(active_cache, entries, "", 0, sha1) != entries)
		die("git-write-tree: internal error");
	printf("%s\n", sha1_to_hex(sha1));
	return 0;
}
