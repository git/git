#include "builtin.h"
#include "cache.h"
#include "object.h"
#include "blob.h"
#include "commit.h"
#include "tag.h"
#include "tree.h"
#include "delta.h"
#include "pack.h"
#include "csum-file.h"
#include "tree-walk.h"
#include "diff.h"
#include "revision.h"
#include "list-objects.h"
#include <sys/time.h>
#include <signal.h>

static const char pack_usage[] = "\
git-pack-objects [{ -q | --progress | --all-progress }] \n\
	[--local] [--incremental] [--window=N] [--depth=N] \n\
	[--no-reuse-delta] [--delta-base-offset] [--non-empty] \n\
	[--revs [--unpacked | --all]*] [--stdout | base-name] \n\
	[<ref-list | <object-list]";

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
#define in_pack_header_size delta_size	/* only when reusing pack data */
	struct object_entry *delta;	/* delta base object */
	struct packed_git *in_pack; 	/* already in pack */
	unsigned int in_pack_offset;
	struct object_entry *delta_child; /* deltified objects who bases me */
	struct object_entry *delta_sibling; /* other deltified objects who
					     * uses the same base as me
					     */
	int preferred_base;	/* we do not pack this, but is encouraged to
				 * be used as the base objectto delta huge
				 * objects against.
				 */
};

/*
 * Objects we are going to pack are collected in objects array (dynamically
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
static int non_empty;
static int no_reuse_delta;
static int local;
static int incremental;
static int allow_ofs_delta;

static struct object_entry **sorted_by_sha, **sorted_by_type;
static struct object_entry *objects;
static int nr_objects, nr_alloc, nr_result;
static const char *base_name;
static unsigned char pack_file_sha1[20];
static int progress = 1;
static volatile sig_atomic_t progress_update;
static int window = 10;
static int pack_to_stdout;
static int num_preferred_base;

/*
 * The object names in objects array are hashed with this hashtable,
 * to help looking up the entry by object name.  Binary search from
 * sorted_by_sha is also possible but this was easier to code and faster.
 * This hashtable is built after all the objects are seen.
 */
static int *object_ix;
static int object_ix_hashsz;

/*
 * Pack index for existing packs give us easy access to the offsets into
 * corresponding pack file where each object's data starts, but the entries
 * do not store the size of the compressed representation (uncompressed
 * size is easily available by examining the pack entry header).  It is
 * also rather expensive to find the sha1 for an object given its offset.
 *
 * We build a hashtable of existing packs (pack_revindex), and keep reverse
 * index here -- pack index file is sorted by object name mapping to offset;
 * this pack_revindex[].revindex array is a list of offset/index_nr pairs
 * ordered by offset, so if you know the offset of an object, next offset
 * is where its packed representation ends and the index_nr can be used to
 * get the object sha1 from the main index.
 */
struct revindex_entry {
	unsigned int offset;
	unsigned int nr;
};
struct pack_revindex {
	struct packed_git *p;
	struct revindex_entry *revindex;
};
static struct  pack_revindex *pack_revindex;
static int pack_revindex_hashsz;

/*
 * stats
 */
static int written;
static int written_delta;
static int reused;
static int reused_delta;

static int pack_revindex_ix(struct packed_git *p)
{
	unsigned long ui = (unsigned long)p;
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
	const struct revindex_entry *a = a_;
	const struct revindex_entry *b = b_;
	return (a->offset < b->offset) ? -1 : (a->offset > b->offset) ? 1 : 0;
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

	rix->revindex = xmalloc(sizeof(*rix->revindex) * (num_ent + 1));
	for (i = 0; i < num_ent; i++) {
		unsigned int hl = *((unsigned int *)((char *) index + 24*i));
		rix->revindex[i].offset = ntohl(hl);
		rix->revindex[i].nr = i;
	}
	/* This knows the pack format -- the 20-byte trailer
	 * follows immediately after the last object data.
	 */
	rix->revindex[num_ent].offset = p->pack_size - 20;
	rix->revindex[num_ent].nr = -1;
	qsort(rix->revindex, num_ent, sizeof(*rix->revindex), cmp_offset);
}

