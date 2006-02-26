#include "cache.h"
#include "object.h"
#include "delta.h"
#include "pack.h"
#include "csum-file.h"
#include "diff.h"
#include <sys/time.h>
#include <signal.h>

static const char pack_usage[] = "git-pack-objects [-q] [--no-reuse-delta] [--non-empty] [--local] [--incremental] [--window=N] [--depth=N] {--stdout | base-name} < object-list";

struct object_entry {
	unsigned char sha1[20];
	unsigned long size;	/* uncompressed size */
	unsigned long offset;	/* offset into the final pack file;
				 * nonzero if already written.
				 */
	unsigned int depth;	/* delta depth */
	unsigned int delta_limit;	/* base adjustment for in-pack delta */
	unsigned int hash;	/* name hint hash */
	enum object_type type;
	enum object_type in_pack_type;	/* could be delta */
	unsigned long delta_size;	/* delta data size (uncompressed) */
	struct object_entry *delta;	/* delta base object */
	struct packed_git *in_pack; 	/* already in pack */
	unsigned int in_pack_offset;
	struct object_entry *delta_child; /* delitified objects who bases me */
	struct object_entry *delta_sibling; /* other deltified objects who
					     * uses the same base as me
					     */
	int preferred_base;	/* we do not pack this, but is encouraged to
				 * be used as the base objectto delta huge
				 * objects against.
				 */
	int based_on_preferred;	/* current delta candidate is a preferred
				 * one, or delta against a preferred one.
				 */
};

/*
 * Objects we are going to pack are colected in objects array (dynamically
 * expanded).  nr_objects & nr_alloc controls this array.  They are stored
 * in the order we see -- typically rev-list --objects order that gives us
 * nice "minimum seek" order.
 *
 * sorted-by-sha ans sorted-by-type are arrays of pointers that point at
 * elements in the objects array.  The former is used to build the pack
 * index (lists object names in the ascending order to help offset lookup),
 * and the latter is used to group similar things together by try_delta()
 * heuristics.
 */

static unsigned char object_list_sha1[20];
static int non_empty = 0;
static int no_reuse_delta = 0;
static int local = 0;
static int incremental = 0;
static struct object_entry **sorted_by_sha, **sorted_by_type;
static struct object_entry *objects = NULL;
static int nr_objects = 0, nr_alloc = 0, nr_result = 0;
static const char *base_name;
static unsigned char pack_file_sha1[20];
static int progress = 1;
static volatile int progress_update = 0;

/*
 * The object names in objects array are hashed with this hashtable,
 * to help looking up the entry by object name.  Binary search from
 * sorted_by_sha is also possible but this was easier to code and faster.
 * This hashtable is built after all the objects are seen.
 */
static int *object_ix = NULL;
static int object_ix_hashsz = 0;

/*
 * Pack index for existing packs give us easy access to the offsets into
 * corresponding pack file where each object's data starts, but the entries
 * do not store the size of the compressed representation (uncompressed
 * size is easily available by examining the pack entry header).  We build
 * a hashtable of existing packs (pack_revindex), and keep reverse index
 * here -- pack index file is sorted by object name mapping to offset; this
 * pack_revindex[].revindex array is an ordered list of offsets, so if you
 * know the offset of an object, next offset is where its packed
 * representation ends.
 */
struct pack_revindex {
	struct packed_git *p;
	unsigned long *revindex;
} *pack_revindex = NULL;
static int pack_revindex_hashsz = 0;

/*
 * stats
 */
static int written = 0;
static int written_delta = 0;
static int reused = 0;
static int reused_delta = 0;

static int pack_revindex_ix(struct packed_git *p)
{
	unsigned long ui = (unsigned long)(long)p;
	int i;

	ui = ui ^ (ui >> 16); /* defeat structure alignment */
	i = (int)(ui % pack_revindex_hashsz);
	while (pack_revindex[i].p) {
		if (pack_revindex[i].p == p)
			return i;
		if (++i == pack_revindex_hashsz)
			i = 0;
	}
	return -1 - i;
}

static void prepare_pack_ix(void)
{
	int num;
	struct packed_git *p;
	for (num = 0, p = packed_git; p; p = p->next)
		num++;
	if (!num)
		return;
	pack_revindex_hashsz = num * 11;
	pack_revindex = xcalloc(sizeof(*pack_revindex), pack_revindex_hashsz);
	for (p = packed_git; p; p = p->next) {
		num = pack_revindex_ix(p);
		num = - 1 - num;
		pack_revindex[num].p = p;
	}
	/* revindex elements are lazily initialized */
}

