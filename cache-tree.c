#include "cache.h"
#include "lockfile.h"
#include "tree.h"
#include "tree-walk.h"
#include "cache-tree.h"

#ifndef DEBUG
#define DEBUG 0
#endif

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
		if (it->down[i]) {
			cache_tree_free(&it->down[i]->cache_tree);
			free(it->down[i]);
		}
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
	ALLOC_GROW(it->down, it->subtree_nr + 1, it->subtree_alloc);
	it->subtree_nr++;

	FLEX_ALLOC_MEM(down, name, path, pathlen);
	down->cache_tree = NULL;
	down->namelen = pathlen;

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

static int do_invalidate_path(struct cache_tree *it, const char *path)
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
		return 0;
	slash = strchrnul(path, '/');
	namelen = slash - path;
	it->entry_count = -1;
	if (!*slash) {
		int pos;
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
		return 1;
	}
	down = find_subtree(it, path, namelen, 0);
	if (down)
		do_invalidate_path(down->cache_tree, slash + 1);
	return 1;
}

void cache_tree_invalidate_path(struct index_state *istate, const char *path)
{
	if (do_invalidate_path(istate->cache_tree, path))
		istate->cache_changed |= CACHE_TREE_CHANGED;
}

static int verify_cache(struct cache_entry **cache,
			int entries, int flags)
{
	int i, funny;
	int silent = flags & WRITE_TREE_SILENT;

	/* Verify that the tree is merged */
	funny = 0;
	for (i = 0; i < entries; i++) {
		const struct cache_entry *ce = cache[i];
		if (ce_stage(ce)) {
			if (silent)
				return -1;
			if (10 < ++funny) {
				fprintf(stderr, "...\n");
				break;
			}
			fprintf(stderr, "%s: unmerged (%s)\n",
				ce->name, oid_to_hex(&ce->oid));
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
		      int *skip_count,
		      int flags)
{
	struct strbuf buffer;
	int missing_ok = flags & WRITE_TREE_MISSING_OK;
	int dryrun = flags & WRITE_TREE_DRY_RUN;
	int repair = flags & WRITE_TREE_REPAIR;
	int to_invalidate = 0;
	int i;

	assert(!(dryrun && repair));

	*skip_count = 0;

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
	i = 0;
	while (i < entries) {
		const struct cache_entry *ce = cache[i];
		struct cache_tree_sub *sub;
		const char *path, *slash;
		int pathlen, sublen, subcnt, subskip;

		path = ce->name;
		pathlen = ce_namelen(ce);
		if (pathlen <= baselen || memcmp(base, path, baselen))
			break; /* at the end of this level */

		slash = strchr(path + baselen, '/');
		if (!slash) {
			i++;
			continue;
		}
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
				    &subskip,
				    flags);
		if (subcnt < 0)
			return subcnt;
		if (!subcnt)
			die("index cache-tree records empty sub-tree");
		i += subcnt;
		sub->count = subcnt; /* to be used in the next loop */
		*skip_count += subskip;
		sub->used = 1;
	}

	discard_unused_subtrees(it);

	/*
	 * Then write out the tree object for this level.
	 */
	strbuf_init(&buffer, 8192);

	i = 0;
	while (i < entries) {
		const struct cache_entry *ce = cache[i];
		struct cache_tree_sub *sub = NULL;
		const char *path, *slash;
		int pathlen, entlen;
		const unsigned char *sha1;
		unsigned mode;
		int expected_missing = 0;
		int contains_ita = 0;

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
			i += sub->count;
			sha1 = sub->cache_tree->sha1;
			mode = S_IFDIR;
			contains_ita = sub->cache_tree->entry_count < 0;
			if (contains_ita) {
				to_invalidate = 1;
				expected_missing = 1;
			}
		}
		else {
			sha1 = ce->oid.hash;
			mode = ce->ce_mode;
			entlen = pathlen - baselen;
			i++;
		}
		if (mode != S_IFGITLINK && !missing_ok && !has_sha1_file(sha1)) {
			strbuf_release(&buffer);
			if (expected_missing)
				return -1;
			return error("invalid object %06o %s for '%.*s'",
				mode, sha1_to_hex(sha1), entlen+baselen, path);
		}

		/*
		 * CE_REMOVE entries are removed before the index is
		 * written to disk. Skip them to remain consistent
		 * with the future on-disk index.
		 */
		if (ce->ce_flags & CE_REMOVE) {
			*skip_count = *skip_count + 1;
			continue;
		}

		/*
		 * CE_INTENT_TO_ADD entries exist on on-disk index but
		 * they are not part of generated trees. Invalidate up
		 * to root to force cache-tree users to read elsewhere.
		 */
		if (!sub && ce_intent_to_add(ce)) {
			to_invalidate = 1;
			continue;
		}

		/*
		 * "sub" can be an empty tree if all subentries are i-t-a.
		 */
		if (contains_ita && !hashcmp(sha1, EMPTY_TREE_SHA1_BIN))
			continue;

		strbuf_grow(&buffer, entlen + 100);
		strbuf_addf(&buffer, "%o %.*s%c", mode, entlen, path + baselen, '\0');
		strbuf_add(&buffer, sha1, 20);

#if DEBUG
		fprintf(stderr, "cache-tree update-one %o %.*s\n",
			mode, entlen, path + baselen);
#endif
	}

	if (repair) {
		unsigned char sha1[20];
		hash_sha1_file(buffer.buf, buffer.len, tree_type, sha1);
		if (has_sha1_file(sha1))
			hashcpy(it->sha1, sha1);
		else
			to_invalidate = 1;
	} else if (dryrun)
		hash_sha1_file(buffer.buf, buffer.len, tree_type, it->sha1);
	else if (write_sha1_file(buffer.buf, buffer.len, tree_type, it->sha1)) {
		strbuf_release(&buffer);
		return -1;
	}

	strbuf_release(&buffer);
	it->entry_count = to_invalidate ? -1 : i - *skip_count;
#if DEBUG
	fprintf(stderr, "cache-tree update-one (%d ent, %d subtree) %s\n",
		it->entry_count, it->subtree_nr,
		sha1_to_hex(it->sha1));
#endif
	return i;
}

