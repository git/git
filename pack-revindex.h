#ifndef PACK_REVINDEX_H
#define PACK_REVINDEX_H

struct revindex_entry {
	off_t offset;
	unsigned int nr;
};

struct pack_revindex {
	struct packed_git *p;
	struct revindex_entry *revindex;
};

struct pack_revindex *revindex_for_pack(struct packed_git *p);
int find_revindex_position(struct pack_revindex *pridx, off_t ofs);

struct revindex_entry *find_pack_revindex(struct packed_git *p, off_t ofs);

#endif
