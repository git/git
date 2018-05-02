#include "cache.h"
#include "tree-walk.h"
#include "unpack-trees.h"
#include "dir.h"
#include "tree.h"
#include "pathspec.h"

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

static int decode_tree_entry(struct tree_desc *desc, const char *buf, unsigned long size, struct strbuf *err)
{
	const char *path;
	unsigned int mode, len;

	if (size < 23 || buf[size - 21]) {
		strbuf_addstr(err, _("too-short tree object"));
		return -1;
	}

	path = get_mode(buf, &mode);
	if (!path) {
		strbuf_addstr(err, _("malformed mode in tree entry"));
		return -1;
	}
	if (!*path) {
		strbuf_addstr(err, _("empty filename in tree entry"));
		return -1;
	}
	len = strlen(path) + 1;

	/* Initialize the descriptor entry */
	desc->entry.path = path;
	desc->entry.mode = canon_mode(mode);
	desc->entry.oid  = (const struct object_id *)(path + len);

	return 0;
}

static int init_tree_desc_internal(struct tree_desc *desc, const void *buffer, unsigned long size, struct strbuf *err)
{
	desc->buffer = buffer;
	desc->size = size;
	if (size)
		return decode_tree_entry(desc, buffer, size, err);
	return 0;
}

void init_tree_desc(struct tree_desc *desc, const void *buffer, unsigned long size)
{
	struct strbuf err = STRBUF_INIT;
	if (init_tree_desc_internal(desc, buffer, size, &err))
		die("%s", err.buf);
	strbuf_release(&err);
}

int init_tree_desc_gently(struct tree_desc *desc, const void *buffer, unsigned long size)
{
	struct strbuf err = STRBUF_INIT;
	int result = init_tree_desc_internal(desc, buffer, size, &err);
	if (result)
		error("%s", err.buf);
	strbuf_release(&err);
	return result;
}

