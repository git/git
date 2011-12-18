#include "cache.h"
#include "pack-revindex.h"

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

struct pack_revindex {
	struct packed_git *p;
	struct revindex_entry *revindex;
};

static struct pack_revindex *pack_revindex;
static int pack_revindex_hashsz;

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

static void init_pack_revindex(void)
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
static void create_pack_revindex(struct pack_revindex *rix)
{
	struct packed_git *p = rix->p;
	int num_ent = p->num_objects;
	int i;
	const char *index = p->index_data;

	rix->revindex = xmalloc(sizeof(*rix->revindex) * (num_ent + 1));
	index += 4 * 256;

	if (p->index_version > 1) {
		const uint32_t *off_32 =
			(uint32_t *)(index + 8 + p->num_objects * (20 + 4));
		const uint32_t *off_64 = off_32 + p->num_objects;
		for (i = 0; i < num_ent; i++) {
			uint32_t off = ntohl(*off_32++);
			if (!(off & 0x80000000)) {
				rix->revindex[i].offset = off;
			} else {
				rix->revindex[i].offset =
					((uint64_t)ntohl(*off_64++)) << 32;
				rix->revindex[i].offset |=
					ntohl(*off_64++);
			}
			rix->revindex[i].nr = i;
		}
	} else {
		for (i = 0; i < num_ent; i++) {
			uint32_t hl = *((uint32_t *)(index + 24 * i));
			rix->revindex[i].offset = ntohl(hl);
			rix->revindex[i].nr = i;
		}
	}

	/* This knows the pack format -- the 20-byte trailer
	 * follows immediately after the last object data.
	 */
	rix->revindex[num_ent].offset = p->pack_size - 20;
	rix->revindex[num_ent].nr = -1;
	qsort(rix->revindex, num_ent, sizeof(*rix->revindex), cmp_offset);
}

struct revindex_entry *find_pack_revindex(struct packed_git *p, off_t ofs)
{
	int num;
	int lo, hi;
	struct pack_revindex *rix;
	struct revindex_entry *revindex;

	if (!pack_revindex_hashsz)
		init_pack_revindex();
	num = pack_revindex_ix(p);
	if (num < 0)
		die("internal error: pack revindex fubar");

	rix = &pack_revindex[num];
	if (!rix->revindex)
		create_pack_revindex(rix);
	revindex = rix->revindex;

	lo = 0;
	hi = p->num_objects + 1;
	do {
		int mi = (lo + hi) / 2;
		if (revindex[mi].offset == ofs) {
			return revindex + mi;
		} else if (ofs < revindex[mi].offset)
			hi = mi;
		else
			lo = mi + 1;
	} while (lo < hi);
	error("bad offset for revindex");
	return NULL;
}

void discard_revindex(void)
{
	if (pack_revindex_hashsz) {
		int i;
		for (i = 0; i < pack_revindex_hashsz; i++)
			free(pack_revindex[i].revindex);
		free(pack_revindex);
		pack_revindex_hashsz = 0;
	}
}
