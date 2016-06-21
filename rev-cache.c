#include "refs.h"
#include "cache.h"
#include "rev-cache.h"

struct rev_cache **rev_cache;
int nr_revs, alloc_revs;

static struct rev_list_elem *rle_free;

#define BATCH_SIZE 512

int find_rev_cache(const unsigned char *sha1)
{
	int lo = 0, hi = nr_revs;
	while (lo < hi) {
		int mi = (lo + hi) / 2;
		struct rev_cache *ri = rev_cache[mi];
		int cmp = memcmp(sha1, ri->sha1, 20);
		if (!cmp)
			return mi;
		if (cmp < 0)
			hi = mi;
		else
			lo = mi + 1;
	}
	return -lo - 1;
}

static struct rev_list_elem *alloc_list_elem(void)
{
	struct rev_list_elem *rle;
	if (!rle_free) {
		int i;

		rle = xmalloc(sizeof(*rle) * BATCH_SIZE);
		for (i = 0; i < BATCH_SIZE - 1; i++) {
			rle[i].ri = NULL;
			rle[i].next = &rle[i + 1];
		}
		rle[BATCH_SIZE - 1].ri = NULL;
		rle[BATCH_SIZE - 1].next = NULL;
		rle_free = rle;
	}
	rle = rle_free;
	rle_free = rle->next;
	return rle;
}

static struct rev_cache *create_rev_cache(const unsigned char *sha1)
{
	struct rev_cache *ri;
	int pos = find_rev_cache(sha1);

	if (0 <= pos)
		return rev_cache[pos];
	pos = -pos - 1;
	if (alloc_revs <= ++nr_revs) {
		alloc_revs = alloc_nr(alloc_revs);
		rev_cache = xrealloc(rev_cache, sizeof(ri) * alloc_revs);
	}
	if (pos < nr_revs)
		memmove(rev_cache + pos + 1, rev_cache + pos,
			(nr_revs - pos - 1) * sizeof(ri));
	ri = xcalloc(1, sizeof(*ri));
	memcpy(ri->sha1, sha1, 20);
	rev_cache[pos] = ri;
	return ri;
}

static unsigned char last_sha1[20];

static void write_one_rev_cache(FILE *rev_cache_file, struct rev_cache *ri)
{
	unsigned char flag;
	struct rev_list_elem *rle;

	if (ri->written)
		return;

	if (ri->parsed) {
		/* We use last_sha1 compression only for the first parent;
		 * otherwise the resulting rev-cache would lose the parent
		 * order information.
		 */
		if (ri->parents &&
		    !memcmp(ri->parents->ri->sha1, last_sha1, 20))
			flag = (ri->num_parents - 1) | 0x80;
		else
			flag = ri->num_parents;

		fwrite(ri->sha1, 20, 1, rev_cache_file);
		fwrite(&flag, 1, 1, rev_cache_file);
		for (rle = ri->parents; rle; rle = rle->next) {
			if (flag & 0x80 && rle == ri->parents)
				continue;
			fwrite(rle->ri->sha1, 20, 1, rev_cache_file);
		}
		memcpy(last_sha1, ri->sha1, 20);
		ri->written = 1;
	}
	/* recursively write children depth first */
	for (rle = ri->children; rle; rle = rle->next)
		write_one_rev_cache(rev_cache_file, rle->ri);
}

void write_rev_cache(const char *newpath, const char *oldpath)
{
	/* write the following commit ancestry information in
	 * $GIT_DIR/info/rev-cache.
	 *
	 * The format is:
	 * 20-byte SHA1 (commit ID)
	 * 1-byte flag:
	 * - bit 0-6 records "number of parent commit SHA1s to
	 *   follow" (i.e. up to 127 children can be listed).
	 * - when the bit 7 is on, then "the entry immediately
	 *   before this entry is one of the parents of this
         *   commit".
	 * N x 20-byte SHA1 (parent commit IDs)
	 */
	FILE *rev_cache_file;
	int i;
	struct rev_cache *ri;

	if (!strcmp(newpath, oldpath)) {
		/* If we are doing it in place */
		rev_cache_file = fopen(newpath, "a");
	}
	else {
		char buf[8096];
		size_t sz;
		FILE *oldfp = fopen(oldpath, "r");
		rev_cache_file = fopen(newpath, "w");
		if (oldfp) {
			while (1) {
				sz = fread(buf, 1, sizeof(buf), oldfp);
				if (sz == 0)
					break;
				fwrite(buf, 1, sz, rev_cache_file);
			}
			fclose(oldfp);
		}
	}

	memset(last_sha1, 0, 20);

	/* Go through available rev_cache structures, starting from
	 * parentless ones first, so that we would get most out of
	 * last_sha1 optimization by the depth first behaviour of
	 * write_one_rev_cache().
	 */
	for (i = 0; i < nr_revs; i++) {
		ri = rev_cache[i];
		if (ri->num_parents)
			continue;
		write_one_rev_cache(rev_cache_file, ri);
	}
	/* Then the rest */
	for (i = 0; i < nr_revs; i++) {
		ri = rev_cache[i];
		write_one_rev_cache(rev_cache_file, ri);
	}
	fclose(rev_cache_file);
}

