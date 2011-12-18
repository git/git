#include "cache.h"
#include "tree-walk.h"
#include "tree.h"

static const char *get_mode(const char *str, unsigned int *modep)
{
	unsigned char c;
	unsigned int mode = 0;

	if (*str == ' ')
		return NULL;

	while ((c = *str++) != ' ') {
		if (c < '0' || c > '7')
			return NULL;
		mode = (mode << 3) + (c - '0');
	}
	*modep = mode;
	return str;
}

static void decode_tree_entry(struct tree_desc *desc, const char *buf, unsigned long size)
{
	const char *path;
	unsigned int mode, len;

	if (size < 24 || buf[size - 21])
		die("corrupt tree file");

	path = get_mode(buf, &mode);
	if (!path || !*path)
		die("corrupt tree file");
	len = strlen(path) + 1;

	/* Initialize the descriptor entry */
	desc->entry.path = path;
	desc->entry.mode = mode;
	desc->entry.sha1 = (const unsigned char *)(path + len);
}

void init_tree_desc(struct tree_desc *desc, const void *buffer, unsigned long size)
{
	desc->buffer = buffer;
	desc->size = size;
	if (size)
		decode_tree_entry(desc, buffer, size);
}

void *fill_tree_descriptor(struct tree_desc *desc, const unsigned char *sha1)
{
	unsigned long size = 0;
	void *buf = NULL;

	if (sha1) {
		buf = read_object_with_reference(sha1, tree_type, &size, NULL);
		if (!buf)
			die("unable to read tree %s", sha1_to_hex(sha1));
	}
	init_tree_desc(desc, buf, size);
	return buf;
}

static void entry_clear(struct name_entry *a)
{
	memset(a, 0, sizeof(*a));
}

static void entry_extract(struct tree_desc *t, struct name_entry *a)
{
	*a = t->entry;
}

void update_tree_entry(struct tree_desc *desc)
{
	const void *buf = desc->buffer;
	const unsigned char *end = desc->entry.sha1 + 20;
	unsigned long size = desc->size;
	unsigned long len = end - (const unsigned char *)buf;

	if (size < len)
		die("corrupt tree file");
	buf = end;
	size -= len;
	desc->buffer = buf;
	desc->size = size;
	if (size)
		decode_tree_entry(desc, buf, size);
}

int tree_entry(struct tree_desc *desc, struct name_entry *entry)
{
	if (!desc->size)
		return 0;

	*entry = desc->entry;
	update_tree_entry(desc);
	return 1;
}

void setup_traverse_info(struct traverse_info *info, const char *base)
{
	int pathlen = strlen(base);
	static struct traverse_info dummy;

	memset(info, 0, sizeof(*info));
	if (pathlen && base[pathlen-1] == '/')
		pathlen--;
	info->pathlen = pathlen ? pathlen + 1 : 0;
	info->name.path = base;
	info->name.sha1 = (void *)(base + pathlen + 1);
	if (pathlen)
		info->prev = &dummy;
}

char *make_traverse_path(char *path, const struct traverse_info *info, const struct name_entry *n)
{
	int len = tree_entry_len(n->path, n->sha1);
	int pathlen = info->pathlen;

	path[pathlen + len] = 0;
	for (;;) {
		memcpy(path + pathlen, n->path, len);
		if (!pathlen)
			break;
		path[--pathlen] = '/';
		n = &info->name;
		len = tree_entry_len(n->path, n->sha1);
		info = info->prev;
		pathlen -= len;
	}
	return path;
}

struct tree_desc_skip {
	struct tree_desc_skip *prev;
	const void *ptr;
};

struct tree_desc_x {
	struct tree_desc d;
	struct tree_desc_skip *skip;
};

static int name_compare(const char *a, int a_len,
			const char *b, int b_len)
{
	int len = (a_len < b_len) ? a_len : b_len;
	int cmp = memcmp(a, b, len);
	if (cmp)
		return cmp;
	return (a_len - b_len);
}

