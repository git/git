/*
 * GIT - The information manager from hell
 *
 * Copyright (C) Linus Torvalds, 2005
 */
#include "cache.h"

static int check_valid_sha1(unsigned char *sha1)
{
	char *filename = sha1_file_name(sha1);
	int ret;

	/* If we were anal, we'd check that the sha1 of the contents actually matches */
	ret = access(filename, R_OK);
	if (ret)
		perror(filename);
	return ret;
}

static int prepend_integer(char *buffer, unsigned val, int i)
{
	buffer[--i] = '\0';
	do {
		buffer[--i] = '0' + (val % 10);
		val /= 10;
	} while (val);
	return i;
}

#define ORIG_OFFSET (40)	/* Enough space to add the header of "tree <size>\0" */

static int write_tree(struct cache_entry **cachep, int maxentries, const char *base, int baselen, unsigned char *returnsha1)
{
	unsigned char subdir_sha1[20];
	unsigned long size, offset;
	char *buffer;
	int i, nr;

	/* Guess at some random initial size */
	size = 8192;
	buffer = malloc(size);
	offset = ORIG_OFFSET;

	nr = 0;
	do {
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

		if (check_valid_sha1(sha1) < 0)
			exit(1);

		entrylen = pathlen - baselen;
		if (offset + entrylen + 100 > size) {
			size = alloc_nr(offset + entrylen + 100);
			buffer = realloc(buffer, size);
		}
		offset += sprintf(buffer + offset, "%o %.*s", mode, entrylen, filename);
		buffer[offset++] = 0;
		memcpy(buffer + offset, sha1, 20);
		offset += 20;
		nr++;
	} while (nr < maxentries);

	i = prepend_integer(buffer, offset - ORIG_OFFSET, ORIG_OFFSET);
	i -= 5;
	memcpy(buffer+i, "tree ", 5);

	write_sha1_file(buffer + i, offset - i, returnsha1);
	free(buffer);
	return nr;
}

int main(int argc, char **argv)
{
	int i, unmerged;
	int entries = read_cache();
	unsigned char sha1[20];

	if (entries <= 0)
		die("write-tree: no cache contents to write");

	/* Verify that the tree is merged */
	unmerged = 0;
	for (i = 0; i < entries; i++) {
		struct cache_entry *ce = active_cache[i];
		if (ntohs(ce->ce_flags) & ~CE_NAMEMASK) {
			if (++unmerged > 10) {
				fprintf(stderr, "...\n");
				break;
			}
			fprintf(stderr, "%s: unmerged (%s)\n", ce->name, sha1_to_hex(ce->sha1));
		}
	}
	if (unmerged)
		die("write-tree: not able to write tree");

	/* Ok, write it out */
	if (write_tree(active_cache, entries, "", 0, sha1) != entries)
		die("write-tree: internal error");
	printf("%s\n", sha1_to_hex(sha1));
	return 0;
}