int cache_tree_update(struct index_state *istate, int flags)
{
	struct cache_tree *it = istate->cache_tree;
	struct cache_entry **cache = istate->cache;
	int entries = istate->cache_nr;
	int skip, i = verify_cache(cache, entries, flags);

	if (i)
		return i;
	i = update_one(it, cache, entries, "", 0, &skip, flags);
	if (i < 0)
		return i;
	istate->cache_changed |= CACHE_TREE_CHANGED;
	return 0;
}

static void write_one(struct strbuf *buffer, struct cache_tree *it,
                      const char *path, int pathlen)
{
	int i;

	/* One "cache-tree" entry consists of the following:
	 * path (NUL terminated)
	 * entry_count, subtree_nr ("%d %d\n")
	 * tree-sha1 (missing if invalid)
	 * subtree_nr "cache-tree" entries for subtrees.
	 */
	strbuf_grow(buffer, pathlen + 100);
	strbuf_add(buffer, path, pathlen);
	strbuf_addf(buffer, "%c%d %d\n", 0, it->entry_count, it->subtree_nr);

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
		strbuf_add(buffer, it->sha1, 20);
	}
	for (i = 0; i < it->subtree_nr; i++) {
		struct cache_tree_sub *down = it->down[i];
		if (i) {
			struct cache_tree_sub *prev = it->down[i-1];
			if (subtree_name_cmp(down->name, down->namelen,
					     prev->name, prev->namelen) <= 0)
				die("fatal - unsorted cache subtree");
		}
		write_one(buffer, down->cache_tree, down->name, down->namelen);
	}
}