static int check_entry_match(const char *a, int a_len, const char *b, int b_len)
{
	/*
	 * The caller wants to pick *a* from a tree or nothing.
	 * We are looking at *b* in a tree.
	 *
	 * (0) If a and b are the same name, we are trivially happy.
	 *
	 * There are three possibilities where *a* could be hiding
	 * behind *b*.
	 *
	 * (1) *a* == "t",   *b* == "ab"  i.e. *b* sorts earlier than *a* no
	 *                                matter what.
	 * (2) *a* == "t",   *b* == "t-2" and "t" is a subtree in the tree;
	 * (3) *a* == "t-2", *b* == "t"   and "t-2" is a blob in the tree.
	 *
	 * Otherwise we know *a* won't appear in the tree without
	 * scanning further.
	 */

	int cmp = name_compare(a, a_len, b, b_len);

	/* Most common case first -- reading sync'd trees */
	if (!cmp)
		return cmp;

	if (0 < cmp) {
		/* a comes after b; it does not matter if it is case (3)
		if (b_len < a_len && !memcmp(a, b, b_len) && a[b_len] < '/')
			return 1;
		*/
		return 1; /* keep looking */
	}

	/* b comes after a; are we looking at case (2)? */
	if (a_len < b_len && !memcmp(a, b, a_len) && b[a_len] < '/')
		return 1; /* keep looking */

	return -1; /* a cannot appear in the tree */
}

/*
 * From the extended tree_desc, extract the first name entry, while
 * paying attention to the candidate "first" name.  Most importantly,
 * when looking for an entry, if there are entries that sorts earlier
 * in the tree object representation than that name, skip them and
 * process the named entry first.  We will remember that we haven't
 * processed the first entry yet, and in the later call skip the
 * entry we processed early when update_extended_entry() is called.
 *
 * E.g. if the underlying tree object has these entries:
 *
 *    blob    "t-1"
 *    blob    "t-2"
 *    tree    "t"
 *    blob    "t=1"
 *
 * and the "first" asks for "t", remember that we still need to
 * process "t-1" and "t-2" but extract "t".  After processing the
 * entry "t" from this call, the caller will let us know by calling
 * update_extended_entry() that we can remember "t" has been processed
 * already.
 */

static void extended_entry_extract(struct tree_desc_x *t,
				   struct name_entry *a,
				   const char *first,
				   int first_len)
{
	const char *path;
	int len;
	struct tree_desc probe;
	struct tree_desc_skip *skip;

	/*
	 * Extract the first entry from the tree_desc, but skip the
	 * ones that we already returned in earlier rounds.
	 */
	while (1) {
		if (!t->d.size) {
			entry_clear(a);
			break; /* not found */
		}
		entry_extract(&t->d, a);
		for (skip = t->skip; skip; skip = skip->prev)
			if (a->path == skip->ptr)
				break; /* found */
		if (!skip)
			break;
		/* We have processed this entry already. */
		update_tree_entry(&t->d);
	}

	if (!first || !a->path)
		return;

	/*
	 * The caller wants "first" from this tree, or nothing.
	 */
	path = a->path;
	len = tree_entry_len(a->path, a->sha1);
	switch (check_entry_match(first, first_len, path, len)) {
	case -1:
		entry_clear(a);
	case 0:
		return;
	default:
		break;
	}

	/*
	 * We need to look-ahead -- we suspect that a subtree whose
	 * name is "first" may be hiding behind the current entry "path".
	 */
	probe = t->d;
	while (probe.size) {
		entry_extract(&probe, a);
		path = a->path;
		len = tree_entry_len(a->path, a->sha1);
		switch (check_entry_match(first, first_len, path, len)) {
		case -1:
			entry_clear(a);
		case 0:
			return;
		default:
			update_tree_entry(&probe);
			break;
		}
		/* keep looking */
	}
	entry_clear(a);
}

static void update_extended_entry(struct tree_desc_x *t, struct name_entry *a)
{
	if (t->d.entry.path == a->path) {
		update_tree_entry(&t->d);
	} else {
		/* we have returned this entry early */
		struct tree_desc_skip *skip = xmalloc(sizeof(*skip));
		skip->ptr = a->path;
		skip->prev = t->skip;
		t->skip = skip;
	}
}

