#ifndef REVISION_H
#define REVISION_H

/*
 * The low 16 bits of the "flags" field shows whether
 * a commit is part of the path to the root for that
 * parent.
 *
 * Bit 16 is an internal flag that we've seen the
 * definition for this rev, and not just seen it as
 * a parent target.
 */
#define marked(rev)	((rev)->flags & 0xffff)
#define SEEN 0x10000
#define USED 0x20000
#define REACHABLE 0x40000

struct parent {
	struct revision *parent;
	struct parent *next;
};

struct revision {
	unsigned int flags;
	unsigned char sha1[20];
	unsigned long date;
	struct parent *parent;
};

static struct revision **revs;
static int nr_revs, rev_allocs;

static int find_rev(unsigned char *sha1)
{
	int first = 0, last = nr_revs;

	while (first < last) {
		int next = (first + last) / 2;
		struct revision *rev = revs[next];
		int cmp;

		cmp = memcmp(sha1, rev->sha1, 20);
		if (!cmp)
			return next;
		if (cmp < 0) {
			last = next;
			continue;
		}
		first = next+1;
	}
	return -first-1;
}

static struct revision *lookup_rev(unsigned char *sha1)
{
	int pos = find_rev(sha1);
	struct revision *n;

	if (pos >= 0)
		return revs[pos];
	
	pos = -pos-1;

	if (rev_allocs == nr_revs) {
		rev_allocs = alloc_nr(rev_allocs);
		revs = realloc(revs, rev_allocs * sizeof(struct revision *));
	}
	n = malloc(sizeof(struct revision));

	n->flags = 0;
	memcpy(n->sha1, sha1, 20);
	n->parent = NULL;

	/* Insert it into the right place */
	memmove(revs + pos + 1, revs + pos, (nr_revs - pos) * sizeof(struct revision *));
	revs[pos] = n;
	nr_revs++;

	return n;
}

static struct revision *add_relationship(struct revision *rev, unsigned char *needs)
{
	struct revision *parent_rev = lookup_rev(needs);
	struct parent **pp = &rev->parent, *p;

	while ((p = *pp) != NULL) {
		if (p->parent == parent_rev)
			return parent_rev;
		pp = &p->next;
	}

	p = malloc(sizeof(*p));
	p->parent = parent_rev;
	p->next = NULL;
	*pp = p;
	return parent_rev;
}

static void mark_reachable(struct revision *rev)
{
	struct parent *p = rev->parent;

	/* If we've been here already, don't bother */
	if (rev->flags & REACHABLE)
		return;
	rev->flags |= REACHABLE | USED;
	while (p) {
		mark_reachable(p->parent);
		p = p->next;
	}
}

#endif /* REVISION_H */
