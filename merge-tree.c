#include "cache.h"

struct tree_entry {
	unsigned mode;
	unsigned char *sha1;
	char *path;
	struct tree_entry *next;
};

static struct tree_entry *read_tree(unsigned char *sha1)
{
	char type[20];
	unsigned long size;
	void *buf = read_sha1_file(sha1, type, &size);
	struct tree_entry *ret = NULL, **tp = &ret;

	if (!buf || strcmp(type, "tree"))
		die("unable to read 'tree' object %s", sha1_to_hex(sha1));
	while (size) {
		int len = strlen(buf)+1;
		struct tree_entry * entry = malloc(sizeof(struct tree_entry));
		if (size < len + 20 || sscanf(buf, "%o", &entry->mode) != 1)
			die("corrupt 'tree' object %s", sha1_to_hex(sha1));
		entry->path = strchr(buf, ' ')+1;
		entry->sha1 = buf + len;
		entry->next = NULL;
		*tp = entry;
		tp = &entry->next;
		len += 20;
		buf += len;
		size -= len;
	}
	return ret;
}

static void show(const struct tree_entry *a, const char *path)
{
	printf("select %o %s %s%c", a->mode, sha1_to_hex(a->sha1), path, 0);
}

static void merge(const struct tree_entry *a, const struct tree_entry *b, const struct tree_entry *c, const char *path)
{
	char hex_a[60], hex_b[60], hex_c[60];
	strcpy(hex_a, sha1_to_hex(a->sha1));
	strcpy(hex_b, sha1_to_hex(b->sha1));
	strcpy(hex_c, sha1_to_hex(c->sha1));
	printf("merge %o->%o,%o %s->%s,%s %s%c",
		a->mode, b->mode, c->mode,
		hex_a, hex_b, hex_c, path, 0);
}

static int same(const struct tree_entry *a, const struct tree_entry *b)
{
	return a->mode == b->mode && !memcmp(a->sha1, b->sha1, 20);
}

static void merge_entry(const struct tree_entry *src, const struct tree_entry *dst1, const struct tree_entry *dst2)
{
	static unsigned char nullsha1[20];
	static const struct tree_entry none = { 0, nullsha1, "", NULL };
	const char *path = NULL;
	const struct tree_entry *a, *b, *c;

	a = &none;
	b = &none;
	c = &none;
	if (src) { a = src; path = src->path; }
	if (dst1) { b = dst1; path = dst1->path; }
	if (dst2) { c = dst2; path = dst2->path; }
	if (same(b, c)) {
		show(b, path);
		return;
	}
	if (same(a, b)) {
		show(c, path);
		return;
	}
	if (same(a, c)) {
		show(b, path);
		return;
	}
	merge(a, b, c, path);
}

/* For two entries, select the smaller one, clear the bigger one */
static void smaller(struct tree_entry **ap, struct tree_entry **bp)
{
	struct tree_entry *a = *ap, *b = *bp;
	if (a && b) {
		int cmp = cache_name_compare(a->path, strlen(a->path), b->path, strlen(b->path));
		if (cmp) {
			if (cmp < 0)
				*bp = NULL;
			else
				*ap = NULL;
		}
	}
}

static void merge_tree(struct tree_entry *src, struct tree_entry *dst1, struct tree_entry *dst2)
{
	while (src || dst1 || dst2) {
		struct tree_entry *a, *b, *c;
		a = src;
		b = dst1;
		c = dst2;
		smaller(&a,&b);
		smaller(&a,&c);
		smaller(&b,&c);
		if (a) src = a->next;
		if (b) dst1 = b->next;
		if (c) dst2 = c->next;
		merge_entry(a,b,c);
	}
}

int main(int argc, char **argv)
{
	unsigned char src[20], dst1[20], dst2[20];

	if (argc != 4 ||
	    get_sha1_hex(argv[1], src) ||
	    get_sha1_hex(argv[2], dst1) ||
	    get_sha1_hex(argv[3], dst2))
		usage("merge-tree <src> <dst1> <dst2>");
	merge_tree(read_tree(src), read_tree(dst1), read_tree(dst2));
	return 0;
}