static void add_parent(struct rev_cache *child,
		       const unsigned char *parent_sha1)
{
	struct rev_cache *parent = create_rev_cache(parent_sha1);
	struct rev_list_elem *e = alloc_list_elem();

	/* Keep the parent list ordered in the same way the commit
	 * object records them.
	 */
	e->ri = parent;
	e->next = NULL;
	if (!child->parents_tail)
		child->parents = e;
	else
		child->parents_tail->next = e;
	child->parents_tail = e;
	child->num_parents++;

	/* There is no inherent order of the children so we just
	 * LIFO them together.
	 */
	e = alloc_list_elem();
	e->next = parent->children;
	parent->children = e;
	e->ri = child;
	parent->num_children++;
}

int read_rev_cache(const char *path, FILE *dumpfile, int dry_run)
{
	unsigned char *map;
	int fd;
	struct stat st;
	unsigned long ofs, len;
	struct rev_cache *ri = NULL;

	fd = open(path, O_RDONLY);
	if (fd < 0) {
		if (dry_run)
			return error("cannot open %s", path);
		if (errno == ENOENT)
			return 0;
		return -1;
	}
	if (fstat(fd, &st)) {
		close(fd);
		return -1;
	}
	map = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
	close(fd);
	if (map == MAP_FAILED)
		return -1;

	memset(last_sha1, 0, 20);
	ofs = 0;
	len = st.st_size;
	while (ofs < len) {
		unsigned char sha1[20];
		int flag, cnt, i;
		if (len < ofs + 21)
			die("rev-cache too short");
		memcpy(sha1, map + ofs, 20);
		flag = map[ofs + 20];
		ofs += 21;
		cnt = (flag & 0x7f) + ((flag & 0x80) != 0);
		if (len < ofs + (flag & 0x7f) * 20)
			die("rev-cache too short to have %d more parents",
			    (flag & 0x7f));
		if (dumpfile)
			fprintf(dumpfile, "%s", sha1_to_hex(sha1));
		if (!dry_run) {
			ri = create_rev_cache(sha1);
			if (!ri)
				die("cannot create rev-cache for %s",
				    sha1_to_hex(sha1));
			ri->written = ri->parsed = 1;
		}
		i = 0;
		if (flag & 0x80) {
			if (!dry_run)
				add_parent(ri, last_sha1);
			if (dumpfile)
				fprintf(dumpfile, " %s",
					sha1_to_hex(last_sha1));
			i++;
		}
		while (i++ < cnt) {
			if (!dry_run)
				add_parent(ri, map + ofs);
			if (dumpfile)
				fprintf(dumpfile, " %s",
					sha1_to_hex(last_sha1));
			ofs += 20;
		}
		if (dumpfile)
			fprintf(dumpfile, "\n");
		memcpy(last_sha1, sha1, 20);
	}
	if (ofs != len)
		die("rev-cache truncated?");
	munmap(map, len);
	return 0;
}

int record_rev_cache(const unsigned char *sha1, FILE *dumpfile)
{
	unsigned char parent[20];
	char type[20];
	unsigned long size, ofs;
	unsigned int cnt, i;
	void *buf;
	struct rev_cache *ri;

	buf = read_sha1_file(sha1, type, &size);
	if (!buf)
		return error("%s: not found", sha1_to_hex(sha1));
	if (strcmp(type, "commit")) {
		free(buf);
		return error("%s: not a commit but a %s",
			     sha1_to_hex(sha1), type);
	}
	ri = create_rev_cache(sha1);
	if (ri->parsed)
		return 0;
	if (dumpfile)
		fprintf(dumpfile, "commit %s\n", sha1_to_hex(sha1));

	cnt = 0;
	ofs = 46; /* "tree " + hex-sha1 + "\n" */
	while (!memcmp(buf + ofs, "parent ", 7) &&
	       !get_sha1_hex(buf + ofs + 7, parent)) {
		ofs += 48;
		cnt++;
	}
	if (cnt * 48 + 46 != ofs) {
		free(buf);
		die("internal error in record_rev_cache");
	}

	ri = create_rev_cache(sha1);
	ri->parsed = 1;

	for (i = 0; i < cnt; i++) {
		unsigned char parent_sha1[20];

		ofs = 46 + i * 48 + 7;
		get_sha1_hex(buf + ofs, parent_sha1);
		add_parent(ri, parent_sha1);
		record_rev_cache(parent_sha1, dumpfile);
	}
	free(buf);
	return 0;
}