static int cmp_offset(const void *a_, const void *b_)
{
	unsigned long a = *(unsigned long *) a_;
	unsigned long b = *(unsigned long *) b_;
	if (a < b)
		return -1;
	else if (a == b)
		return 0;
	else
		return 1;
}

/*
 * Ordered list of offsets of objects in the pack.
 */
static void prepare_pack_revindex(struct pack_revindex *rix)
{
	struct packed_git *p = rix->p;
	int num_ent = num_packed_objects(p);
	int i;
	void *index = p->index_base + 256;

	rix->revindex = xmalloc(sizeof(unsigned long) * (num_ent + 1));
	for (i = 0; i < num_ent; i++) {
		long hl = *((long *)(index + 24 * i));
		rix->revindex[i] = ntohl(hl);
	}
	/* This knows the pack format -- the 20-byte trailer
	 * follows immediately after the last object data.
	 */
	rix->revindex[num_ent] = p->pack_size - 20;
	qsort(rix->revindex, num_ent, sizeof(unsigned long), cmp_offset);
}

static unsigned long find_packed_object_size(struct packed_git *p,
					     unsigned long ofs)
{
	int num;
	int lo, hi;
	struct pack_revindex *rix;
	unsigned long *revindex;
	num = pack_revindex_ix(p);
	if (num < 0)
		die("internal error: pack revindex uninitialized");
	rix = &pack_revindex[num];
	if (!rix->revindex)
		prepare_pack_revindex(rix);
	revindex = rix->revindex;
	lo = 0;
	hi = num_packed_objects(p) + 1;
	do {
		int mi = (lo + hi) / 2;
		if (revindex[mi] == ofs) {
			return revindex[mi+1] - ofs;
		}
		else if (ofs < revindex[mi])
			hi = mi;
		else
			lo = mi + 1;
	} while (lo < hi);
	die("internal error: pack revindex corrupt");
}

static void *delta_against(void *buf, unsigned long size, struct object_entry *entry)
{
	unsigned long othersize, delta_size;
	char type[10];
	void *otherbuf = read_sha1_file(entry->delta->sha1, type, &othersize);
	void *delta_buf;

	if (!otherbuf)
		die("unable to read %s", sha1_to_hex(entry->delta->sha1));
        delta_buf = diff_delta(otherbuf, othersize,
			       buf, size, &delta_size, 0);
        if (!delta_buf || delta_size != entry->delta_size)
        	die("delta size changed");
        free(buf);
        free(otherbuf);
	return delta_buf;
}

/*
 * The per-object header is a pretty dense thing, which is
 *  - first byte: low four bits are "size", then three bits of "type",
 *    and the high bit is "size continues".
 *  - each byte afterwards: low seven bits are size continuation,
 *    with the high bit being "size continues"
 */
static int encode_header(enum object_type type, unsigned long size, unsigned char *hdr)
{
	int n = 1;
	unsigned char c;

	if (type < OBJ_COMMIT || type > OBJ_DELTA)
		die("bad type %d", type);

	c = (type << 4) | (size & 15);
	size >>= 4;
	while (size) {
		*hdr++ = c | 0x80;
		c = size & 0x7f;
		size >>= 7;
		n++;
	}
	*hdr = c;
	return n;
}

static unsigned long write_object(struct sha1file *f,
				  struct object_entry *entry)
{
	unsigned long size;
	char type[10];
	void *buf;
	unsigned char header[10];
	unsigned hdrlen, datalen;
	enum object_type obj_type;
	int to_reuse = 0;

	if (entry->preferred_base)
		return 0;

	obj_type = entry->type;
	if (! entry->in_pack)
		to_reuse = 0;	/* can't reuse what we don't have */
	else if (obj_type == OBJ_DELTA)
		to_reuse = 1;	/* check_object() decided it for us */
	else if (obj_type != entry->in_pack_type)
		to_reuse = 0;	/* pack has delta which is unusable */
	else if (entry->delta)
		to_reuse = 0;	/* we want to pack afresh */
	else
		to_reuse = 1;	/* we have it in-pack undeltified,
				 * and we do not need to deltify it.
				 */

