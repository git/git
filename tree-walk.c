#include "cache.h"
#include "tree-walk.h"
#include "tree.h"

void init_tree_desc(struct tree_desc *desc, const void *buffer, unsigned long size)
{
	desc->buffer = buffer;
	desc->size = size;
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

static int entry_compare(struct name_entry *a, struct name_entry *b)
{
	return base_name_compare(
			a->path, tree_entry_len(a->path, a->sha1), a->mode,
			b->path, tree_entry_len(b->path, b->sha1), b->mode);
}

static void entry_clear(struct name_entry *a)
{
	memset(a, 0, sizeof(*a));
}

static void entry_extract(struct tree_desc *t, struct name_entry *a)
{
	a->sha1 = tree_entry_extract(t, &a->path, &a->mode);
}

void update_tree_entry(struct tree_desc *desc)
{
	const void *buf = desc->buffer;
	unsigned long size = desc->size;
	int len = strlen(buf) + 1 + 20;

	if (size < len)
		die("corrupt tree file");
	desc->buffer = (char *) buf + len;
	desc->size = size - len;
}

static const char *get_mode(const char *str, unsigned int *modep)
{
	unsigned char c;
	unsigned int mode = 0;

	while ((c = *str++) != ' ') {
		if (c < '0' || c > '7')
			return NULL;
		mode = (mode << 3) + (c - '0');
	}
	*modep = mode;
	return str;
}

const unsigned char *tree_entry_extract(struct tree_desc *desc, const char **pathp, unsigned int *modep)
{
	const void *tree = desc->buffer;
	unsigned long size = desc->size;
	int len = strlen(tree)+1;
	const unsigned char *sha1 = (unsigned char *) tree + len;
	const char *path;
	unsigned int mode;

	path = get_mode(tree, &mode);
	if (!path || size < len + 20)
		die("corrupt tree file");
	*pathp = path;
	*modep = canon_mode(mode);
	return sha1;
}

int tree_entry(struct tree_desc *desc, struct name_entry *entry)
{
	const void *tree = desc->buffer;
	const char *path;
	unsigned long len, size = desc->size;

	if (!size)
		return 0;

	path = get_mode(tree, &entry->mode);
	if (!path)
		die("corrupt tree file");

	entry->path = path;
	len = strlen(path);

	path += len + 1;
	entry->sha1 = (const unsigned char *) path;

	path += 20;
	len = path - (char *) tree;
	if (len > size)
		die("corrupt tree file");

	desc->buffer = path;
	desc->size = size - len;
	return 1;
}

void traverse_trees(int n, struct tree_desc *t, const char *base, traverse_callback_t callback)
{
	struct name_entry *entry = xmalloc(n*sizeof(*entry));

	for (;;) {
		unsigned long mask = 0;
		int i, last;

		last = -1;
		for (i = 0; i < n; i++) {
			if (!t[i].size)
				continue;
			entry_extract(t+i, entry+i);
			if (last >= 0) {
				int cmp = entry_compare(entry+i, entry+last);

				/*
				 * Is the new name bigger than the old one?
				 * Ignore it
				 */
				if (cmp > 0)
					continue;
				/*
				 * Is the new name smaller than the old one?
				 * Ignore all old ones
				 */
				if (cmp < 0)
					mask = 0;
			}
			mask |= 1ul << i;
			last = i;
		}
		if (!mask)
			break;

		/*
		 * Update the tree entries we've walked, and clear
		 * all the unused name-entries.
		 */
		for (i = 0; i < n; i++) {
			if (mask & (1ul << i)) {
				update_tree_entry(t+i);
				continue;
			}
			entry_clear(entry + i);
		}
		callback(n, mask, entry, base);
	}
	free(entry);
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
		return 0;
	}

	init_tree_desc(&t, tree, size);
	retval = find_tree_entry(&t, name, sha1, mode);
	free(tree);
	return retval;
}

