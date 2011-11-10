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

static int entry_compare(struct name_entry *a, struct name_entry *b)
{
	return df_name_compare(
			a->path, tree_entry_len(a->path, a->sha1), a->mode,
			b->path, tree_entry_len(b->path, b->sha1), b->mode);
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

int traverse_trees(int n, struct tree_desc *t, struct traverse_info *info)
{
	int ret = 0;
	struct name_entry *entry = xmalloc(n*sizeof(*entry));

	for (;;) {
		unsigned long mask = 0;
		unsigned long dirmask = 0;
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
			if (S_ISDIR(entry[i].mode))
				dirmask |= 1ul << i;
			last = i;
		}
		if (!mask)
			break;
		dirmask &= mask;

		/*
		 * Clear all the unused name-entries.
		 */
		for (i = 0; i < n; i++) {
			if (mask & (1ul << i))
				continue;
			entry_clear(entry + i);
		}
		ret = info->fn(n, mask, dirmask, entry, info);
		if (ret < 0)
			break;
		if (ret)
			mask &= ret;
		ret = 0;
		for (i = 0; i < n; i++) {
			if (mask & (1ul << i))
				update_tree_entry(t + i);
		}
	}
	free(entry);
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
		return 0;
	}

	init_tree_desc(&t, tree, size);
	retval = find_tree_entry(&t, name, sha1, mode);
	free(tree);
	return retval;
}