	if (! to_reuse) {
		buf = read_sha1_file(entry->sha1, type, &size);
		if (!buf)
			die("unable to read %s", sha1_to_hex(entry->sha1));
		if (size != entry->size)
			die("object %s size inconsistency (%lu vs %lu)",
			    sha1_to_hex(entry->sha1), size, entry->size);
		if (entry->delta) {
			buf = delta_against(buf, size, entry);
			size = entry->delta_size;
			obj_type = OBJ_DELTA;
		}
		/*
		 * The object header is a byte of 'type' followed by zero or
		 * more bytes of length.  For deltas, the 20 bytes of delta
		 * sha1 follows that.
		 */
		hdrlen = encode_header(obj_type, size, header);
		sha1write(f, header, hdrlen);

		if (entry->delta) {
			sha1write(f, entry->delta, 20);
			hdrlen += 20;
		}
		datalen = sha1write_compressed(f, buf, size);
		free(buf);
	}
	else {
		struct packed_git *p = entry->in_pack;
		use_packed_git(p);

		datalen = find_packed_object_size(p, entry->in_pack_offset);
		buf = p->pack_base + entry->in_pack_offset;
		sha1write(f, buf, datalen);
		unuse_packed_git(p);
		hdrlen = 0; /* not really */
		if (obj_type == OBJ_DELTA)
			reused_delta++;
		reused++;
	}
	if (obj_type == OBJ_DELTA)
		written_delta++;
	written++;
	return hdrlen + datalen;
}

static unsigned long write_one(struct sha1file *f,
			       struct object_entry *e,
			       unsigned long offset)
{
	if (e->offset)
		/* offset starts from header size and cannot be zero
		 * if it is written already.
		 */
		return offset;
	e->offset = offset;
	offset += write_object(f, e);
	/* if we are deltified, write out its base object. */
	if (e->delta)
		offset = write_one(f, e->delta, offset);
	return offset;
}

static void write_pack_file(void)
{
	int i;
	struct sha1file *f;
	unsigned long offset;
	struct pack_header hdr;
	unsigned last_percent = 999;
	int do_progress = 0;

	if (!base_name)
		f = sha1fd(1, "<stdout>");
	else {
		f = sha1create("%s-%s.%s", base_name,
			       sha1_to_hex(object_list_sha1), "pack");
		do_progress = progress;
	}
	if (do_progress)
		fprintf(stderr, "Writing %d objects.\n", nr_result);

	hdr.hdr_signature = htonl(PACK_SIGNATURE);
	hdr.hdr_version = htonl(PACK_VERSION);
	hdr.hdr_entries = htonl(nr_result);
	sha1write(f, &hdr, sizeof(hdr));
	offset = sizeof(hdr);
	if (!nr_result)
		goto done;
	for (i = 0; i < nr_objects; i++) {
		offset = write_one(f, objects + i, offset);
		if (do_progress) {
			unsigned percent = written * 100 / nr_result;
			if (progress_update || percent != last_percent) {
				fprintf(stderr, "%4u%% (%u/%u) done\r",
					percent, written, nr_result);
				progress_update = 0;
				last_percent = percent;
			}
		}
	}
	if (do_progress)
		fputc('\n', stderr);
 done:
	sha1close(f, pack_file_sha1, 1);
}

static void write_index_file(void)
{
	int i;
	struct sha1file *f = sha1create("%s-%s.%s", base_name,
					sha1_to_hex(object_list_sha1), "idx");
	struct object_entry **list = sorted_by_sha;
	struct object_entry **last = list + nr_result;
	unsigned int array[256];

	/*
	 * Write the first-level table (the list is sorted,
	 * but we use a 256-entry lookup to be able to avoid
	 * having to do eight extra binary search iterations).
	 */
	for (i = 0; i < 256; i++) {
		struct object_entry **next = list;
		while (next < last) {
			struct object_entry *entry = *next;
			if (entry->sha1[0] != i)
				break;
			next++;
		}
		array[i] = htonl(next - sorted_by_sha);
		list = next;
	}
	sha1write(f, array, 256 * sizeof(int));

	/*
	 * Write the actual SHA1 entries..
	 */
	list = sorted_by_sha;
	for (i = 0; i < nr_result; i++) {
		struct object_entry *entry = *list++;
		unsigned int offset = htonl(entry->offset);
		sha1write(f, &offset, 4);
		sha1write(f, entry->sha1, 20);
	}
	sha1write(f, pack_file_sha1, 20);
	sha1close(f, NULL, 1);
}