static void free_extended_entry(struct tree_desc_x *t)
{
	struct tree_desc_skip *p, *s;

	for (s = t->skip; s; s = p) {
		p = s->prev;
		free(s);
	}
}

int traverse_trees(int n, struct tree_desc *t, struct traverse_info *info)
{
	int ret = 0;
	struct name_entry *entry = xmalloc(n*sizeof(*entry));
	int i;
	struct tree_desc_x *tx = xcalloc(n, sizeof(*tx));

	for (i = 0; i < n; i++)
		tx[i].d = t[i];

	for (;;) {
		unsigned long mask, dirmask;
		const char *first = NULL;
		int first_len = 0;
		struct name_entry *e;
		int len;

		for (i = 0; i < n; i++) {
			e = entry + i;
			extended_entry_extract(tx + i, e, NULL, 0);
		}

		/*
		 * A tree may have "t-2" at the current location even
		 * though it may have "t" that is a subtree behind it,
		 * and another tree may return "t".  We want to grab
		 * all "t" from all trees to match in such a case.
		 */
		for (i = 0; i < n; i++) {
			e = entry + i;
			if (!e->path)
				continue;
			len = tree_entry_len(e->path, e->sha1);
			if (!first) {
				first = e->path;
				first_len = len;
				continue;
			}
			if (name_compare(e->path, len, first, first_len) < 0) {
				first = e->path;
				first_len = len;
			}
		}

		if (first) {
			for (i = 0; i < n; i++) {
				e = entry + i;
				extended_entry_extract(tx + i, e, first, first_len);
				/* Cull the ones that are not the earliest */
				if (!e->path)
					continue;
				len = tree_entry_len(e->path, e->sha1);
				if (name_compare(e->path, len, first, first_len))
					entry_clear(e);
			}
		}

		/* Now we have in entry[i] the earliest name from the trees */
		mask = 0;
		dirmask = 0;
		for (i = 0; i < n; i++) {
			if (!entry[i].path)
				continue;
			mask |= 1ul << i;
			if (S_ISDIR(entry[i].mode))
				dirmask |= 1ul << i;
		}
		if (!mask)
			break;
		ret = info->fn(n, mask, dirmask, entry, info);
		if (ret < 0)
			break;
		mask &= ret;
		ret = 0;
		for (i = 0; i < n; i++)
			if (mask & (1ul << i))
				update_extended_entry(tx + i, entry + i);
	}
	free(entry);
	for (i = 0; i < n; i++)
		free_extended_entry(tx + i);
	free(tx);
	return ret;
}

static int find_tree_entry(struct tree_desc *t, const char *name, unsigned char *result, unsigned *mode)
{
	int namelen = strlen(name);
	while (t->size) {
		const char *entry;
		const unsigned char *sha1;
		int entrylen, cmp;

		sha1 = tree_entry_extract(t, &entry, mode);
		update_tree_entry(t);
		entrylen = tree_entry_len(entry, sha1);
		if (entrylen > namelen)
			continue;
		cmp = memcmp(name, entry, entrylen);
		if (cmp > 0)
			continue;
		if (cmp < 0)
			break;
		if (entrylen == namelen) {
			hashcpy(result, sha1);
			return 0;
		}
		if (name[entrylen] != '/')
			continue;
		if (!S_ISDIR(*mode))
			break;
		if (++entrylen == namelen) {
			hashcpy(result, sha1);
			return 0;
		}
		return get_tree_entry(sha1, name + entrylen, result, mode);
	}
	return -1;
}

int get_tree_entry(const unsigned char *tree_sha1, const char *name, unsigned char *sha1, unsigned *mode)
{
	int retval;
	void *tree;
	unsigned long size;
	struct tree_desc t;
	unsigned char root[20];

	tree = read_object_with_reference(tree_sha1, tree_type, &size, root);
	if (!tree)
		return -1;

	if (name[0] == '\0') {
		hashcpy(sha1, root);
		free(tree);
		return 0;
	}

	init_tree_desc(&t, tree, size);
	retval = find_tree_entry(&t, name, sha1, mode);
	free(tree);
	return retval;
}
