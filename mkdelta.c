/*
 * Deltafication of a GIT database.
 *
 * (C) 2005 Nicolas Pitre <nico@cam.org>
 *
 * This code is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "cache.h"
#include "delta.h"

static int replace_object(char *buf, unsigned long size, unsigned char *sha1)
{
	char tmpfile[PATH_MAX];
	int fd;

	snprintf(tmpfile, sizeof(tmpfile), "%s/obj_XXXXXX", get_object_directory());
	fd = mkstemp(tmpfile);
	if (fd < 0)
		return error("%s: %s\n", tmpfile, strerror(errno));
	if (write(fd, buf, size) != size) {
		perror("unable to write file");
		close(fd);
		unlink(tmpfile);
		return -1;
	}
	fchmod(fd, 0444);
	close(fd);
	if (rename(tmpfile, sha1_file_name(sha1))) {
		perror("unable to replace original object");
		unlink(tmpfile);
		return -1;
	}
	return 0;
}

static void *create_object(char *buf, unsigned long len, char *hdr, int hdrlen,
			   unsigned long *retsize)
{
	char *compressed;
	unsigned long size;
	z_stream stream;

	/* Set it up */
	memset(&stream, 0, sizeof(stream));
	deflateInit(&stream, Z_BEST_COMPRESSION);
	size = deflateBound(&stream, len+hdrlen);
	compressed = xmalloc(size);

	/* Compress it */
	stream.next_out = compressed;
	stream.avail_out = size;

	/* First header.. */
	stream.next_in = hdr;
	stream.avail_in = hdrlen;
	while (deflate(&stream, 0) == Z_OK)
		/* nothing */;

	/* Then the data itself.. */
	stream.next_in = buf;
	stream.avail_in = len;
	while (deflate(&stream, Z_FINISH) == Z_OK)
		/* nothing */;
	deflateEnd(&stream);
	*retsize = stream.total_out;
	return compressed;
}

static int restore_original_object(char *buf, unsigned long len,
				   char *type, unsigned char *sha1)
{
	char hdr[50];
	int hdrlen, ret;
	void *compressed;
	unsigned long size;

	hdrlen = sprintf(hdr, "%s %lu", type, len)+1;
	compressed = create_object(buf, len, hdr, hdrlen, &size);
	ret = replace_object(compressed, size, sha1);
	free(compressed);
	return ret;
}

static void *create_delta_object(char *buf, unsigned long len,
				 unsigned char *sha1_ref, unsigned long *size)
{
	char hdr[50];
	int hdrlen;

	/* Generate the header + sha1 of reference for delta */
	hdrlen = sprintf(hdr, "delta %lu", len+20)+1;
	memcpy(hdr + hdrlen, sha1_ref, 20);
	hdrlen += 20;

	return create_object(buf, len, hdr, hdrlen, size);
}

static void *get_buffer(unsigned char *sha1, char *type,
			unsigned long *size, unsigned long *compsize)
{
	unsigned long mapsize;
	void *map = map_sha1_file(sha1, &mapsize);
	if (map) {
		void *buffer = unpack_sha1_file(map, mapsize, type, size);
		munmap(map, mapsize);
		if (compsize)
			*compsize = mapsize;
		if (buffer)
			return buffer;
	}
	error("unable to get object %s", sha1_to_hex(sha1));
	return NULL;
}

static void *expand_delta(void *delta, unsigned long *size, char *type,
			  unsigned int *depth, unsigned char **links)
{
	void *buf = NULL;
	unsigned int level = (*depth)++;
	if (*size < 20) {
		error("delta object is bad");
		free(delta);
	} else {
		unsigned long ref_size;
		void *ref = get_buffer(delta, type, &ref_size, NULL);
		if (ref && !strcmp(type, "delta"))
			ref = expand_delta(ref, &ref_size, type, depth, links);
		else if (ref)
{
			*links = xmalloc(*depth * 20);
}
		if (ref) {
			buf = patch_delta(ref, ref_size, delta+20, *size-20, size);
			free(ref);
			if (buf)
				memcpy(*links + level*20, delta, 20);
			else
				free(*links);
		}
		free(delta);
	}
	return buf;
}

