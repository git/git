#include "cache.h"
#include "tree.h"
#include "cache-tree.h"

#define DEBUG 0

struct cache_tree *cache_tree(void)
{
	struct cache_tree *it = xcalloc(1, sizeof(struct cache_tree));
	it->entry_count = -1;
	return it;
}

void cache_tree_free(struct cache_tree **it_p)
{
	int i;
	struct cache_tree *it = *it_p;

	if (!it)
		return;
	for (i = 0; i < it->subtree_nr; i++)
		if (it->down[i])
			cache_tree_free(&it->down[i]->cache_tree);
	free(it->down);
	free(it);
	*it_p = NULL;
}

static int subtree_name_cmp(const char *one, int onelen,
			    const char *two, int twolen)
{
	if (onelen < twolen)
		return -1;
	if (twolen < onelen)
		return 1;
	return memcmp(one, two, onelen);
}

static int subtree_pos(struct cache_tree *it, const char *path, int pathlen)
{
	struct cache_tree_sub **down = it->down;
	int lo, hi;
	lo = 0;
	hi = it->subtree_nr;
	while (lo < hi) {
		int mi = (lo + hi) / 2;
		struct cache_tree_sub *mdl = down[mi];
		int cmp = subtree_name_cmp(path, pathlen,
					   mdl->name, mdl->namelen);
		if (!cmp)
			return mi;
		if (cmp < 0)
			hi = mi;
		else
			lo = mi + 1;
	}
	return -lo-1;
}

static struct cache_tree_sub *find_subtree(struct cache_tree *it,
					   const char *path,
					   int pathlen,
					   int create)
{
	struct cache_tree_sub *down;
	int pos = subtree_pos(it, path, pathlen);
	if (0 <= pos)
		return it->down[pos];
	if (!create)
		return NULL;

	pos = -pos-1;
	if (it->subtree_alloc <= it->subtree_nr) {
		it->subtree_alloc = alloc_nr(it->subtree_alloc);
		it->down = xrealloc(it->down, it->subtree_alloc *
				    sizeof(*it->down));
	}
	it->subtree_nr++;

	down = xmalloc(sizeof(*down) + pathlen + 1);
	down->cache_tree = NULL;
	down->namelen = pathlen;
	memcpy(down->name, path, pathlen);
	down->name[pathlen] = 0;

	if (pos < it->subtree_nr)
		memmove(it->down + pos + 1,
			it->down + pos,
			sizeof(down) * (it->subtree_nr - pos - 1));
	it->down[pos] = down;
	return down;
}

struct cache_tree_sub *cache_tree_sub(struct cache_tree *it, const char *path)
{
	int pathlen = strlen(path);
	return find_subtree(it, path, pathlen, 1);
}

void cache_tree_invalidate_path(struct cache_tree *it, const char *path)
{
	/* a/b/c
	 * ==> invalidate self
	 * ==> find "a", have it invalidate "b/c"
	 * a
	 * ==> invalidate self
	 * ==> if "a" exists as a subtree, remove it.
	 */
	const char *slash;
	int namelen;
	struct cache_tree_sub *down;

#if DEBUG
	fprintf(stderr, "cache-tree invalidate <%s>\n", path);
#endif

	if (!it)
		return;
	slash = strchr(path, '/');
	it->entry_count = -1;
	if (!slash) {
		int pos;
		namelen = strlen(path);
		pos = subtree_pos(it, path, namelen);
		if (0 <= pos) {
			cache_tree_free(&it->down[pos]->cache_tree);
			free(it->down[pos]);
			/* 0 1 2 3 4 5
			 *       ^     ^subtree_nr = 6
			 *       pos
			 * move 4 and 5 up one place (2 entries)
			 * 2 = 6 - 3 - 1 = subtree_nr - pos - 1
			 */
			memmove(it->down+pos, it->down+pos+1,
				sizeof(struct cache_tree_sub *) *
				(it->subtree_nr - pos - 1));
			it->subtree_nr--;
		}
		return;
	}
	namelen = slash - path;
	down = find_subtree(it, path, namelen, 0);
	if (down)
		cache_tree_invalidate_path(down->cache_tree, slash + 1);
}