static int locate_object_entry_hash(const unsigned char *sha1)
{
	int i;
	unsigned int ui;
	memcpy(&ui, sha1, sizeof(unsigned int));
	i = ui % object_ix_hashsz;
	while (0 < object_ix[i]) {
		if (!memcmp(sha1, objects[object_ix[i]-1].sha1, 20))
			return i;
		if (++i == object_ix_hashsz)
			i = 0;
	}
	return -1 - i;
}

static struct object_entry *locate_object_entry(const unsigned char *sha1)
{
	int i;

	if (!object_ix_hashsz)
		return NULL;

	i = locate_object_entry_hash(sha1);
	if (0 <= i)
		return &objects[object_ix[i]-1];
	return NULL;
}

static void rehash_objects(void)
{
	int i;
	struct object_entry *oe;

	object_ix_hashsz = nr_objects * 3;
	if (object_ix_hashsz < 1024)
		object_ix_hashsz = 1024;
	object_ix = xrealloc(object_ix, sizeof(int) * object_ix_hashsz);
	object_ix = memset(object_ix, 0, sizeof(int) * object_ix_hashsz);
	for (i = 0, oe = objects; i < nr_objects; i++, oe++) {
		int ix = locate_object_entry_hash(oe->sha1);
		if (0 <= ix)
			continue;
		ix = -1 - ix;
		object_ix[ix] = i + 1;
	}
}

struct name_path {
	struct name_path *up;
	const char *elem;
	int len;
};

#define DIRBITS 12

static unsigned name_hash(struct name_path *path, const char *name)
{
	struct name_path *p = path;
	const char *n = name + strlen(name);
	unsigned hash = 0, name_hash = 0, name_done = 0;

	if (n != name && n[-1] == '\n')
		n--;
	while (name <= --n) {
		unsigned char c = *n;
		if (c == '/' && !name_done) {
			name_hash = hash;
			name_done = 1;
			hash = 0;
		}
		hash = hash * 11 + c;
	}
	if (!name_done) {
		name_hash = hash;
		hash = 0;
	}
	for (p = path; p; p = p->up) {
		hash = hash * 11 + '/';
		n = p->elem + p->len;
		while (p->elem <= --n) {
			unsigned char c = *n;
			hash = hash * 11 + c;
		}
	}
	/*
	 * Make sure "Makefile" and "t/Makefile" are hashed separately
	 * but close enough.
	 */
	hash = (name_hash<<DIRBITS) | (hash & ((1U<<DIRBITS )-1));

	if (0) { /* debug */
		n = name + strlen(name);
		if (n != name && n[-1] == '\n')
			n--;
		while (name <= --n)
			fputc(*n, stderr);
		for (p = path; p; p = p->up) {
			fputc('/', stderr);
			n = p->elem + p->len;
			while (p->elem <= --n)
				fputc(*n, stderr);
		}
		fprintf(stderr, "\t%08x\n", hash);
	}
	return hash;
}

static int add_object_entry(const unsigned char *sha1, unsigned hash, int exclude)
{
	unsigned int idx = nr_objects;
	struct object_entry *entry;
	struct packed_git *p;
	unsigned int found_offset = 0;
	struct packed_git *found_pack = NULL;
	int ix, status = 0;

	if (!exclude) {
		for (p = packed_git; p; p = p->next) {
			struct pack_entry e;
			if (find_pack_entry_one(sha1, &e, p)) {
				if (incremental)
					return 0;
				if (local && !p->pack_local)
					return 0;
				if (!found_pack) {
					found_offset = e.offset;
					found_pack = e.p;
				}
			}
		}
	}
	if ((entry = locate_object_entry(sha1)) != NULL)
		goto already_added;

	if (idx >= nr_alloc) {
		unsigned int needed = (idx + 1024) * 3 / 2;
		objects = xrealloc(objects, needed * sizeof(*entry));
		nr_alloc = needed;
	}
	entry = objects + idx;
	nr_objects = idx + 1;
	memset(entry, 0, sizeof(*entry));
	memcpy(entry->sha1, sha1, 20);
	entry->hash = hash;

	if (object_ix_hashsz * 3 <= nr_objects * 4)
		rehash_objects();
	else {
		ix = locate_object_entry_hash(entry->sha1);
		if (0 <= ix)
			die("internal error in object hashing.");
		object_ix[-1 - ix] = idx + 1;
	}
	status = 1;

 already_added:
	if (progress_update) {
		fprintf(stderr, "Counting objects...%d\r", nr_objects);
		progress_update = 0;
	}
	if (exclude)
		entry->preferred_base = 1;
	else {
		if (found_pack) {
			entry->in_pack = found_pack;
			entry->in_pack_offset = found_offset;
		}
	}
	return status;
}

