#ifndef PACK_REVINDEX_H
#define PACK_REVINDEX_H

struct revindex_entry {
	off_t offset;
	unsigned int nr;
};

void init_pack_revindex(void);
struct revindex_entry *find_pack_revindex(struct packed_git *p, off_t ofs);

#endif