static int verify_cache(struct cache_entry **cache,
			int entries)
{
	int i, funny;

	/* Verify that the tree is merged */
	funny = 0;
	for (i = 0; i < entries; i++) {
		struct cache_entry *ce = cache[i];
		if (ce_stage(ce)) {
			if (10 < ++funny) {
				fprintf(stderr, "...\n");
				break;
			}
			fprintf(stderr, "%s: unmerged (%s)\n",
				ce->name, sha1_to_hex(ce->sha1));
		}
	}
	if (funny)
		return -1;

	/* Also verify that the cache does not have path and path/file
	 * at the same time.  At this point we know the cache has only
	 * stage 0 entries.
	 */
	funny = 0;
	for (i = 0; i < entries - 1; i++) {
		/* path/file always comes after path because of the way
		 * the cache is sorted.  Also path can appear only once,
		 * which means conflicting one would immediately follow.
		 */
		const char *this_name = cache[i]->name;
		const char *next_name = cache[i+1]->name;
		int this_len = strlen(this_name);
		if (this_len < strlen(next_name) &&
		    strncmp(this_name, next_name, this_len) == 0 &&
		    next_name[this_len] == '/') {
			if (10 < ++funny) {
				fprintf(stderr, "...\n");
				break;
			}
			fprintf(stderr, "You have both %s and %s\n",
				this_name, next_name);
		}
	}
	if (funny)
		return -1;
	return 0;
}

static void discard_unused_subtrees(struct cache_tree *it)
{
	struct cache_tree_sub **down = it->down;
	int nr = it->subtree_nr;
	int dst, src;
	for (dst = src = 0; src < nr; src++) {
		struct cache_tree_sub *s = down[src];
		if (s->used)
			down[dst++] = s;
		else {
			cache_tree_free(&s->cache_tree);
			free(s);
			it->subtree_nr--;
		}
	}
}

int cache_tree_fully_valid(struct cache_tree *it)
{
	int i;
	if (!it)
		return 0;
	if (it->entry_count < 0 || !has_sha1_file(it->sha1))
		return 0;
	for (i = 0; i < it->subtree_nr; i++) {
		if (!cache_tree_fully_valid(it->down[i]->cache_tree))
			return 0;
	}
	return 1;
}

