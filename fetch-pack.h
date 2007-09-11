#ifndef FETCH_PACK_H
#define FETCH_PACK_H

struct fetch_pack_args
{
	const char *uploadpack;
	int quiet;
	int keep_pack;
	int unpacklimit;
	int use_thin_pack;
	int fetch_all;
	int verbose;
	int depth;
	int no_progress;
};

void setup_fetch_pack(struct fetch_pack_args *args);

struct ref *fetch_pack(const char *dest, int nr_heads, char **heads);

#endif
