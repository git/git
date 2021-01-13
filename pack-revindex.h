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
 */

struct packed_git;

/*
 * load_pack_revindex populates the revindex's internal data-structures for the
 * given pack, returning zero on success and a negative value otherwise.
 */
int load_pack_revindex(struct packed_git *p);

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
 * This function runs in constant time.
 */
off_t pack_pos_to_offset(struct packed_git *p, uint32_t pos);

#endif
