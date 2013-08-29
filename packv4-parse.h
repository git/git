#ifndef PACKV4_PARSE_H
#define PACKV4_PARSE_H

void *pv4_get_commit(struct packed_git *p, struct pack_window **w_curs,
		     off_t offset, unsigned long size);

#endif