static int update_one(struct cache_tree *it,
		      struct cache_entry **cache,
		      int entries,
		      const char *base,
		      int baselen,
		      int missing_ok,
		      int dryrun)
{
	unsigned long size, offset;
	char *buffer;
	int i;

	if (0 <= it->entry_count && has_sha1_file(it->sha1))
		return it->entry_count;

	/*
	 * We first scan for subtrees and update them; we start by
	 * marking existing subtrees -- the ones that are unmarked
	 * should not be in the result.
	 */
	for (i = 0; i < it->subtree_nr; i++)
		it->down[i]->used = 0;

	/*
	 * Find the subtrees and update them.
	 */
	for (i = 0; i < entries; i++) {
		struct cache_entry *ce = cache[i];
		struct cache_tree_sub *sub;
		const char *path, *slash;
		int pathlen, sublen, subcnt;

		path = ce->name;
		pathlen = ce_namelen(ce);
		if (pathlen <= baselen || memcmp(base, path, baselen))
			break; /* at the end of this level */

		slash = strchr(path + baselen, '/');
		if (!slash)
			continue;
		/*
		 * a/bbb/c (base = a/, slash = /c)
		 * ==>
		 * path+baselen = bbb/c, sublen = 3
		 */
		sublen = slash - (path + baselen);
		sub = find_subtree(it, path + baselen, sublen, 1);
		if (!sub->cache_tree)
			sub->cache_tree = cache_tree();
		subcnt = update_one(sub->cache_tree,
				    cache + i, entries - i,
				    path,
				    baselen + sublen + 1,
				    missing_ok,
				    dryrun);
		i += subcnt - 1;
		sub->used = 1;
	}

	discard_unused_subtrees(it);

	/*
	 * Then write out the tree object for this level.
	 */
	size = 8192;
	buffer = xmalloc(size);
	offset = 0;

	for (i = 0; i < entries; i++) {
		struct cache_entry *ce = cache[i];
		struct cache_tree_sub *sub;
		const char *path, *slash;
		int pathlen, entlen;
		const unsigned char *sha1;
		unsigned mode;

		path = ce->name;
		pathlen = ce_namelen(ce);
		if (pathlen <= baselen || memcmp(base, path, baselen))
			break; /* at the end of this level */

		slash = strchr(path + baselen, '/');
		if (slash) {
			entlen = slash - (path + baselen);
			sub = find_subtree(it, path + baselen, entlen, 0);
			if (!sub)
				die("cache-tree.c: '%.*s' in '%s' not found",
				    entlen, path + baselen, path);
			i += sub->cache_tree->entry_count - 1;
			sha1 = sub->cache_tree->sha1;
			mode = S_IFDIR;
		}
		else {
			sha1 = ce->sha1;
			mode = ntohl(ce->ce_mode);
			entlen = pathlen - baselen;
		}
		if (!missing_ok && !has_sha1_file(sha1))
			return error("invalid object %s", sha1_to_hex(sha1));

		if (!ce->ce_mode)
			continue; /* entry being removed */

		if (size < offset + entlen + 100) {
			size = alloc_nr(offset + entlen + 100);
			buffer = xrealloc(buffer, size);
		}
		offset += sprintf(buffer + offset,
				  "%o %.*s", mode, entlen, path + baselen);
		buffer[offset++] = 0;
		hashcpy((unsigned char*)buffer + offset, sha1);
		offset += 20;

#if DEBUG
		fprintf(stderr, "cache-tree update-one %o %.*s\n",
			mode, entlen, path + baselen);
#endif
	}

	if (dryrun)
		hash_sha1_file(buffer, offset, tree_type, it->sha1);
	else
		write_sha1_file(buffer, offset, tree_type, it->sha1);
	free(buffer);
	it->entry_count = i;
#if DEBUG
	fprintf(stderr, "cache-tree update-one (%d ent, %d subtree) %s\n",
		it->entry_count, it->subtree_nr,
		sha1_to_hex(it->sha1));
#endif
	return i;
}

int cache_tree_update(struct cache_tree *it,
		      struct cache_entry **cache,
		      int entries,
		      int missing_ok,
		      int dryrun)
{
	int i;
	i = verify_cache(cache, entries);
	if (i)
		return i;
	i = update_one(it, cache, entries, "", 0, missing_ok, dryrun);
	if (i < 0)
		return i;
	return 0;
}

static void *write_one(struct cache_tree *it,
		       char *path,
		       int pathlen,
		       char *buffer,
		       unsigned long *size,
		       unsigned long *offset)
{
	int i;

	/* One "cache-tree" entry consists of the following:
	 * path (NUL terminated)
	 * entry_count, subtree_nr ("%d %d\n")
	 * tree-sha1 (missing if invalid)
	 * subtree_nr "cache-tree" entries for subtrees.
	 */
	if (*size < *offset + pathlen + 100) {
		*size = alloc_nr(*offset + pathlen + 100);
		buffer = xrealloc(buffer, *size);
	}
	*offset += sprintf(buffer + *offset, "%.*s%c%d %d\n",
			   pathlen, path, 0,
			   it->entry_count, it->subtree_nr);

#if DEBUG
	if (0 <= it->entry_count)
		fprintf(stderr, "cache-tree <%.*s> (%d ent, %d subtree) %s\n",
			pathlen, path, it->entry_count, it->subtree_nr,
			sha1_to_hex(it->sha1));
	else
		fprintf(stderr, "cache-tree <%.*s> (%d subtree) invalid\n",
			pathlen, path, it->subtree_nr);
#endif

	if (0 <= it->entry_count) {
		hashcpy((unsigned char*)buffer + *offset, it->sha1);
		*offset += 20;
	}
	for (i = 0; i < it->subtree_nr; i++) {
		struct cache_tree_sub *down = it->down[i];
		if (i) {
			struct cache_tree_sub *prev = it->down[i-1];
			if (subtree_name_cmp(down->name, down->namelen,
					     prev->name, prev->namelen) <= 0)
				die("fatal - unsorted cache subtree");
		}
		buffer = write_one(down->cache_tree, down->name, down->namelen,
				   buffer, size, offset);
	}
	return buffer;
}

