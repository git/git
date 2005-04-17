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
	char tag[1];
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

static struct revision *lookup_rev(unsigned char *sha1, const char *tag)
{
	int pos = find_rev(sha1);
	struct revision *n;

	if (pos >= 0) {
		n = revs[pos];
		if (strcmp(n->tag, tag))
			error("expected tag %s on object %s: got %s", tag, sha1_to_hex(sha1), n->tag);
		return n;
	}
	
	pos = -pos-1;

	if (rev_allocs == nr_revs) {
		rev_allocs = alloc_nr(rev_allocs);
		revs = realloc(revs, rev_allocs * sizeof(struct revision *));
	}
	n = malloc(sizeof(struct revision) + strlen(tag));

	n->flags = 0;
	memcpy(n->sha1, sha1, 20);
	n->parent = NULL;
	strcpy(n->tag, tag);

	/* Insert it into the right place */
	memmove(revs + pos + 1, revs + pos, (nr_revs - pos) * sizeof(struct revision *));
	revs[pos] = n;
	nr_revs++;

	return n;
}

static struct revision *add_relationship(struct revision *rev, unsigned char *needs, const char *tag)
{
	struct revision *parent_rev = lookup_rev(needs, tag);
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

static void mark_reachable(struct revision *rev, unsigned int mask)
{
	struct parent *p = rev->parent;

	/* If we've been here already, don't bother */
	if (rev->flags & mask)
		return;
	rev->flags |= mask | USED;
	while (p) {
		mark_reachable(p->parent, mask);
		p = p->next;
	}
}

static unsigned long parse_commit_date(const char *buf)
{
	unsigned long date;

	if (memcmp(buf, "author", 6))
		return 0;
	while (*buf++ != '\n')
		/* nada */;
	if (memcmp(buf, "committer", 9))
		return 0;
	while (*buf++ != '>')
		/* nada */;
	date = strtoul(buf, NULL, 10);
	if (date == ULONG_MAX)
		date = 0;
	return date;
}

static struct revision * parse_commit(unsigned char *sha1)
{
	struct revision *rev = lookup_rev(sha1, "commit");

	if (!(rev->flags & SEEN)) {
		void *buffer, *bufptr;
		unsigned long size;
		char type[20];
		unsigned char parent[20];

		rev->flags |= SEEN;
		buffer = bufptr = read_sha1_file(sha1, type, &size);
		if (!buffer || strcmp(type, "commit"))
			die("%s is not a commit object", sha1_to_hex(sha1));
		bufptr += 46; /* "tree " + "hex sha1" + "\n" */
		while (!memcmp(bufptr, "parent ", 7) && !get_sha1_hex(bufptr+7, parent)) {
			add_relationship(rev, parent, "commit");
			parse_commit(parent);
			bufptr += 48;	/* "parent " + "hex sha1" + "\n" */
		}
		rev->date = parse_commit_date(bufptr);
		free(buffer);
	}
	return rev;
}

#endif /* REVISION_H */
