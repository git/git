#ifndef PACK_REVINDEX_H
#define PACK_REVINDEX_H

struct packed_git;

struct revindex_entry {
	off_t offset;
	unsigned int nr;
};

void load_pack_revindex(struct packed_git *p);
int find_revindex_position(struct packed_git *p, off_t ofs);

struct revindex_entry *find_pack_revindex(struct packed_git *p, off_t ofs);

#endif
