#ifndef PACK_REVINDEX_H
#define PACK_REVINDEX_H

/**
 * A revindex allows converting efficiently between three properties
 * of an object within a pack:
 *
 * - index position: the numeric position within the list of sorted object ids
 *   found in the .idx file
 *
 * - pack position: the numeric position within the list of objects in their
 *   order within the actual .pack file (i.e., 0 is the first object in the
 *   .pack, 1 is the second, and so on)
 *
 * - offset: the byte offset within the .pack file at which the object contents
 *   can be found
 *
 * The revindex can also be used with a multi-pack index (MIDX). In this
 * setting:
 *
 *   - index position refers to an object's numeric position within the MIDX
 *
 *   - pack position refers to an object's position within a non-existent pack
 *     described by the MIDX. The pack structure is described in
 *     Documentation/technical/pack-format.txt.
 *
 *     It is effectively a concatanation of all packs in the MIDX (ordered by
 *     their numeric ID within the MIDX) in their original order within each
 *     pack), removing duplicates, and placing the preferred pack (if any)
 *     first.
 */


#define RIDX_SIGNATURE 0x52494458 /* "RIDX" */
#define RIDX_VERSION 1

#define GIT_TEST_WRITE_REV_INDEX "GIT_TEST_WRITE_REV_INDEX"
#define GIT_TEST_REV_INDEX_DIE_IN_MEMORY "GIT_TEST_REV_INDEX_DIE_IN_MEMORY"

struct packed_git;
struct multi_pack_index;

/*
 * load_pack_revindex populates the revindex's internal data-structures for the
 * given pack, returning zero on success and a negative value otherwise.
 *
 * If a '.rev' file is present it is mmap'd, and pointers are assigned into it
 * (instead of using the in-memory variant).
 */
int load_pack_revindex(struct packed_git *p);

/*
 * load_midx_revindex loads the '.rev' file corresponding to the given
 * multi-pack index by mmap-ing it and assigning pointers in the
 * multi_pack_index to point at it.
 *
 * A negative number is returned on error.
 */
int load_midx_revindex(struct multi_pack_index *m);

/*
 * Frees resources associated with a multi-pack reverse index.
 *
 * A negative number is returned on error.
 */
int close_midx_revindex(struct multi_pack_index *m);

/*
 * offset_to_pack_pos converts an object offset to a pack position. This
 * function returns zero on success, and a negative number otherwise. The
 * parameter 'pos' is usable only on success.
 *
 * If the reverse index has not yet been loaded, this function loads it lazily,
 * and returns an negative number if an error was encountered.
 *
 * This function runs in time O(log N) with the number of objects in the pack.
 */
int offset_to_pack_pos(struct packed_git *p, off_t ofs, uint32_t *pos);

/*
 * pack_pos_to_index converts the given pack-relative position 'pos' by
 * returning an index-relative position.
 *
 * If the reverse index has not yet been loaded, or the position is out of
 * bounds, this function aborts.
 *
 * This function runs in constant time.
 */
uint32_t pack_pos_to_index(struct packed_git *p, uint32_t pos);

/*
 * pack_pos_to_offset converts the given pack-relative position 'pos' into a
 * pack offset. For a pack with 'N' objects, asking for position 'N' will return
 * the total size (in bytes) of the pack.
 *
 * If the reverse index has not yet been loaded, or the position is out of
 * bounds, this function aborts.
 *
 * This function runs in constant time under both in-memory and on-disk reverse
 * indexes, but an additional step is taken to consult the corresponding .idx
 * file when using the on-disk format.
 */
off_t pack_pos_to_offset(struct packed_git *p, uint32_t pos);

/*
 * pack_pos_to_midx converts the object at position "pos" within the MIDX
 * pseudo-pack into a MIDX position.
 *
 * If the reverse index has not yet been loaded, or the position is out of
 * bounds, this function aborts.
 *
 * This function runs in constant time.
 */
uint32_t pack_pos_to_midx(struct multi_pack_index *m, uint32_t pos);

/*
 * midx_to_pack_pos converts from the MIDX-relative position at "at" to the
 * corresponding pack position.
 *
 * If the reverse index has not yet been loaded, or the position is out of
 * bounds, this function aborts.
 *
 * This function runs in time O(log N) with the number of objects in the MIDX.
 */
int midx_to_pack_pos(struct multi_pack_index *midx, uint32_t at, uint32_t *pos);

#endif