void *cache_tree_write(struct cache_tree *root, unsigned long *size_p)
{
	char path[PATH_MAX];
	unsigned long size = 8192;
	char *buffer = xmalloc(size);

	*size_p = 0;
	path[0] = 0;
	return write_one(root, path, 0, buffer, &size, size_p);
}

static struct cache_tree *read_one(const char **buffer, unsigned long *size_p)
{
	const char *buf = *buffer;
	unsigned long size = *size_p;
	const char *cp;
	char *ep;
	struct cache_tree *it;
	int i, subtree_nr;

	it = NULL;
	/* skip name, but make sure name exists */
	while (size && *buf) {
		size--;
		buf++;
	}
	if (!size)
		goto free_return;
	buf++; size--;
	it = cache_tree();

	cp = buf;
	it->entry_count = strtol(cp, &ep, 10);
	if (cp == ep)
		goto free_return;
	cp = ep;
	subtree_nr = strtol(cp, &ep, 10);
	if (cp == ep)
		goto free_return;
	while (size && *buf && *buf != '\n') {
		size--;
		buf++;
	}
	if (!size)
		goto free_return;
	buf++; size--;
	if (0 <= it->entry_count) {
		if (size < 20)
			goto free_return;
		hashcpy(it->sha1, (unsigned char*)buf);
		buf += 20;
		size -= 20;
	}

#if DEBUG
	if (0 <= it->entry_count)
		fprintf(stderr, "cache-tree <%s> (%d ent, %d subtree) %s\n",
			*buffer, it->entry_count, subtree_nr,
			sha1_to_hex(it->sha1));
	else
		fprintf(stderr, "cache-tree <%s> (%d subtrees) invalid\n",
			*buffer, subtree_nr);
#endif

	/*
	 * Just a heuristic -- we do not add directories that often but
	 * we do not want to have to extend it immediately when we do,
	 * hence +2.
	 */
	it->subtree_alloc = subtree_nr + 2;
	it->down = xcalloc(it->subtree_alloc, sizeof(struct cache_tree_sub *));
	for (i = 0; i < subtree_nr; i++) {
		/* read each subtree */
		struct cache_tree *sub;
		struct cache_tree_sub *subtree;
		const char *name = buf;

		sub = read_one(&buf, &size);
		if (!sub)
			goto free_return;
		subtree = cache_tree_sub(it, name);
		subtree->cache_tree = sub;
	}
	if (subtree_nr != it->subtree_nr)
		die("cache-tree: internal error");
	*buffer = buf;
	*size_p = size;
	return it;

 free_return:
	cache_tree_free(&it);
	return NULL;
}

struct cache_tree *cache_tree_read(const char *buffer, unsigned long size)
{
	if (buffer[0])
		return NULL; /* not the whole tree */
	return read_one(&buffer, &size);
}

struct cache_tree *cache_tree_find(struct cache_tree *it, const char *path)
{
	while (*path) {
		const char *slash;
		struct cache_tree_sub *sub;

		slash = strchr(path, '/');
		if (!slash)
			slash = path + strlen(path);
		/* between path and slash is the name of the
		 * subtree to look for.
		 */
		sub = find_subtree(it, path, slash - path, 0);
		if (!sub)
			return NULL;
		it = sub->cache_tree;
		if (slash)
			while (*slash && *slash == '/')
				slash++;
		if (!slash || !*slash)
			return it; /* prefix ended with slashes */
		path = slash;
	}
	return it;
}