static char *mkdelta_usage =
"mkdelta [--max-depth=N] [--max-behind=N] <reference_sha1> <target_sha1> [<next_sha1> ...]";

struct delta {
	unsigned char sha1[20];		/* object sha1 */
	unsigned long size;		/* object size */
	void *buf;			/* object content */
	unsigned char *links;		/* delta reference links */
	unsigned int depth;		/* delta depth */
};
	
int main(int argc, char **argv)
{
	struct delta *ref, trg;
	char ref_type[20], trg_type[20], *skip_reason;
	void *best_buf;
	unsigned long best_size, orig_size, orig_compsize;
	unsigned int r, orig_ref, best_ref, nb_refs, next_ref, max_refs = 0;
	unsigned int i, duplicate, skip_lvl, verbose = 0, quiet = 0;
	unsigned int max_depth = -1;

	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "-v")) {
			verbose = 1;
			quiet = 0;
		} else if (!strcmp(argv[i], "-q")) {
			quiet = 1;
			verbose = 0;
		} else if (!strcmp(argv[i], "-d") && i+1 < argc) {
			max_depth = atoi(argv[++i]);
		} else if (!strncmp(argv[i], "--max-depth=", 12)) {
			max_depth = atoi(argv[i]+12);
		} else if (!strcmp(argv[i], "-b") && i+1 < argc) {
			max_refs = atoi(argv[++i]);
		} else if (!strncmp(argv[i], "--max-behind=", 13)) {
			max_refs = atoi(argv[i]+13);
		} else
			break;
	}

	if (i + (max_depth != 0) >= argc)
		usage(mkdelta_usage);

	if (!max_refs || max_refs > argc - i)
		max_refs = argc - i;
	ref = xmalloc(max_refs * sizeof(*ref));
	for (r = 0; r < max_refs; r++)
		ref[r].buf = ref[r].links = NULL;
	next_ref = nb_refs = 0;

	do {
		if (get_sha1(argv[i], trg.sha1))
			die("bad sha1 %s", argv[i]);
		trg.buf = get_buffer(trg.sha1, trg_type, &trg.size, &orig_compsize);
		if (trg.buf && !trg.size) {
			if (verbose)
				printf("skip    %s (object is empty)\n", argv[i]);
			continue;
		}
		orig_size = trg.size;
		orig_ref = -1;
		trg.depth = 0;
		trg.links = NULL;
		if (trg.buf && !strcmp(trg_type, "delta")) {
			for (r = 0; r < nb_refs; r++)
				if (!memcmp(trg.buf, ref[r].sha1, 20))
					break;
			if (r < nb_refs) {
				/* no need to reload the reference object */
				trg.depth = ref[r].depth + 1;
				trg.links = xmalloc(trg.depth*20);
				memcpy(trg.links, trg.buf, 20);
				memcpy(trg.links+20, ref[r].links, ref[r].depth*20);
				trg.buf = patch_delta(ref[r].buf, ref[r].size,
						      trg.buf+20, trg.size-20,
						      &trg.size);
				strcpy(trg_type, ref_type);
				orig_ref = r;
			} else {
				trg.buf = expand_delta(trg.buf, &trg.size, trg_type,
						       &trg.depth, &trg.links);
			}
		}
		if (!trg.buf)
			die("unable to read target object %s", argv[i]);

		if (!nb_refs) {
			strcpy(ref_type, trg_type);
		} else if (max_depth && strcmp(ref_type, trg_type)) {
			die("type mismatch for object %s", argv[i]);
		}

		duplicate = 0;
		best_buf = NULL;
		best_size = -1;
		best_ref = -1;
		skip_lvl = 0;
		skip_reason = NULL;
		for (r = 0; max_depth && r < nb_refs; r++) {
			void *delta_buf, *comp_buf;
			unsigned long delta_size, comp_size;
			unsigned int l;

			duplicate = !memcmp(trg.sha1, ref[r].sha1, 20);
			if (duplicate) {
				skip_reason = "already seen";
				break;
			}
			if (ref[r].depth >= max_depth) {
				if (skip_lvl < 1) {
					skip_reason = "exceeding max link depth";
					skip_lvl = 1;
				}
				continue;
			}
			for (l = 0; l < ref[r].depth; l++)
				if (!memcmp(trg.sha1, ref[r].links + l*20, 20))
					break;
			if (l != ref[r].depth) {
				if (skip_lvl < 2) {
					skip_reason = "would create a loop";
					skip_lvl = 2;
				}
				continue;
			}
			if (trg.depth < max_depth && r == orig_ref) {
				if (skip_lvl < 3) {
					skip_reason = "delta already in place";
					skip_lvl = 3;
				}
				continue;
			}
			delta_buf = diff_delta(ref[r].buf, ref[r].size,
					       trg.buf, trg.size, &delta_size);
			if (!delta_buf)
				die("out of memory");
			if (trg.depth < max_depth &&
			    delta_size+20 >= orig_size) {
				/* no need to even try to compress if original
				   object is smaller than this delta */
				free(delta_buf);
				if (skip_lvl < 4) {
					skip_reason = "no size reduction";
					skip_lvl = 4;
				}
				continue;
			}
			comp_buf = create_delta_object(delta_buf, delta_size,
						       ref[r].sha1, &comp_size);
			if (!comp_buf)
				die("out of memory");
			free(delta_buf);
			if (trg.depth < max_depth &&
			    comp_size >= orig_compsize) {
				free(comp_buf);
				if (skip_lvl < 5) {
					skip_reason = "no size reduction";
					skip_lvl = 5;
				}
				continue;
			}
			if ((comp_size < best_size) ||
			    (comp_size == best_size &&
			     ref[r].depth < ref[best_ref].depth)) {
				free(best_buf);
				best_buf = comp_buf;
				best_size = comp_size;
				best_ref = r;
			}
		}

		if (best_buf) {
			if (replace_object(best_buf, best_size, trg.sha1))
				die("unable to write delta for %s", argv[i]);
			free(best_buf);
			free(trg.links);
			trg.depth = ref[best_ref].depth + 1;
			trg.links = xmalloc(trg.depth*20);
			memcpy(trg.links, ref[best_ref].sha1, 20);
			memcpy(trg.links+20, ref[best_ref].links, ref[best_ref].depth*20);
			if (!quiet)
				printf("delta   %s (size=%ld.%02ld%% depth=%d dist=%d)\n",
				       argv[i], best_size*100 / orig_compsize,
				       (best_size*10000 / orig_compsize)%100,
				       trg.depth,
				       (next_ref - best_ref + max_refs)
				       % (max_refs + 1) + 1);
		} else if (trg.depth > max_depth) {
			if (restore_original_object(trg.buf, trg.size, trg_type, trg.sha1))
				die("unable to restore %s", argv[i]);
			if (!quiet)
				printf("undelta %s (depth was %d)\n",
				       argv[i], trg.depth);
			trg.depth = 0;
			free(trg.links);
			trg.links = NULL;
		} else if (skip_reason && verbose) {
			printf("skip    %s (%s)\n", argv[i], skip_reason);
		}

		if (!duplicate) {
			free(ref[next_ref].buf);
			free(ref[next_ref].links);
			ref[next_ref] = trg;
			if (++next_ref > nb_refs)
				nb_refs = next_ref;
			if (next_ref == max_refs)
				next_ref = 0;
		} else {
			free(trg.buf);
			free(trg.links);
		}
	} while (++i < argc);

	return 0;
}