void *fill_tree_descriptor(struct tree_desc *desc, const struct object_id *oid)
{
	unsigned long size = 0;
	void *buf = NULL;

	if (oid) {
		buf = read_object_with_reference(oid, tree_type, &size, NULL);
		if (!buf)
			die("unable to read tree %s", oid_to_hex(oid));
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

static int update_tree_entry_internal(struct tree_desc *desc, struct strbuf *err)
{
	const void *buf = desc->buffer;
	const unsigned char *end = desc->entry.oid->hash + the_hash_algo->rawsz;
	unsigned long size = desc->size;
	unsigned long len = end - (const unsigned char *)buf;

	if (size < len)
		die(_("too-short tree file"));
	buf = end;
	size -= len;
	desc->buffer = buf;
	desc->size = size;
	if (size)
		return decode_tree_entry(desc, buf, size, err);
	return 0;
}

void update_tree_entry(struct tree_desc *desc)
{
	struct strbuf err = STRBUF_INIT;
	if (update_tree_entry_internal(desc, &err))
		die("%s", err.buf);
	strbuf_release(&err);
}

int update_tree_entry_gently(struct tree_desc *desc)
{
	struct strbuf err = STRBUF_INIT;
	if (update_tree_entry_internal(desc, &err)) {
		error("%s", err.buf);
		strbuf_release(&err);
		/* Stop processing this tree after error */
		desc->size = 0;
		return -1;
	}
	strbuf_release(&err);
	return 0;
}

int tree_entry(struct tree_desc *desc, struct name_entry *entry)
{
	if (!desc->size)
		return 0;

	*entry = desc->entry;
	update_tree_entry(desc);
	return 1;
}

int tree_entry_gently(struct tree_desc *desc, struct name_entry *entry)
{
	if (!desc->size)
		return 0;

	*entry = desc->entry;
	if (update_tree_entry_gently(desc))
		return 0;
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
	info->name.oid = (void *)(base + pathlen + 1);
	if (pathlen)
		info->prev = &dummy;
}

char *make_traverse_path(char *path, const struct traverse_info *info, const struct name_entry *n)
{
	int len = tree_entry_len(n);
	int pathlen = info->pathlen;

	path[pathlen + len] = 0;
	for (;;) {
		memcpy(path + pathlen, n->path, len);
		if (!pathlen)
			break;
		path[--pathlen] = '/';
		n = &info->name;
		len = tree_entry_len(n);
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
	len = tree_entry_len(a);
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
		len = tree_entry_len(a);
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

static inline int prune_traversal(struct name_entry *e,
				  struct traverse_info *info,
				  struct strbuf *base,
				  int still_interesting)
{
	if (!info->pathspec || still_interesting == 2)
		return 2;
	if (still_interesting < 0)
		return still_interesting;
	return tree_entry_interesting(e, base, 0, info->pathspec);
}

int traverse_trees(int n, struct tree_desc *t, struct traverse_info *info)
{
	int error = 0;
	struct name_entry *entry = xmalloc(n*sizeof(*entry));
	int i;
	struct tree_desc_x *tx = xcalloc(n, sizeof(*tx));
	struct strbuf base = STRBUF_INIT;
	int interesting = 1;
	char *traverse_path;

	for (i = 0; i < n; i++)
		tx[i].d = t[i];

	if (info->prev) {
		strbuf_grow(&base, info->pathlen);
		make_traverse_path(base.buf, info->prev, &info->name);
		base.buf[info->pathlen-1] = '/';
		strbuf_setlen(&base, info->pathlen);
		traverse_path = xstrndup(base.buf, info->pathlen);
	} else {
		traverse_path = xstrndup(info->name.path, info->pathlen);
	}
	info->traverse_path = traverse_path;
	for (;;) {
		int trees_used;
		unsigned long mask, dirmask;
		const char *first = NULL;
		int first_len = 0;
		struct name_entry *e = NULL;
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
			len = tree_entry_len(e);
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
				len = tree_entry_len(e);
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
			e = &entry[i];
		}
		if (!mask)
			break;
		interesting = prune_traversal(e, info, &base, interesting);
		if (interesting < 0)
			break;
		if (interesting) {
			trees_used = info->fn(n, mask, dirmask, entry, info);
			if (trees_used < 0) {
				error = trees_used;
				if (!info->show_all_errors)
					break;
			}
			mask &= trees_used;
		}
		for (i = 0; i < n; i++)
			if (mask & (1ul << i))
				update_extended_entry(tx + i, entry + i);
	}
	free(entry);
	for (i = 0; i < n; i++)
		free_extended_entry(tx + i);
	free(tx);
	free(traverse_path);
	info->traverse_path = NULL;
	strbuf_release(&base);
	return error;
}

struct dir_state {
	void *tree;
	unsigned long size;
	struct object_id oid;
};

static int find_tree_entry(struct tree_desc *t, const char *name, struct object_id *result, unsigned *mode)
{
	int namelen = strlen(name);
	while (t->size) {
		const char *entry;
		const struct object_id *oid;
		int entrylen, cmp;

		oid = tree_entry_extract(t, &entry, mode);
		entrylen = tree_entry_len(&t->entry);
		update_tree_entry(t);
		if (entrylen > namelen)
			continue;
		cmp = memcmp(name, entry, entrylen);
		if (cmp > 0)
			continue;
		if (cmp < 0)
			break;
		if (entrylen == namelen) {
			oidcpy(result, oid);
			return 0;
		}
		if (name[entrylen] != '/')
			continue;
		if (!S_ISDIR(*mode))
			break;
		if (++entrylen == namelen) {
			oidcpy(result, oid);
			return 0;
		}
		return get_tree_entry(oid, name + entrylen, result, mode);
	}
	return -1;
}

int get_tree_entry(const struct object_id *tree_oid, const char *name, struct object_id *oid, unsigned *mode)
{
	int retval;
	void *tree;
	unsigned long size;
	struct object_id root;

	tree = read_object_with_reference(tree_oid, tree_type, &size, &root);
	if (!tree)
		return -1;

	if (name[0] == '\0') {
		oidcpy(oid, &root);
		free(tree);
		return 0;
	}

	if (!size) {
		retval = -1;
	} else {
		struct tree_desc t;
		init_tree_desc(&t, tree, size);
		retval = find_tree_entry(&t, name, oid, mode);
	}
	free(tree);
	return retval;
}

/*
 * This is Linux's built-in max for the number of symlinks to follow.
 * That limit, of course, does not affect git, but it's a reasonable
 * choice.
 */
#define GET_TREE_ENTRY_FOLLOW_SYMLINKS_MAX_LINKS 40

/**
 * Find a tree entry by following symlinks in tree_sha (which is
 * assumed to be the root of the repository).  In the event that a
 * symlink points outside the repository (e.g. a link to /foo or a
 * root-level link to ../foo), the portion of the link which is
 * outside the repository will be returned in result_path, and *mode
 * will be set to 0.  It is assumed that result_path is uninitialized.
 * If there are no symlinks, or the end result of the symlink chain
 * points to an object inside the repository, result will be filled in
 * with the sha1 of the found object, and *mode will hold the mode of
 * the object.
 *
 * See the code for enum follow_symlink_result for a description of
 * the return values.
 */
enum follow_symlinks_result get_tree_entry_follow_symlinks(struct object_id *tree_oid, const char *name, struct object_id *result, struct strbuf *result_path, unsigned *mode)
{
	int retval = MISSING_OBJECT;
	struct dir_state *parents = NULL;
	size_t parents_alloc = 0;
	size_t i, parents_nr = 0;
	struct object_id current_tree_oid;
	struct strbuf namebuf = STRBUF_INIT;
	struct tree_desc t;
	int follows_remaining = GET_TREE_ENTRY_FOLLOW_SYMLINKS_MAX_LINKS;

	init_tree_desc(&t, NULL, 0UL);
	strbuf_addstr(&namebuf, name);
	oidcpy(&current_tree_oid, tree_oid);

	while (1) {
		int find_result;
		char *first_slash;
		char *remainder = NULL;

		if (!t.buffer) {
			void *tree;
			struct object_id root;
			unsigned long size;
			tree = read_object_with_reference(&current_tree_oid,
							  tree_type, &size,
							  &root);
			if (!tree)
				goto done;

			ALLOC_GROW(parents, parents_nr + 1, parents_alloc);
			parents[parents_nr].tree = tree;
			parents[parents_nr].size = size;
			oidcpy(&parents[parents_nr].oid, &root);
			parents_nr++;

			if (namebuf.buf[0] == '\0') {
				oidcpy(result, &root);
				retval = FOUND;
				goto done;
			}

			if (!size)
				goto done;

			/* descend */
			init_tree_desc(&t, tree, size);
		}

		/* Handle symlinks to e.g. a//b by removing leading slashes */
		while (namebuf.buf[0] == '/') {
			strbuf_remove(&namebuf, 0, 1);
		}

		/* Split namebuf into a first component and a remainder */
		if ((first_slash = strchr(namebuf.buf, '/'))) {
			*first_slash = 0;
			remainder = first_slash + 1;
		}

		if (!strcmp(namebuf.buf, "..")) {
			struct dir_state *parent;
			/*
			 * We could end up with .. in the namebuf if it
			 * appears in a symlink.
			 */

			if (parents_nr == 1) {
				if (remainder)
					*first_slash = '/';
				strbuf_add(result_path, namebuf.buf,
					   namebuf.len);
				*mode = 0;
				retval = FOUND;
				goto done;
			}
			parent = &parents[parents_nr - 1];
			free(parent->tree);
			parents_nr--;
			parent = &parents[parents_nr - 1];
			init_tree_desc(&t, parent->tree, parent->size);
			strbuf_remove(&namebuf, 0, remainder ? 3 : 2);
			continue;
		}

		/* We could end up here via a symlink to dir/.. */
		if (namebuf.buf[0] == '\0') {
			oidcpy(result, &parents[parents_nr - 1].oid);
			retval = FOUND;
			goto done;
		}

		/* Look up the first (or only) path component in the tree. */
		find_result = find_tree_entry(&t, namebuf.buf,
					      &current_tree_oid, mode);
		if (find_result) {
			goto done;
		}

		if (S_ISDIR(*mode)) {
			if (!remainder) {
				oidcpy(result, &current_tree_oid);
				retval = FOUND;
				goto done;
			}
			/* Descend the tree */
			t.buffer = NULL;
			strbuf_remove(&namebuf, 0,
				      1 + first_slash - namebuf.buf);
		} else if (S_ISREG(*mode)) {
			if (!remainder) {
				oidcpy(result, &current_tree_oid);
				retval = FOUND;
			} else {
				retval = NOT_DIR;
			}
			goto done;
		} else if (S_ISLNK(*mode)) {
			/* Follow a symlink */
			unsigned long link_len;
			size_t len;
			char *contents, *contents_start;
			struct dir_state *parent;
			enum object_type type;

			if (follows_remaining-- == 0) {
				/* Too many symlinks followed */
				retval = SYMLINK_LOOP;
				goto done;
			}

			/*
			 * At this point, we have followed at a least
			 * one symlink, so on error we need to report this.
			 */
			retval = DANGLING_SYMLINK;

			contents = read_object_file(&current_tree_oid, &type,
						    &link_len);

			if (!contents)
				goto done;

			if (contents[0] == '/') {
				strbuf_addstr(result_path, contents);
				free(contents);
				*mode = 0;
				retval = FOUND;
				goto done;
			}

			if (remainder)
				len = first_slash - namebuf.buf;
			else
				len = namebuf.len;

			contents_start = contents;

			parent = &parents[parents_nr - 1];
			init_tree_desc(&t, parent->tree, parent->size);
			strbuf_splice(&namebuf, 0, len,
				      contents_start, link_len);
			if (remainder)
				namebuf.buf[link_len] = '/';
			free(contents);
		}
	}
done:
	for (i = 0; i < parents_nr; i++)
		free(parents[i].tree);
	free(parents);

	strbuf_release(&namebuf);
	return retval;
}

static int match_entry(const struct pathspec_item *item,
		       const struct name_entry *entry, int pathlen,
		       const char *match, int matchlen,
		       enum interesting *never_interesting)
{
	int m = -1; /* signals that we haven't called strncmp() */

	if (item->magic & PATHSPEC_ICASE)
		/*
		 * "Never interesting" trick requires exact
		 * matching. We could do something clever with inexact
		 * matching, but it's trickier (and not to forget that
		 * strcasecmp is locale-dependent, at least in
		 * glibc). Just disable it for now. It can't be worse
		 * than the wildcard's codepath of '[Tt][Hi][Is][Ss]'
		 * pattern.
		 */
		*never_interesting = entry_not_interesting;
	else if (*never_interesting != entry_not_interesting) {
		/*
		 * We have not seen any match that sorts later
		 * than the current path.
		 */

		/*
		 * Does match sort strictly earlier than path
		 * with their common parts?
		 */
		m = strncmp(match, entry->path,
			    (matchlen < pathlen) ? matchlen : pathlen);
		if (m < 0)
			return 0;

		/*
		 * If we come here even once, that means there is at
		 * least one pathspec that would sort equal to or
		 * later than the path we are currently looking at.
		 * In other words, if we have never reached this point
		 * after iterating all pathspecs, it means all
		 * pathspecs are either outside of base, or inside the
		 * base but sorts strictly earlier than the current
		 * one.  In either case, they will never match the
		 * subsequent entries.  In such a case, we initialized
		 * the variable to -1 and that is what will be
		 * returned, allowing the caller to terminate early.
		 */
		*never_interesting = entry_not_interesting;
	}

	if (pathlen > matchlen)
		return 0;

	if (matchlen > pathlen) {
		if (match[pathlen] != '/')
			return 0;
		if (!S_ISDIR(entry->mode) && !S_ISGITLINK(entry->mode))
			return 0;
	}

	if (m == -1)
		/*
		 * we cheated and did not do strncmp(), so we do
		 * that here.
		 */
		m = ps_strncmp(item, match, entry->path, pathlen);

	/*
	 * If common part matched earlier then it is a hit,
	 * because we rejected the case where path is not a
	 * leading directory and is shorter than match.
	 */
	if (!m)
		/*
		 * match_entry does not check if the prefix part is
		 * matched case-sensitively. If the entry is a
		 * directory and part of prefix, it'll be rematched
		 * eventually by basecmp with special treatment for
		 * the prefix.
		 */
		return 1;

	return 0;
}

/* :(icase)-aware string compare */
static int basecmp(const struct pathspec_item *item,
		   const char *base, const char *match, int len)
{
	if (item->magic & PATHSPEC_ICASE) {
		int ret, n = len > item->prefix ? item->prefix : len;
		ret = strncmp(base, match, n);
		if (ret)
			return ret;
		base += n;
		match += n;
		len -= n;
	}
	return ps_strncmp(item, base, match, len);
}

static int match_dir_prefix(const struct pathspec_item *item,
			    const char *base,
			    const char *match, int matchlen)
{
	if (basecmp(item, base, match, matchlen))
		return 0;

	/*
	 * If the base is a subdirectory of a path which
	 * was specified, all of them are interesting.
	 */
	if (!matchlen ||
	    base[matchlen] == '/' ||
	    match[matchlen - 1] == '/')
		return 1;

	/* Just a random prefix match */
	return 0;
}

/*
 * Perform matching on the leading non-wildcard part of
 * pathspec. item->nowildcard_len must be greater than zero. Return
 * non-zero if base is matched.
 */
static int match_wildcard_base(const struct pathspec_item *item,
			       const char *base, int baselen,
			       int *matched)
{
	const char *match = item->match;
	/* the wildcard part is not considered in this function */
	int matchlen = item->nowildcard_len;

	if (baselen) {
		int dirlen;
		/*
		 * Return early if base is longer than the
		 * non-wildcard part but it does not match.
		 */
		if (baselen >= matchlen) {
			*matched = matchlen;
			return !basecmp(item, base, match, matchlen);
		}

		dirlen = matchlen;
		while (dirlen && match[dirlen - 1] != '/')
			dirlen--;

		/*
		 * Return early if base is shorter than the
		 * non-wildcard part but it does not match. Note that
		 * base ends with '/' so we are sure it really matches
		 * directory
		 */
		if (basecmp(item, base, match, baselen))
			return 0;
		*matched = baselen;
	} else
		*matched = 0;
	/*
	 * we could have checked entry against the non-wildcard part
	 * that is not in base and does similar never_interesting
	 * optimization as in match_entry. For now just be happy with
	 * base comparison.
	 */
	return entry_interesting;
}

/*
 * Is a tree entry interesting given the pathspec we have?
 *
 * Pre-condition: either baselen == base_offset (i.e. empty path)
 * or base[baselen-1] == '/' (i.e. with trailing slash).
 */
static enum interesting do_match(const struct name_entry *entry,
				 struct strbuf *base, int base_offset,
				 const struct pathspec *ps,
				 int exclude)
{
	int i;
	int pathlen, baselen = base->len - base_offset;
	enum interesting never_interesting = ps->has_wildcard ?
		entry_not_interesting : all_entries_not_interesting;

	GUARD_PATHSPEC(ps,
		       PATHSPEC_FROMTOP |
		       PATHSPEC_MAXDEPTH |
		       PATHSPEC_LITERAL |
		       PATHSPEC_GLOB |
		       PATHSPEC_ICASE |
		       PATHSPEC_EXCLUDE);

	if (!ps->nr) {
		if (!ps->recursive ||
		    !(ps->magic & PATHSPEC_MAXDEPTH) ||
		    ps->max_depth == -1)
			return all_entries_interesting;
		return within_depth(base->buf + base_offset, baselen,
				    !!S_ISDIR(entry->mode),
				    ps->max_depth) ?
			entry_interesting : entry_not_interesting;
	}

	pathlen = tree_entry_len(entry);

	for (i = ps->nr - 1; i >= 0; i--) {
		const struct pathspec_item *item = ps->items+i;
		const char *match = item->match;
		const char *base_str = base->buf + base_offset;
		int matchlen = item->len, matched = 0;

		if ((!exclude &&   item->magic & PATHSPEC_EXCLUDE) ||
		    ( exclude && !(item->magic & PATHSPEC_EXCLUDE)))
			continue;

		if (baselen >= matchlen) {
			/* If it doesn't match, move along... */
			if (!match_dir_prefix(item, base_str, match, matchlen))
				goto match_wildcards;

			if (!ps->recursive ||
			    !(ps->magic & PATHSPEC_MAXDEPTH) ||
			    ps->max_depth == -1)
				return all_entries_interesting;

			return within_depth(base_str + matchlen + 1,
					    baselen - matchlen - 1,
					    !!S_ISDIR(entry->mode),
					    ps->max_depth) ?
				entry_interesting : entry_not_interesting;
		}

		/* Either there must be no base, or the base must match. */
		if (baselen == 0 || !basecmp(item, base_str, match, baselen)) {
			if (match_entry(item, entry, pathlen,
					match + baselen, matchlen - baselen,
					&never_interesting))
				return entry_interesting;

			if (item->nowildcard_len < item->len) {
				if (!git_fnmatch(item, match + baselen, entry->path,
						 item->nowildcard_len - baselen))
					return entry_interesting;

				/*
				 * Match all directories. We'll try to
				 * match files later on.
				 */
				if (ps->recursive && S_ISDIR(entry->mode))
					return entry_interesting;

				/*
				 * When matching against submodules with
				 * wildcard characters, ensure that the entry
				 * at least matches up to the first wild
				 * character.  More accurate matching can then
				 * be performed in the submodule itself.
				 */
				if (ps->recurse_submodules &&
				    S_ISGITLINK(entry->mode) &&
				    !ps_strncmp(item, match + baselen,
						entry->path,
						item->nowildcard_len - baselen))
					return entry_interesting;
			}

			continue;
		}

match_wildcards:
		if (item->nowildcard_len == item->len)
			continue;

		if (item->nowildcard_len &&
		    !match_wildcard_base(item, base_str, baselen, &matched))
			continue;

		/*
		 * Concatenate base and entry->path into one and do
		 * fnmatch() on it.
		 *
		 * While we could avoid concatenation in certain cases
		 * [1], which saves a memcpy and potentially a
		 * realloc, it turns out not worth it. Measurement on
		 * linux-2.6 does not show any clear improvements,
		 * partly because of the nowildcard_len optimization
		 * in git_fnmatch(). Avoid micro-optimizations here.
		 *
		 * [1] if match_wildcard_base() says the base
		 * directory is already matched, we only need to match
		 * the rest, which is shorter so _in theory_ faster.
		 */

		strbuf_add(base, entry->path, pathlen);

		if (!git_fnmatch(item, match, base->buf + base_offset,
				 item->nowildcard_len)) {
			strbuf_setlen(base, base_offset + baselen);
			return entry_interesting;
		}

		/*
		 * When matching against submodules with
		 * wildcard characters, ensure that the entry
		 * at least matches up to the first wild
		 * character.  More accurate matching can then
		 * be performed in the submodule itself.
		 */
		if (ps->recurse_submodules && S_ISGITLINK(entry->mode) &&
		    !ps_strncmp(item, match, base->buf + base_offset,
				item->nowildcard_len)) {
			strbuf_setlen(base, base_offset + baselen);
			return entry_interesting;
		}

		strbuf_setlen(base, base_offset + baselen);

		/*
		 * Match all directories. We'll try to match files
		 * later on.
		 * max_depth is ignored but we may consider support it
		 * in future, see
		 * https://public-inbox.org/git/7vmxo5l2g4.fsf@alter.siamese.dyndns.org/
		 */
		if (ps->recursive && S_ISDIR(entry->mode))
			return entry_interesting;
	}
	return never_interesting; /* No matches */
}

/*
 * Is a tree entry interesting given the pathspec we have?
 *
 * Pre-condition: either baselen == base_offset (i.e. empty path)
 * or base[baselen-1] == '/' (i.e. with trailing slash).
 */
enum interesting tree_entry_interesting(const struct name_entry *entry,
					struct strbuf *base, int base_offset,
					const struct pathspec *ps)
{
	enum interesting positive, negative;
	positive = do_match(entry, base, base_offset, ps, 0);

	/*
	 * case | entry | positive | negative | result
	 * -----+-------+----------+----------+-------
	 *   1  |  file |   -1     |  -1..2   |  -1
	 *   2  |  file |    0     |  -1..2   |   0
	 *   3  |  file |    1     |   -1     |   1
	 *   4  |  file |    1     |    0     |   1
	 *   5  |  file |    1     |    1     |   0
	 *   6  |  file |    1     |    2     |   0
	 *   7  |  file |    2     |   -1     |   2
	 *   8  |  file |    2     |    0     |   2
	 *   9  |  file |    2     |    1     |   0
	 *  10  |  file |    2     |    2     |  -1
	 * -----+-------+----------+----------+-------
	 *  11  |  dir  |   -1     |  -1..2   |  -1
	 *  12  |  dir  |    0     |  -1..2   |   0
	 *  13  |  dir  |    1     |   -1     |   1
	 *  14  |  dir  |    1     |    0     |   1
	 *  15  |  dir  |    1     |    1     |   1 (*)
	 *  16  |  dir  |    1     |    2     |   0
	 *  17  |  dir  |    2     |   -1     |   2
	 *  18  |  dir  |    2     |    0     |   2
	 *  19  |  dir  |    2     |    1     |   1 (*)
	 *  20  |  dir  |    2     |    2     |  -1
	 *
	 * (*) An exclude pattern interested in a directory does not
	 * necessarily mean it will exclude all of the directory. In
	 * wildcard case, it can't decide until looking at individual
	 * files inside. So don't write such directories off yet.
	 */

	if (!(ps->magic & PATHSPEC_EXCLUDE) ||
	    positive <= entry_not_interesting) /* #1, #2, #11, #12 */
		return positive;

	negative = do_match(entry, base, base_offset, ps, 1);

	/* #3, #4, #7, #8, #13, #14, #17, #18 */
	if (negative <= entry_not_interesting)
		return positive;

	/* #15, #19 */
	if (S_ISDIR(entry->mode) &&
	    positive >= entry_interesting &&
	    negative == entry_interesting)
		return entry_interesting;

	if ((positive == entry_interesting &&
	     negative >= entry_interesting) || /* #5, #6, #16 */
	    (positive == all_entries_interesting &&
	     negative == entry_interesting)) /* #9 */
		return entry_not_interesting;

	return all_entries_not_interesting; /* #10, #20 */
}