void cache_tree_write(struct strbuf *sb, struct cache_tree *root)
{
	write_one(sb, root, "", 0);
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
		hashcpy(it->sha1, (const unsigned char*)buf);
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

static struct cache_tree *cache_tree_find(struct cache_tree *it, const char *path)
{
	if (!it)
		return NULL;
	while (*path) {
		const char *slash;
		struct cache_tree_sub *sub;

		slash = strchrnul(path, '/');
		/*
		 * Between path and slash is the name of the subtree
		 * to look for.
		 */
		sub = find_subtree(it, path, slash - path, 0);
		if (!sub)
			return NULL;
		it = sub->cache_tree;

		path = slash;
		while (*path == '/')
			path++;
	}
	return it;
}

int write_index_as_tree(unsigned char *sha1, struct index_state *index_state, const char *index_path, int flags, const char *prefix)
{
	int entries, was_valid, newfd;
	struct lock_file *lock_file;

	/*
	 * We can't free this memory, it becomes part of a linked list
	 * parsed atexit()
	 */
	lock_file = xcalloc(1, sizeof(struct lock_file));

	newfd = hold_lock_file_for_update(lock_file, index_path, LOCK_DIE_ON_ERROR);

	entries = read_index_from(index_state, index_path);
	if (entries < 0)
		return WRITE_TREE_UNREADABLE_INDEX;
	if (flags & WRITE_TREE_IGNORE_CACHE_TREE)
		cache_tree_free(&index_state->cache_tree);

	if (!index_state->cache_tree)
		index_state->cache_tree = cache_tree();

	was_valid = cache_tree_fully_valid(index_state->cache_tree);
	if (!was_valid) {
		if (cache_tree_update(index_state, flags) < 0)
			return WRITE_TREE_UNMERGED_INDEX;
		if (0 <= newfd) {
			if (!write_locked_index(index_state, lock_file, COMMIT_LOCK))
				newfd = -1;
		}
		/* Not being able to write is fine -- we are only interested
		 * in updating the cache-tree part, and if the next caller
		 * ends up using the old index with unupdated cache-tree part
		 * it misses the work we did here, but that is just a
		 * performance penalty and not a big deal.
		 */
	}

	if (prefix) {
		struct cache_tree *subtree;
		subtree = cache_tree_find(index_state->cache_tree, prefix);
		if (!subtree)
			return WRITE_TREE_PREFIX_ERROR;
		hashcpy(sha1, subtree->sha1);
	}
	else
		hashcpy(sha1, index_state->cache_tree->sha1);

	if (0 <= newfd)
		rollback_lock_file(lock_file);

	return 0;
}

int write_cache_as_tree(unsigned char *sha1, int flags, const char *prefix)
{
	return write_index_as_tree(sha1, &the_index, get_index_file(), flags, prefix);
}

static void prime_cache_tree_rec(struct cache_tree *it, struct tree *tree)
{
	struct tree_desc desc;
	struct name_entry entry;
	int cnt;

	hashcpy(it->sha1, tree->object.oid.hash);
	init_tree_desc(&desc, tree->buffer, tree->size);
	cnt = 0;
	while (tree_entry(&desc, &entry)) {
		if (!S_ISDIR(entry.mode))
			cnt++;
		else {
			struct cache_tree_sub *sub;
			struct tree *subtree = lookup_tree(entry.oid->hash);
			if (!subtree->object.parsed)
				parse_tree(subtree);
			sub = cache_tree_sub(it, entry.path);
			sub->cache_tree = cache_tree();
			prime_cache_tree_rec(sub->cache_tree, subtree);
			cnt += sub->cache_tree->entry_count;
		}
	}
	it->entry_count = cnt;
}

void prime_cache_tree(struct index_state *istate, struct tree *tree)
{
	cache_tree_free(&istate->cache_tree);
	istate->cache_tree = cache_tree();
	prime_cache_tree_rec(istate->cache_tree, tree);
	istate->cache_changed |= CACHE_TREE_CHANGED;
}

/*
 * find the cache_tree that corresponds to the current level without
 * exploding the full path into textual form.  The root of the
 * cache tree is given as "root", and our current level is "info".
 * (1) When at root level, info->prev is NULL, so it is "root" itself.
 * (2) Otherwise, find the cache_tree that corresponds to one level
 *     above us, and find ourselves in there.
 */
static struct cache_tree *find_cache_tree_from_traversal(struct cache_tree *root,
							 struct traverse_info *info)
{
	struct cache_tree *our_parent;

	if (!info->prev)
		return root;
	our_parent = find_cache_tree_from_traversal(root, info->prev);
	return cache_tree_find(our_parent, info->name.path);
}

int cache_tree_matches_traversal(struct cache_tree *root,
				 struct name_entry *ent,
				 struct traverse_info *info)
{
	struct cache_tree *it;

	it = find_cache_tree_from_traversal(root, info);
	it = cache_tree_find(it, ent->path);
	if (it && it->entry_count > 0 && !hashcmp(ent->oid->hash, it->sha1))
		return it->entry_count;
	return 0;
}

int update_main_cache_tree(int flags)
{
	if (!the_index.cache_tree)
		the_index.cache_tree = cache_tree();
	return cache_tree_update(&the_index, flags);
}