static void add_pbase_tree(struct tree_desc *tree, struct name_path *up)
{
	while (tree->size) {
		const unsigned char *sha1;
		const char *name;
		unsigned mode, hash;
		unsigned long size;
		char type[20];

		sha1 = tree_entry_extract(tree, &name, &mode);
		update_tree_entry(tree);
		if (!has_sha1_file(sha1))
			continue;
		if (sha1_object_info(sha1, type, &size))
			continue;

		hash = name_hash(up, name);
		if (!add_object_entry(sha1, hash, 1))
			continue;

		if (!strcmp(type, "tree")) {
			struct tree_desc sub;
			void *elem;
			struct name_path me;

			elem = read_sha1_file(sha1, type, &sub.size);
			sub.buf = elem;
			if (sub.buf) {
				me.up = up;
				me.elem = name;
				me.len = strlen(name);
				add_pbase_tree(&sub, &me);
				free(elem);
			}
		}
	}
}

static void add_preferred_base(unsigned char *sha1)
{
	struct tree_desc tree;
	void *elem;

	elem = read_object_with_reference(sha1, "tree", &tree.size, NULL);
	tree.buf = elem;
	if (!tree.buf)
		return;
	if (add_object_entry(sha1, name_hash(NULL, ""), 1))
		add_pbase_tree(&tree, NULL);
	free(elem);
}

static void check_object(struct object_entry *entry)
{
	char type[20];

	if (entry->in_pack && !entry->preferred_base) {
		unsigned char base[20];
		unsigned long size;
		struct object_entry *base_entry;

		/* We want in_pack_type even if we do not reuse delta.
		 * There is no point not reusing non-delta representations.
		 */
		check_reuse_pack_delta(entry->in_pack,
				       entry->in_pack_offset,
				       base, &size,
				       &entry->in_pack_type);

		/* Check if it is delta, and the base is also an object
		 * we are going to pack.  If so we will reuse the existing
		 * delta.
		 */
		if (!no_reuse_delta &&
		    entry->in_pack_type == OBJ_DELTA &&
		    (base_entry = locate_object_entry(base)) &&
		    (!base_entry->preferred_base)) {

			/* Depth value does not matter - find_deltas()
			 * will never consider reused delta as the
			 * base object to deltify other objects
			 * against, in order to avoid circular deltas.
			 */

			/* uncompressed size of the delta data */
			entry->size = entry->delta_size = size;
			entry->delta = base_entry;
			entry->type = OBJ_DELTA;

			entry->delta_sibling = base_entry->delta_child;
			base_entry->delta_child = entry;

			return;
		}
		/* Otherwise we would do the usual */
	}

	if (sha1_object_info(entry->sha1, type, &entry->size))
		die("unable to get type of object %s",
		    sha1_to_hex(entry->sha1));

	if (!strcmp(type, "commit")) {
		entry->type = OBJ_COMMIT;
	} else if (!strcmp(type, "tree")) {
		entry->type = OBJ_TREE;
	} else if (!strcmp(type, "blob")) {
		entry->type = OBJ_BLOB;
	} else if (!strcmp(type, "tag")) {
		entry->type = OBJ_TAG;
	} else
		die("unable to pack object %s of type %s",
		    sha1_to_hex(entry->sha1), type);
}

static unsigned int check_delta_limit(struct object_entry *me, unsigned int n)
{
	struct object_entry *child = me->delta_child;
	unsigned int m = n;
	while (child) {
		unsigned int c = check_delta_limit(child, n + 1);
		if (m < c)
			m = c;
		child = child->delta_sibling;
	}
	return m;
}

