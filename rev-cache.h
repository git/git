#ifndef REV_CACHE_H
#define REV_CACHE_H

extern struct rev_cache {
	struct rev_cache *head_list;
	struct rev_list_elem *children;
	struct rev_list_elem *parents;
	struct rev_list_elem *parents_tail;
	unsigned short num_parents;
	unsigned short num_children;
	unsigned int written : 1;
	unsigned int parsed : 1;
	unsigned int work : 30;
	void *work_ptr;
	unsigned char sha1[20];
} **rev_cache;
extern int nr_revs, alloc_revs;

struct rev_list_elem {
	struct rev_list_elem *next;
	struct rev_cache *ri;
};

extern int find_rev_cache(const unsigned char *);
extern int read_rev_cache(const char *, FILE *, int);
extern int record_rev_cache(const unsigned char *, FILE *);
extern void write_rev_cache(const char *new, const char *old);

#endif
