/*
 * Builtin "git count-objects".
 *
 * Copyright (c) 2006 Junio C Hamano
 */

#include "cache.h"
#include "dir.h"
#include "builtin.h"
#include "parse-options.h"

static void count_objects(DIR *d, char *path, int len, int verbose,
			  unsigned long *loose,
			  off_t *loose_size,
			  unsigned long *packed_loose,
			  unsigned long *garbage)
{
	struct dirent *ent;
	while ((ent = readdir(d)) != NULL) {
		char hex[41];
		unsigned char sha1[20];
		const char *cp;
		int bad = 0;

		if (is_dot_or_dotdot(ent->d_name))
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
				(*loose_size) += xsize_t(on_disk_bytes(st));
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
		if (has_sha1_pack(sha1))
			(*packed_loose)++;
	}
}

static char const * const count_objects_usage[] = {
	"git count-objects [-v]",
	NULL
};

int cmd_count_objects(int argc, const char **argv, const char *prefix)
{
	int i, verbose = 0;
	const char *objdir = get_object_directory();
	int len = strlen(objdir);
	char *path = xmalloc(len + 50);
	unsigned long loose = 0, packed = 0, packed_loose = 0, garbage = 0;
	off_t loose_size = 0;
	struct option opts[] = {
		OPT__VERBOSE(&verbose, "be verbose"),
		OPT_END(),
	};

	argc = parse_options(argc, argv, prefix, opts, count_objects_usage, 0);
	/* we do not take arguments other than flags for now */
	if (argc)
		usage_with_options(count_objects_usage, opts);
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
		off_t size_pack = 0;
		if (!packed_git)
			prepare_packed_git();
		for (p = packed_git; p; p = p->next) {
			if (!p->pack_local)
				continue;
			if (open_pack_index(p))
				continue;
			packed += p->num_objects;
			size_pack += p->pack_size + p->index_size;
			num_pack++;
		}
		printf("count: %lu\n", loose);
		printf("size: %lu\n", (unsigned long) (loose_size / 1024));
		printf("in-pack: %lu\n", packed);
		printf("packs: %lu\n", num_pack);
		printf("size-pack: %lu\n", (unsigned long) (size_pack / 1024));
		printf("prune-packable: %lu\n", packed_loose);
		printf("garbage: %lu\n", garbage);
	}
	else
		printf("%lu objects, %lu kilobytes\n",
		       loose, (unsigned long) (loose_size / 1024));
	return 0;
}