static void get_object_details(void)
{
	int i;
	struct object_entry *entry;

	prepare_pack_ix();
	for (i = 0, entry = objects; i < nr_objects; i++, entry++)
		check_object(entry);

	if (nr_objects == nr_result) {
		/*
		 * Depth of objects that depend on the entry -- this
		 * is subtracted from depth-max to break too deep
		 * delta chain because of delta data reusing.
		 * However, we loosen this restriction when we know we
		 * are creating a thin pack -- it will have to be
		 * expanded on the other end anyway, so do not
		 * artificially cut the delta chain and let it go as
		 * deep as it wants.
		 */
		for (i = 0, entry = objects; i < nr_objects; i++, entry++)
			if (!entry->delta && entry->delta_child)
				entry->delta_limit =
					check_delta_limit(entry, 1);
	}
}

typedef int (*entry_sort_t)(const struct object_entry *, const struct object_entry *);

static entry_sort_t current_sort;

static int sort_comparator(const void *_a, const void *_b)
{
	struct object_entry *a = *(struct object_entry **)_a;
	struct object_entry *b = *(struct object_entry **)_b;
	return current_sort(a,b);
}

static struct object_entry **create_sorted_list(entry_sort_t sort)
{
	struct object_entry **list = xmalloc(nr_objects * sizeof(struct object_entry *));
	int i;

	for (i = 0; i < nr_objects; i++)
		list[i] = objects + i;
	current_sort = sort;
	qsort(list, nr_objects, sizeof(struct object_entry *), sort_comparator);
	return list;
}

static int sha1_sort(const struct object_entry *a, const struct object_entry *b)
{
	return memcmp(a->sha1, b->sha1, 20);
}

static struct object_entry **create_final_object_list(void)
{
	struct object_entry **list;
	int i, j;

	for (i = nr_result = 0; i < nr_objects; i++)
		if (!objects[i].preferred_base)
			nr_result++;
	list = xmalloc(nr_result * sizeof(struct object_entry *));
	for (i = j = 0; i < nr_objects; i++) {
		if (!objects[i].preferred_base)
			list[j++] = objects + i;
	}
	current_sort = sha1_sort;
	qsort(list, nr_result, sizeof(struct object_entry *), sort_comparator);
	return list;
}

static int type_size_sort(const struct object_entry *a, const struct object_entry *b)
{
	if (a->type < b->type)
		return -1;
	if (a->type > b->type)
		return 1;
	if (a->hash < b->hash)
		return -1;
	if (a->hash > b->hash)
		return 1;
	if (a->preferred_base < b->preferred_base)
		return -1;
	if (a->preferred_base > b->preferred_base)
		return 1;
	if (a->size < b->size)
		return -1;
	if (a->size > b->size)
		return 1;
	return a < b ? -1 : (a > b);
}

struct unpacked {
	struct object_entry *entry;
	void *data;
};

/*
 * We search for deltas _backwards_ in a list sorted by type and
 * by size, so that we see progressively smaller and smaller files.
 * That's because we prefer deltas to be from the bigger file
 * to the smaller - deletes are potentially cheaper, but perhaps
 * more importantly, the bigger file is likely the more recent
 * one.
 */
static int try_delta(struct unpacked *cur, struct unpacked *old, unsigned max_depth)
{
	struct object_entry *cur_entry = cur->entry;
	struct object_entry *old_entry = old->entry;
	int old_preferred = (old_entry->preferred_base ||
			     old_entry->based_on_preferred);
	unsigned long size, oldsize, delta_size, sizediff;
	long max_size;
	void *delta_buf;

	/* Don't bother doing diffs between different types */
	if (cur_entry->type != old_entry->type)
		return -1;

	/* We do not compute delta to *create* objects we are not
	 * going to pack.
	 */
	if (cur_entry->preferred_base)
		return -1;

	/* If the current object is at pack edge, take the depth the
	 * objects that depend on the current object into account --
	 * otherwise they would become too deep.
	 */
	if (cur_entry->delta_child) {
		if (max_depth <= cur_entry->delta_limit)
			return 0;
		max_depth -= cur_entry->delta_limit;
	}

	size = cur_entry->size;
	oldsize = old_entry->size;
	sizediff = oldsize > size ? oldsize - size : size - oldsize;

	if (size < 50)
		return -1;
	if (old_entry->depth >= max_depth)
		return 0;

	/*
	 * NOTE!
	 *
	 * We always delta from the bigger to the smaller, since that's
	 * more space-efficient (deletes don't have to say _what_ they
	 * delete).
	 */
	max_size = size / 2 - 20;
	if (cur_entry->delta) {
		if (cur_entry->based_on_preferred) {
			if (old_preferred)
				max_size = cur_entry->delta_size-1;
			else
				/* trying with non-preferred one when we
				 * already have a delta based on preferred
				 * one is pointless.
				 */
				return -1;
		}
		else if (!old_preferred)
			max_size = cur_entry->delta_size-1;
		else
			/* otherwise...  even if delta with a
			 * preferred one produces a bigger result than
			 * what we currently have, which is based on a
			 * non-preferred one, it is OK.
			 */
			;
	}
	if (sizediff >= max_size)
		return -1;
	delta_buf = diff_delta(old->data, oldsize,
			       cur->data, size, &delta_size, max_size);
	if (!delta_buf)
		return 0;
	cur_entry->delta = old_entry;
	cur_entry->delta_size = delta_size;
	cur_entry->depth = old_entry->depth + 1;
	cur_entry->based_on_preferred = old_preferred;
	free(delta_buf);
	return 0;
}