static struct revindex_entry * find_packed_object(struct packed_git *p,
						  unsigned int ofs)
{
	int num;
	int lo, hi;
	struct pack_revindex *rix;
	struct revindex_entry *revindex;
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
		if (revindex[mi].offset == ofs) {
			return revindex + mi;
		}
		else if (ofs < revindex[mi].offset)
			hi = mi;
		else
			lo = mi + 1;
	} while (lo < hi);
	die("internal error: pack revindex corrupt");
}

static unsigned long find_packed_object_size(struct packed_git *p,
					     unsigned long ofs)
{
	struct revindex_entry *entry = find_packed_object(p, ofs);
	return entry[1].offset - ofs;
}

static unsigned char *find_packed_object_name(struct packed_git *p,
					      unsigned long ofs)
{
	struct revindex_entry *entry = find_packed_object(p, ofs);
	return (unsigned char *)(p->index_base + 256) + 24 * entry->nr + 4;
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

	if (type < OBJ_COMMIT || type > OBJ_REF_DELTA)
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

/*
 * we are going to reuse the existing object data as is.  make
 * sure it is not corrupt.
 */
static int check_inflate(unsigned char *data, unsigned long len, unsigned long expect)
{
	z_stream stream;
	unsigned char fakebuf[4096];
	int st;

	memset(&stream, 0, sizeof(stream));
	stream.next_in = data;
	stream.avail_in = len;
	stream.next_out = fakebuf;
	stream.avail_out = sizeof(fakebuf);
	inflateInit(&stream);

	while (1) {
		st = inflate(&stream, Z_FINISH);
		if (st == Z_STREAM_END || st == Z_OK) {
			st = (stream.total_out == expect &&
			      stream.total_in == len) ? 0 : -1;
			break;
		}
		if (st != Z_BUF_ERROR) {
			st = -1;
			break;
		}
		stream.next_out = fakebuf;
		stream.avail_out = sizeof(fakebuf);
	}
	inflateEnd(&stream);
	return st;
}

static int revalidate_loose_object(struct object_entry *entry,
				   unsigned char *map,
				   unsigned long mapsize)
{
	/* we already know this is a loose object with new type header. */
	enum object_type type;
	unsigned long size, used;

	if (pack_to_stdout)
		return 0;

	used = unpack_object_header_gently(map, mapsize, &type, &size);
	if (!used)
		return -1;
	map += used;
	mapsize -= used;
	return check_inflate(map, mapsize, size);
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

	obj_type = entry->type;
	if (! entry->in_pack)
		to_reuse = 0;	/* can't reuse what we don't have */
	else if (obj_type == OBJ_REF_DELTA || obj_type == OBJ_OFS_DELTA)
		to_reuse = 1;	/* check_object() decided it for us */
	else if (obj_type != entry->in_pack_type)
		to_reuse = 0;	/* pack has delta which is unusable */
	else if (entry->delta)
		to_reuse = 0;	/* we want to pack afresh */
	else
		to_reuse = 1;	/* we have it in-pack undeltified,
				 * and we do not need to deltify it.
				 */

	if (!entry->in_pack && !entry->delta) {
		unsigned char *map;
		unsigned long mapsize;
		map = map_sha1_file(entry->sha1, &mapsize);
		if (map && !legacy_loose_object(map)) {
			/* We can copy straight into the pack file */
			if (revalidate_loose_object(entry, map, mapsize))
				die("corrupt loose object %s",
				    sha1_to_hex(entry->sha1));
			sha1write(f, map, mapsize);
			munmap(map, mapsize);
			written++;
			reused++;
			return mapsize;
		}
		if (map)
			munmap(map, mapsize);
	}

	if (!to_reuse) {
		buf = read_sha1_file(entry->sha1, type, &size);
		if (!buf)
			die("unable to read %s", sha1_to_hex(entry->sha1));
		if (size != entry->size)
			die("object %s size inconsistency (%lu vs %lu)",
			    sha1_to_hex(entry->sha1), size, entry->size);
		if (entry->delta) {
			buf = delta_against(buf, size, entry);
			size = entry->delta_size;
			obj_type = (allow_ofs_delta && entry->delta->offset) ?
				OBJ_OFS_DELTA : OBJ_REF_DELTA;
		}
		/*
		 * The object header is a byte of 'type' followed by zero or
		 * more bytes of length.
		 */
		hdrlen = encode_header(obj_type, size, header);
		sha1write(f, header, hdrlen);

		if (obj_type == OBJ_OFS_DELTA) {
			/*
			 * Deltas with relative base contain an additional
			 * encoding of the relative offset for the delta
			 * base from this object's position in the pack.
			 */
			unsigned long ofs = entry->offset - entry->delta->offset;
			unsigned pos = sizeof(header) - 1;
			header[pos] = ofs & 127;
			while (ofs >>= 7)
				header[--pos] = 128 | (--ofs & 127);
			sha1write(f, header + pos, sizeof(header) - pos);
			hdrlen += sizeof(header) - pos;
		} else if (obj_type == OBJ_REF_DELTA) {
			/*
			 * Deltas with a base reference contain
			 * an additional 20 bytes for the base sha1.
			 */
			sha1write(f, entry->delta->sha1, 20);
			hdrlen += 20;
		}
		datalen = sha1write_compressed(f, buf, size);
		free(buf);
	}
	else {
		struct packed_git *p = entry->in_pack;

		if (entry->delta) {
			obj_type = (allow_ofs_delta && entry->delta->offset) ?
				OBJ_OFS_DELTA : OBJ_REF_DELTA;
			reused_delta++;
		}
		hdrlen = encode_header(obj_type, entry->size, header);
		sha1write(f, header, hdrlen);
		if (obj_type == OBJ_OFS_DELTA) {
			unsigned long ofs = entry->offset - entry->delta->offset;
			unsigned pos = sizeof(header) - 1;
			header[pos] = ofs & 127;
			while (ofs >>= 7)
				header[--pos] = 128 | (--ofs & 127);
			sha1write(f, header + pos, sizeof(header) - pos);
			hdrlen += sizeof(header) - pos;
		} else if (obj_type == OBJ_REF_DELTA) {
			sha1write(f, entry->delta->sha1, 20);
			hdrlen += 20;
		}

		use_packed_git(p);
		buf = (char *) p->pack_base
			+ entry->in_pack_offset
			+ entry->in_pack_header_size;
		datalen = find_packed_object_size(p, entry->in_pack_offset)
				- entry->in_pack_header_size;
		if (!pack_to_stdout && check_inflate(buf, datalen, entry->size))
			die("corrupt delta in pack %s", sha1_to_hex(entry->sha1));
		sha1write(f, buf, datalen);
		unuse_packed_git(p);
		reused++;
	}
	if (entry->delta)
		written_delta++;
	written++;
	return hdrlen + datalen;
}

static unsigned long write_one(struct sha1file *f,
			       struct object_entry *e,
			       unsigned long offset)
{
	if (e->offset || e->preferred_base)
		/* offset starts from header size and cannot be zero
		 * if it is written already.
		 */
		return offset;
	/* if we are deltified, write out its base object first. */
	if (e->delta)
		offset = write_one(f, e->delta, offset);
	e->offset = offset;
	return offset + write_object(f, e);
}

static void write_pack_file(void)
{
	int i;
	struct sha1file *f;
	unsigned long offset;
	struct pack_header hdr;
	unsigned last_percent = 999;
	int do_progress = progress;

	if (!base_name) {
		f = sha1fd(1, "<stdout>");
		do_progress >>= 1;
	}
	else
		f = sha1create("%s-%s.%s", base_name,
			       sha1_to_hex(object_list_sha1), "pack");
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
		if (!hashcmp(sha1, objects[object_ix[i] - 1].sha1))
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
	memset(object_ix, 0, sizeof(int) * object_ix_hashsz);
	for (i = 0, oe = objects; i < nr_objects; i++, oe++) {
		int ix = locate_object_entry_hash(oe->sha1);
		if (0 <= ix)
			continue;
		ix = -1 - ix;
		object_ix[ix] = i + 1;
	}
}

static unsigned name_hash(const char *name)
{
	unsigned char c;
	unsigned hash = 0;

	/*
	 * This effectively just creates a sortable number from the
	 * last sixteen non-whitespace characters. Last characters
	 * count "most", so things that end in ".c" sort together.
	 */
	while ((c = *name++) != 0) {
		if (isspace(c))
			continue;
		hash = (hash >> 2) + (c << 24);
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
			unsigned long offset = find_pack_entry_one(sha1, p);
			if (offset) {
				if (incremental)
					return 0;
				if (local && !p->pack_local)
					return 0;
				if (!found_pack) {
					found_offset = offset;
					found_pack = p;
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
	hashcpy(entry->sha1, sha1);
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

struct pbase_tree_cache {
	unsigned char sha1[20];
	int ref;
	int temporary;
	void *tree_data;
	unsigned long tree_size;
};

static struct pbase_tree_cache *(pbase_tree_cache[256]);
static int pbase_tree_cache_ix(const unsigned char *sha1)
{
	return sha1[0] % ARRAY_SIZE(pbase_tree_cache);
}
static int pbase_tree_cache_ix_incr(int ix)
{
	return (ix+1) % ARRAY_SIZE(pbase_tree_cache);
}

static struct pbase_tree {
	struct pbase_tree *next;
	/* This is a phony "cache" entry; we are not
	 * going to evict it nor find it through _get()
	 * mechanism -- this is for the toplevel node that
	 * would almost always change with any commit.
	 */
	struct pbase_tree_cache pcache;
} *pbase_tree;

static struct pbase_tree_cache *pbase_tree_get(const unsigned char *sha1)
{
	struct pbase_tree_cache *ent, *nent;
	void *data;
	unsigned long size;
	char type[20];
	int neigh;
	int my_ix = pbase_tree_cache_ix(sha1);
	int available_ix = -1;

	/* pbase-tree-cache acts as a limited hashtable.
	 * your object will be found at your index or within a few
	 * slots after that slot if it is cached.
	 */
	for (neigh = 0; neigh < 8; neigh++) {
		ent = pbase_tree_cache[my_ix];
		if (ent && !hashcmp(ent->sha1, sha1)) {
			ent->ref++;
			return ent;
		}
		else if (((available_ix < 0) && (!ent || !ent->ref)) ||
			 ((0 <= available_ix) &&
			  (!ent && pbase_tree_cache[available_ix])))
			available_ix = my_ix;
		if (!ent)
			break;
		my_ix = pbase_tree_cache_ix_incr(my_ix);
	}

	/* Did not find one.  Either we got a bogus request or
	 * we need to read and perhaps cache.
	 */
	data = read_sha1_file(sha1, type, &size);
	if (!data)
		return NULL;
	if (strcmp(type, tree_type)) {
		free(data);
		return NULL;
	}

	/* We need to either cache or return a throwaway copy */

	if (available_ix < 0)
		ent = NULL;
	else {
		ent = pbase_tree_cache[available_ix];
		my_ix = available_ix;
	}

	if (!ent) {
		nent = xmalloc(sizeof(*nent));
		nent->temporary = (available_ix < 0);
	}
	else {
		/* evict and reuse */
		free(ent->tree_data);
		nent = ent;
	}
	hashcpy(nent->sha1, sha1);
	nent->tree_data = data;
	nent->tree_size = size;
	nent->ref = 1;
	if (!nent->temporary)
		pbase_tree_cache[my_ix] = nent;
	return nent;
}

static void pbase_tree_put(struct pbase_tree_cache *cache)
{
	if (!cache->temporary) {
		cache->ref--;
		return;
	}
	free(cache->tree_data);
	free(cache);
}

static int name_cmp_len(const char *name)
{
	int i;
	for (i = 0; name[i] && name[i] != '\n' && name[i] != '/'; i++)
		;
	return i;
}

static void add_pbase_object(struct tree_desc *tree,
			     const char *name,
			     int cmplen,
			     const char *fullname)
{
	struct name_entry entry;

	while (tree_entry(tree,&entry)) {
		unsigned long size;
		char type[20];

		if (entry.pathlen != cmplen ||
		    memcmp(entry.path, name, cmplen) ||
		    !has_sha1_file(entry.sha1) ||
		    sha1_object_info(entry.sha1, type, &size))
			continue;
		if (name[cmplen] != '/') {
			unsigned hash = name_hash(fullname);
			add_object_entry(entry.sha1, hash, 1);
			return;
		}
		if (!strcmp(type, tree_type)) {
			struct tree_desc sub;
			struct pbase_tree_cache *tree;
			const char *down = name+cmplen+1;
			int downlen = name_cmp_len(down);

			tree = pbase_tree_get(entry.sha1);
			if (!tree)
				return;
			sub.buf = tree->tree_data;
			sub.size = tree->tree_size;

			add_pbase_object(&sub, down, downlen, fullname);
			pbase_tree_put(tree);
		}
	}
}

static unsigned *done_pbase_paths;
static int done_pbase_paths_num;
static int done_pbase_paths_alloc;
static int done_pbase_path_pos(unsigned hash)
{
	int lo = 0;
	int hi = done_pbase_paths_num;
	while (lo < hi) {
		int mi = (hi + lo) / 2;
		if (done_pbase_paths[mi] == hash)
			return mi;
		if (done_pbase_paths[mi] < hash)
			hi = mi;
		else
			lo = mi + 1;
	}
	return -lo-1;
}

static int check_pbase_path(unsigned hash)
{
	int pos = (!done_pbase_paths) ? -1 : done_pbase_path_pos(hash);
	if (0 <= pos)
		return 1;
	pos = -pos - 1;
	if (done_pbase_paths_alloc <= done_pbase_paths_num) {
		done_pbase_paths_alloc = alloc_nr(done_pbase_paths_alloc);
		done_pbase_paths = xrealloc(done_pbase_paths,
					    done_pbase_paths_alloc *
					    sizeof(unsigned));
	}
	done_pbase_paths_num++;
	if (pos < done_pbase_paths_num)
		memmove(done_pbase_paths + pos + 1,
			done_pbase_paths + pos,
			(done_pbase_paths_num - pos - 1) * sizeof(unsigned));
	done_pbase_paths[pos] = hash;
	return 0;
}

static void add_preferred_base_object(const char *name, unsigned hash)
{
	struct pbase_tree *it;
	int cmplen = name_cmp_len(name);

	if (check_pbase_path(hash))
		return;

	for (it = pbase_tree; it; it = it->next) {
		if (cmplen == 0) {
			hash = name_hash("");
			add_object_entry(it->pcache.sha1, hash, 1);
		}
		else {
			struct tree_desc tree;
			tree.buf = it->pcache.tree_data;
			tree.size = it->pcache.tree_size;
			add_pbase_object(&tree, name, cmplen, name);
		}
	}
}

static void add_preferred_base(unsigned char *sha1)
{
	struct pbase_tree *it;
	void *data;
	unsigned long size;
	unsigned char tree_sha1[20];

	if (window <= num_preferred_base++)
		return;

	data = read_object_with_reference(sha1, tree_type, &size, tree_sha1);
	if (!data)
		return;

	for (it = pbase_tree; it; it = it->next) {
		if (!hashcmp(it->pcache.sha1, tree_sha1)) {
			free(data);
			return;
		}
	}

	it = xcalloc(1, sizeof(*it));
	it->next = pbase_tree;
	pbase_tree = it;

	hashcpy(it->pcache.sha1, tree_sha1);
	it->pcache.tree_data = data;
	it->pcache.tree_size = size;
}

static void check_object(struct object_entry *entry)
{
	char type[20];

	if (entry->in_pack && !entry->preferred_base) {
		struct packed_git *p = entry->in_pack;
		unsigned long left = p->pack_size - entry->in_pack_offset;
		unsigned long size, used;
		unsigned char *buf;
		struct object_entry *base_entry = NULL;

		use_packed_git(p);
		buf = p->pack_base;
		buf += entry->in_pack_offset;

		/* We want in_pack_type even if we do not reuse delta.
		 * There is no point not reusing non-delta representations.
		 */
		used = unpack_object_header_gently(buf, left,
						   &entry->in_pack_type, &size);
		if (!used || left - used <= 20)
			die("corrupt pack for %s", sha1_to_hex(entry->sha1));

		/* Check if it is delta, and the base is also an object
		 * we are going to pack.  If so we will reuse the existing
		 * delta.
		 */
		if (!no_reuse_delta) {
			unsigned char c, *base_name;
			unsigned long ofs;
			/* there is at least 20 bytes left in the pack */
			switch (entry->in_pack_type) {
			case OBJ_REF_DELTA:
				base_name = buf + used;
				used += 20;
				break;
			case OBJ_OFS_DELTA:
				c = buf[used++];
				ofs = c & 127;
				while (c & 128) {
					ofs += 1;
					if (!ofs || ofs & ~(~0UL >> 7))
						die("delta base offset overflow in pack for %s",
						    sha1_to_hex(entry->sha1));
					c = buf[used++];
					ofs = (ofs << 7) + (c & 127);
				}
				if (ofs >= entry->in_pack_offset)
					die("delta base offset out of bound for %s",
					    sha1_to_hex(entry->sha1));
				ofs = entry->in_pack_offset - ofs;
				base_name = find_packed_object_name(p, ofs);
				break;
			default:
				base_name = NULL;
			}
			if (base_name)
				base_entry = locate_object_entry(base_name);
		}
		unuse_packed_git(p);
		entry->in_pack_header_size = used;

		if (base_entry) {

			/* Depth value does not matter - find_deltas()
			 * will never consider reused delta as the
			 * base object to deltify other objects
			 * against, in order to avoid circular deltas.
			 */

			/* uncompressed size of the delta data */
			entry->size = size;
			entry->delta = base_entry;
			entry->type = entry->in_pack_type;

			entry->delta_sibling = base_entry->delta_child;
			base_entry->delta_child = entry;

			return;
		}
		/* Otherwise we would do the usual */
	}

	if (sha1_object_info(entry->sha1, type, &entry->size))
		die("unable to get type of object %s",
		    sha1_to_hex(entry->sha1));

	if (!strcmp(type, commit_type)) {
		entry->type = OBJ_COMMIT;
	} else if (!strcmp(type, tree_type)) {
		entry->type = OBJ_TREE;
	} else if (!strcmp(type, blob_type)) {
		entry->type = OBJ_BLOB;
	} else if (!strcmp(type, tag_type)) {
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
	return hashcmp(a->sha1, b->sha1);
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
	struct delta_index *index;
};

/*
 * We search for deltas _backwards_ in a list sorted by type and
 * by size, so that we see progressively smaller and smaller files.
 * That's because we prefer deltas to be from the bigger file
 * to the smaller - deletes are potentially cheaper, but perhaps
 * more importantly, the bigger file is likely the more recent
 * one.
 */
static int try_delta(struct unpacked *trg, struct unpacked *src,
		     unsigned max_depth)
{
	struct object_entry *trg_entry = trg->entry;
	struct object_entry *src_entry = src->entry;
	unsigned long trg_size, src_size, delta_size, sizediff, max_size, sz;
	char type[10];
	void *delta_buf;

	/* Don't bother doing diffs between different types */
	if (trg_entry->type != src_entry->type)
		return -1;

	/* We do not compute delta to *create* objects we are not
	 * going to pack.
	 */
	if (trg_entry->preferred_base)
		return -1;

	/*
	 * We do not bother to try a delta that we discarded
	 * on an earlier try, but only when reusing delta data.
	 */
	if (!no_reuse_delta && trg_entry->in_pack &&
	    trg_entry->in_pack == src_entry->in_pack)
		return 0;

	/*
	 * If the current object is at pack edge, take the depth the
	 * objects that depend on the current object into account --
	 * otherwise they would become too deep.
	 */
	if (trg_entry->delta_child) {
		if (max_depth <= trg_entry->delta_limit)
			return 0;
		max_depth -= trg_entry->delta_limit;
	}
	if (src_entry->depth >= max_depth)
		return 0;

	/* Now some size filtering heuristics. */
	trg_size = trg_entry->size;
	max_size = trg_size/2 - 20;
	max_size = max_size * (max_depth - src_entry->depth) / max_depth;
	if (max_size == 0)
		return 0;
	if (trg_entry->delta && trg_entry->delta_size <= max_size)
		max_size = trg_entry->delta_size-1;
	src_size = src_entry->size;
	sizediff = src_size < trg_size ? trg_size - src_size : 0;
	if (sizediff >= max_size)
		return 0;

	/* Load data if not already done */
	if (!trg->data) {
		trg->data = read_sha1_file(trg_entry->sha1, type, &sz);
		if (sz != trg_size)
			die("object %s inconsistent object length (%lu vs %lu)",
			    sha1_to_hex(trg_entry->sha1), sz, trg_size);
	}
	if (!src->data) {
		src->data = read_sha1_file(src_entry->sha1, type, &sz);
		if (sz != src_size)
			die("object %s inconsistent object length (%lu vs %lu)",
			    sha1_to_hex(src_entry->sha1), sz, src_size);
	}
	if (!src->index) {
		src->index = create_delta_index(src->data, src_size);
		if (!src->index)
			die("out of memory");
	}

	delta_buf = create_delta(src->index, trg->data, trg_size, &delta_size, max_size);
	if (!delta_buf)
		return 0;

	trg_entry->delta = src_entry;
	trg_entry->delta_size = delta_size;
	trg_entry->depth = src_entry->depth + 1;
	free(delta_buf);
	return 1;
}

static void progress_interval(int signum)
{
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

		if (entry->size < 50)
			continue;
		free_delta_index(n->index);
		n->index = NULL;
		free(n->data);
		n->data = NULL;
		n->entry = entry;

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
		/* if we made n a delta, and if n is already at max
		 * depth, leaving it in the window is pointless.  we
		 * should evict it first.
		 */
		if (entry->delta && depth <= entry->depth)
			continue;

		idx++;
		if (idx >= window)
			idx = 0;
	}

	if (progress)
		fputc('\n', stderr);

	for (i = 0; i < window; ++i) {
		free_delta_index(array[i].index);
		free(array[i].data);
	}
	free(array);
}

static void prepare_pack(int window, int depth)
{
	get_object_details();
	sorted_by_type = create_sorted_list(type_size_sort);
	if (window && depth)
		find_deltas(sorted_by_type, window+1, depth);
}

static int reuse_cached_pack(unsigned char *sha1)
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

static void setup_progress_signal(void)
{
	struct sigaction sa;
	struct itimerval v;

	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = progress_interval;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESTART;
	sigaction(SIGALRM, &sa, NULL);

	v.it_interval.tv_sec = 1;
	v.it_interval.tv_usec = 0;
	v.it_value = v.it_interval;
	setitimer(ITIMER_REAL, &v, NULL);
}

static int git_pack_config(const char *k, const char *v)
{
	if(!strcmp(k, "pack.window")) {
		window = git_config_int(k, v);
		return 0;
	}
	return git_default_config(k, v);
}

static void read_object_list_from_stdin(void)
{
	char line[40 + 1 + PATH_MAX + 2];
	unsigned char sha1[20];
	unsigned hash;

	for (;;) {
		if (!fgets(line, sizeof(line), stdin)) {
			if (feof(stdin))
				break;
			if (!ferror(stdin))
				die("fgets returned NULL, not EOF, not error!");
			if (errno != EINTR)
				die("fgets: %s", strerror(errno));
			clearerr(stdin);
			continue;
		}
		if (line[0] == '-') {
			if (get_sha1_hex(line+1, sha1))
				die("expected edge sha1, got garbage:\n %s",
				    line);
			add_preferred_base(sha1);
			continue;
		}
		if (get_sha1_hex(line, sha1))
			die("expected sha1, got garbage:\n %s", line);

		hash = name_hash(line+41);
		add_preferred_base_object(line+41, hash);
		add_object_entry(sha1, hash, 0);
	}
}

static void show_commit(struct commit *commit)
{
	unsigned hash = name_hash("");
	add_preferred_base_object("", hash);
	add_object_entry(commit->object.sha1, hash, 0);
}

static void show_object(struct object_array_entry *p)
{
	unsigned hash = name_hash(p->name);
	add_preferred_base_object(p->name, hash);
	add_object_entry(p->item->sha1, hash, 0);
}

static void show_edge(struct commit *commit)
{
	add_preferred_base(commit->object.sha1);
}

static void get_object_list(int ac, const char **av)
{
	struct rev_info revs;
	char line[1000];
	int flags = 0;

	init_revisions(&revs, NULL);
	save_commit_buffer = 0;
	track_object_refs = 0;
	setup_revisions(ac, av, &revs, NULL);

	while (fgets(line, sizeof(line), stdin) != NULL) {
		int len = strlen(line);
		if (line[len - 1] == '\n')
			line[--len] = 0;
		if (!len)
			break;
		if (*line == '-') {
			if (!strcmp(line, "--not")) {
				flags ^= UNINTERESTING;
				continue;
			}
			die("not a rev '%s'", line);
		}
		if (handle_revision_arg(line, &revs, flags, 1))
			die("bad revision '%s'", line);
	}

	prepare_revision_walk(&revs);
	mark_edges_uninteresting(revs.commits, &revs, show_edge);
	traverse_commit_list(&revs, show_commit, show_object);
}

int cmd_pack_objects(int argc, const char **argv, const char *prefix)
{
	SHA_CTX ctx;
	int depth = 10;
	struct object_entry **list;
	int use_internal_rev_list = 0;
	int thin = 0;
	int i;
	const char *rp_av[64];
	int rp_ac;

	rp_av[0] = "pack-objects";
	rp_av[1] = "--objects"; /* --thin will make it --objects-edge */
	rp_ac = 2;

	git_config(git_pack_config);

	progress = isatty(2);
	for (i = 1; i < argc; i++) {
		const char *arg = argv[i];

		if (*arg != '-')
			break;

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
		if (!strcmp("--progress", arg)) {
			progress = 1;
			continue;
		}
		if (!strcmp("--all-progress", arg)) {
			progress = 2;
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
		if (!strcmp("--delta-base-offset", arg)) {
			allow_ofs_delta = 1;
			continue;
		}
		if (!strcmp("--stdout", arg)) {
			pack_to_stdout = 1;
			continue;
		}
		if (!strcmp("--revs", arg)) {
			use_internal_rev_list = 1;
			continue;
		}
		if (!strcmp("--unpacked", arg) ||
		    !strncmp("--unpacked=", arg, 11) ||
		    !strcmp("--all", arg)) {
			use_internal_rev_list = 1;
			if (ARRAY_SIZE(rp_av) - 1 <= rp_ac)
				die("too many internal rev-list options");
			rp_av[rp_ac++] = arg;
			continue;
		}
		if (!strcmp("--thin", arg)) {
			use_internal_rev_list = 1;
			thin = 1;
			rp_av[1] = "--objects-edge";
			continue;
		}
		usage(pack_usage);
	}

	/* Traditionally "pack-objects [options] base extra" failed;
	 * we would however want to take refs parameter that would
	 * have been given to upstream rev-list ourselves, which means
	 * we somehow want to say what the base name is.  So the
	 * syntax would be:
	 *
	 * pack-objects [options] base <refs...>
	 *
	 * in other words, we would treat the first non-option as the
	 * base_name and send everything else to the internal revision
	 * walker.
	 */

	if (!pack_to_stdout)
		base_name = argv[i++];

	if (pack_to_stdout != !base_name)
		usage(pack_usage);

	if (!pack_to_stdout && thin)
		die("--thin cannot be used to build an indexable pack.");

	prepare_packed_git();

	if (progress) {
		fprintf(stderr, "Generating pack...\n");
		setup_progress_signal();
	}

	if (!use_internal_rev_list)
		read_object_list_from_stdin();
	else {
		rp_av[rp_ac] = NULL;
		get_object_list(rp_ac, rp_av);
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

	if (reuse_cached_pack(object_list_sha1))
		;
	else {
		if (nr_result)
			prepare_pack(window, depth);
		if (progress == 1 && pack_to_stdout) {
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
