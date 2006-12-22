/*
 * Builtin "git count-objects".
 *
 * Copyright (c) 2006 Junio C Hamano
 */

#include "cache.h"
#include "builtin.h"

static const char count_objects_usage[] = "git-count-objects [-v]";

static void count_objects(DIR *d, char *path, int len, int verbose,
			  unsigned long *loose,
			  unsigned long *loose_size,
			  unsigned long *packed_loose,
			  unsigned long *garbage)
{
	struct dirent *ent;
	while ((ent = readdir(d)) != NULL) {
		char hex[41];
		unsigned char sha1[20];
		const char *cp;
		int bad = 0;

		if ((ent->d_name[0] == '.') &&
		    (ent->d_name[1] == 0 ||
		     ((ent->d_name[1] == '.') && (ent->d_name[2] == 0))))
			continue;
		for (cp = ent->d_name; *cp; cp++) {
			int ch = *cp;
			if (('0' <= ch && ch <= '9') ||
			    ('a' <= ch && ch <= 'f'))
				continue;
			bad = 1;
			break;
		}
		if (cp - ent->d_name != 38)
			bad = 1;
		else {
			struct stat st;
			memcpy(path + len + 3, ent->d_name, 38);
			path[len + 2] = '/';
			path[len + 41] = 0;
			if (lstat(path, &st) || !S_ISREG(st.st_mode))
				bad = 1;
			else
#ifndef NO_ST_BLOCKS
				(*loose_size) += st.st_blocks;
#else
				(*loose_size) += (st.st_size+511)/512;
#endif
		}
		if (bad) {
			if (verbose) {
				error("garbage found: %.*s/%s",
				      len + 2, path, ent->d_name);
				(*garbage)++;
			}
			continue;
		}
		(*loose)++;
		if (!verbose)
			continue;
		memcpy(hex, path+len, 2);
		memcpy(hex+2, ent->d_name, 38);
		hex[40] = 0;
		if (get_sha1_hex(hex, sha1))
			die("internal error");
		if (has_sha1_pack(sha1, NULL))
			(*packed_loose)++;
	}
}

int cmd_count_objects(int ac, const char **av, const char *prefix)
{
	int i;
	int verbose = 0;
	const char *objdir = get_object_directory();
	int len = strlen(objdir);
	char *path = xmalloc(len + 50);
	unsigned long loose = 0, packed = 0, packed_loose = 0, garbage = 0;
	unsigned long loose_size = 0;

	for (i = 1; i < ac; i++) {
		const char *arg = av[i];
		if (*arg != '-')
			break;
		else if (!strcmp(arg, "-v"))
			verbose = 1;
		else
			usage(count_objects_usage);
	}

	/* we do not take arguments other than flags for now */
	if (i < ac)
		usage(count_objects_usage);
	memcpy(path, objdir, len);
	if (len && objdir[len-1] != '/')
		path[len++] = '/';
	for (i = 0; i < 256; i++) {
		DIR *d;
		sprintf(path + len, "%02x", i);
		d = opendir(path);
		if (!d)
			continue;
		count_objects(d, path, len, verbose,
			      &loose, &loose_size, &packed_loose, &garbage);
		closedir(d);
	}
	if (verbose) {
		struct packed_git *p;
		unsigned long num_pack = 0;
		if (!packed_git)
			prepare_packed_git();
		for (p = packed_git; p; p = p->next) {
			if (!p->pack_local)
				continue;
			packed += num_packed_objects(p);
			num_pack++;
		}
		printf("count: %lu\n", loose);
		printf("size: %lu\n", loose_size / 2);
		printf("in-pack: %lu\n", packed);
		printf("packs: %lu\n", num_pack);
		printf("prune-packable: %lu\n", packed_loose);
		printf("garbage: %lu\n", garbage);
	}
	else
		printf("%lu objects, %lu kilobytes\n",
		       loose, loose_size / 2);
	return 0;
}