static void progress_interval(int signum)
{
	signal(SIGALRM, progress_interval);
	progress_update = 1;
}

static void find_deltas(struct object_entry **list, int window, int depth)
{
	int i, idx;
	unsigned int array_size = window * sizeof(struct unpacked);
	struct unpacked *array = xmalloc(array_size);
	unsigned processed = 0;
	unsigned last_percent = 999;

	memset(array, 0, array_size);
	i = nr_objects;
	idx = 0;
	if (progress)
		fprintf(stderr, "Deltifying %d objects.\n", nr_result);

	while (--i >= 0) {
		struct object_entry *entry = list[i];
		struct unpacked *n = array + idx;
		unsigned long size;
		char type[10];
		int j;

		if (!entry->preferred_base)
			processed++;

		if (progress) {
			unsigned percent = processed * 100 / nr_result;
			if (percent != last_percent || progress_update) {
				fprintf(stderr, "%4u%% (%u/%u) done\r",
					percent, processed, nr_result);
				progress_update = 0;
				last_percent = percent;
			}
		}

		if (entry->delta)
			/* This happens if we decided to reuse existing
			 * delta from a pack.  "!no_reuse_delta &&" is implied.
			 */
			continue;

		free(n->data);
		n->entry = entry;
		n->data = read_sha1_file(entry->sha1, type, &size);
		if (size != entry->size)
			die("object %s inconsistent object length (%lu vs %lu)", sha1_to_hex(entry->sha1), size, entry->size);

		j = window;
		while (--j > 0) {
			unsigned int other_idx = idx + j;
			struct unpacked *m;
			if (other_idx >= window)
				other_idx -= window;
			m = array + other_idx;
			if (!m->entry)
				break;
			if (try_delta(n, m, depth) < 0)
				break;
		}
		idx++;
		if (idx >= window)
			idx = 0;
	}

	if (progress)
		fputc('\n', stderr);

	for (i = 0; i < window; ++i)
		free(array[i].data);
	free(array);
}

static void prepare_pack(int window, int depth)
{
	get_object_details();
	sorted_by_type = create_sorted_list(type_size_sort);
	if (window && depth)
		find_deltas(sorted_by_type, window+1, depth);
}

static int reuse_cached_pack(unsigned char *sha1, int pack_to_stdout)
{
	static const char cache[] = "pack-cache/pack-%s.%s";
	char *cached_pack, *cached_idx;
	int ifd, ofd, ifd_ix = -1;

	cached_pack = git_path(cache, sha1_to_hex(sha1), "pack");
	ifd = open(cached_pack, O_RDONLY);
	if (ifd < 0)
		return 0;

	if (!pack_to_stdout) {
		cached_idx = git_path(cache, sha1_to_hex(sha1), "idx");
		ifd_ix = open(cached_idx, O_RDONLY);
		if (ifd_ix < 0) {
			close(ifd);
			return 0;
		}
	}

	if (progress)
		fprintf(stderr, "Reusing %d objects pack %s\n", nr_objects,
			sha1_to_hex(sha1));

	if (pack_to_stdout) {
		if (copy_fd(ifd, 1))
			exit(1);
		close(ifd);
	}
	else {
		char name[PATH_MAX];
		snprintf(name, sizeof(name),
			 "%s-%s.%s", base_name, sha1_to_hex(sha1), "pack");
		ofd = open(name, O_CREAT | O_EXCL | O_WRONLY, 0666);
		if (ofd < 0)
			die("unable to open %s (%s)", name, strerror(errno));
		if (copy_fd(ifd, ofd))
			exit(1);
		close(ifd);

		snprintf(name, sizeof(name),
			 "%s-%s.%s", base_name, sha1_to_hex(sha1), "idx");
		ofd = open(name, O_CREAT | O_EXCL | O_WRONLY, 0666);
		if (ofd < 0)
			die("unable to open %s (%s)", name, strerror(errno));
		if (copy_fd(ifd_ix, ofd))
			exit(1);
		close(ifd_ix);
		puts(sha1_to_hex(sha1));
	}

	return 1;
}

int main(int argc, char **argv)
{
	SHA_CTX ctx;
	char line[PATH_MAX + 20];
	int window = 10, depth = 10, pack_to_stdout = 0;
	struct object_entry **list;
	int i;

	setup_git_directory();

	for (i = 1; i < argc; i++) {
		const char *arg = argv[i];

		if (*arg == '-') {
			if (!strcmp("--non-empty", arg)) {
				non_empty = 1;
				continue;
			}
			if (!strcmp("--local", arg)) {
				local = 1;
				continue;
			}
			if (!strcmp("--incremental", arg)) {
				incremental = 1;
				continue;
			}
			if (!strncmp("--window=", arg, 9)) {
				char *end;
				window = strtoul(arg+9, &end, 0);
				if (!arg[9] || *end)
					usage(pack_usage);
				continue;
			}
			if (!strncmp("--depth=", arg, 8)) {
				char *end;
				depth = strtoul(arg+8, &end, 0);
				if (!arg[8] || *end)
					usage(pack_usage);
				continue;
			}
			if (!strcmp("-q", arg)) {
				progress = 0;
				continue;
			}
			if (!strcmp("--no-reuse-delta", arg)) {
				no_reuse_delta = 1;
				continue;
			}
			if (!strcmp("--stdout", arg)) {
				pack_to_stdout = 1;
				continue;
			}
			usage(pack_usage);
		}
		if (base_name)
			usage(pack_usage);
		base_name = arg;
	}

	if (pack_to_stdout != !base_name)
		usage(pack_usage);

	prepare_packed_git();

	if (progress) {
		struct itimerval v;
		v.it_interval.tv_sec = 1;
		v.it_interval.tv_usec = 0;
		v.it_value = v.it_interval;
		signal(SIGALRM, progress_interval);
		setitimer(ITIMER_REAL, &v, NULL);
		fprintf(stderr, "Generating pack...\n");
	}

	while (fgets(line, sizeof(line), stdin) != NULL) {
		unsigned char sha1[20];

		if (line[0] == '-') {
			if (get_sha1_hex(line+1, sha1))
				die("expected edge sha1, got garbage:\n %s",
				    line+1);
			add_preferred_base(sha1);
			continue;
		}
		if (get_sha1_hex(line, sha1))
			die("expected sha1, got garbage:\n %s", line);
		add_object_entry(sha1, name_hash(NULL, line+41), 0);
	}
	if (progress)
		fprintf(stderr, "Done counting %d objects.\n", nr_objects);
	sorted_by_sha = create_final_object_list();
	if (non_empty && !nr_result)
		return 0;

	SHA1_Init(&ctx);
	list = sorted_by_sha;
	for (i = 0; i < nr_result; i++) {
		struct object_entry *entry = *list++;
		SHA1_Update(&ctx, entry->sha1, 20);
	}
	SHA1_Final(object_list_sha1, &ctx);
	if (progress && (nr_objects != nr_result))
		fprintf(stderr, "Result has %d objects.\n", nr_result);

	if (reuse_cached_pack(object_list_sha1, pack_to_stdout))
		;
	else {
		if (nr_result)
			prepare_pack(window, depth);
		if (progress && pack_to_stdout) {
			/* the other end usually displays progress itself */
			struct itimerval v = {{0,},};
			setitimer(ITIMER_REAL, &v, NULL);
			signal(SIGALRM, SIG_IGN );
			progress_update = 0;
		}
		write_pack_file();
		if (!pack_to_stdout) {
			write_index_file();
			puts(sha1_to_hex(object_list_sha1));
		}
	}
	if (progress)
		fprintf(stderr, "Total %d, written %d (delta %d), reused %d (delta %d)\n",
			nr_result, written, written_delta, reused, reused_delta);
	return 0;
}
