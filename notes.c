#include "git-compat-util.h"
#include "config.h"
#include "environment.h"
#include "hex.h"
#include "notes.h"
#include "object-name.h"
#include "object-store.h"
#include "blob.h"
#include "tree.h"
#include "utf8.h"
#include "strbuf.h"
#include "tree-walk.h"
#include "string-list.h"
#include "refs.h"

/*
 * Use a non-balancing simple 16-tree structure with struct int_node as
 * internal nodes, and struct leaf_node as leaf nodes. Each int_node has a
 * 16-array of pointers to its children.
 * The bottom 2 bits of each pointer is used to identify the pointer type
 * - ptr & 3 == 0 - NULL pointer, assert(ptr == NULL)
 * - ptr & 3 == 1 - pointer to next internal node - cast to struct int_node *
 * - ptr & 3 == 2 - pointer to note entry - cast to struct leaf_node *
 * - ptr & 3 == 3 - pointer to subtree entry - cast to struct leaf_node *
 *
 * The root node is a statically allocated struct int_node.
 */
struct int_node {
	void *a[16];
};

/*
 * Leaf nodes come in two variants, note entries and subtree entries,
 * distinguished by the LSb of the leaf node pointer (see above).
 * As a note entry, the key is the SHA1 of the referenced object, and the
 * value is the SHA1 of the note object.
 * As a subtree entry, the key is the prefix SHA1 (w/trailing NULs) of the
 * referenced object, using the last byte of the key to store the length of
 * the prefix. The value is the SHA1 of the tree object containing the notes
 * subtree.
 */
struct leaf_node {
	struct object_id key_oid;
	struct object_id val_oid;
};

/*
 * A notes tree may contain entries that are not notes, and that do not follow
 * the naming conventions of notes. There are typically none/few of these, but
 * we still need to keep track of them. Keep a simple linked list sorted alpha-
 * betically on the non-note path. The list is populated when parsing tree
 * objects in load_subtree(), and the non-notes are correctly written back into
 * the tree objects produced by write_notes_tree().
 */
struct non_note {
	struct non_note *next; /* grounded (last->next == NULL) */
	char *path;
	unsigned int mode;
	struct object_id oid;
};

#define PTR_TYPE_NULL     0
#define PTR_TYPE_INTERNAL 1
#define PTR_TYPE_NOTE     2
#define PTR_TYPE_SUBTREE  3

#define GET_PTR_TYPE(ptr)       ((uintptr_t) (ptr) & 3)
#define CLR_PTR_TYPE(ptr)       ((void *) ((uintptr_t) (ptr) & ~3))
#define SET_PTR_TYPE(ptr, type) ((void *) ((uintptr_t) (ptr) | (type)))

#define GET_NIBBLE(n, sha1) ((((sha1)[(n) >> 1]) >> ((~(n) & 0x01) << 2)) & 0x0f)

#define KEY_INDEX (the_hash_algo->rawsz - 1)
#define FANOUT_PATH_SEPARATORS (the_hash_algo->rawsz - 1)
#define FANOUT_PATH_SEPARATORS_MAX ((GIT_MAX_HEXSZ / 2) - 1)
#define SUBTREE_SHA1_PREFIXCMP(key_sha1, subtree_sha1) \
	(memcmp(key_sha1, subtree_sha1, subtree_sha1[KEY_INDEX]))

struct notes_tree default_notes_tree;

static struct string_list display_notes_refs = STRING_LIST_INIT_NODUP;
static struct notes_tree **display_notes_trees;

static void load_subtree(struct notes_tree *t, struct leaf_node *subtree,
		struct int_node *node, unsigned int n);

/*
 * Search the tree until the appropriate location for the given key is found:
 * 1. Start at the root node, with n = 0
 * 2. If a[0] at the current level is a matching subtree entry, unpack that
 *    subtree entry and remove it; restart search at the current level.
 * 3. Use the nth nibble of the key as an index into a:
 *    - If a[n] is an int_node, recurse from #2 into that node and increment n
 *    - If a matching subtree entry, unpack that subtree entry (and remove it);
 *      restart search at the current level.
 *    - Otherwise, we have found one of the following:
 *      - a subtree entry which does not match the key
 *      - a note entry which may or may not match the key
 *      - an unused leaf node (NULL)
 *      In any case, set *tree and *n, and return pointer to the tree location.
 */
static void **note_tree_search(struct notes_tree *t, struct int_node **tree,
		unsigned char *n, const unsigned char *key_sha1)
{
	struct leaf_node *l;
	unsigned char i;
	void *p = (*tree)->a[0];

	if (GET_PTR_TYPE(p) == PTR_TYPE_SUBTREE) {
		l = (struct leaf_node *) CLR_PTR_TYPE(p);
		if (!SUBTREE_SHA1_PREFIXCMP(key_sha1, l->key_oid.hash)) {
			/* unpack tree and resume search */
			(*tree)->a[0] = NULL;
			load_subtree(t, l, *tree, *n);
			free(l);
			return note_tree_search(t, tree, n, key_sha1);
		}
	}

	i = GET_NIBBLE(*n, key_sha1);
	p = (*tree)->a[i];
	switch (GET_PTR_TYPE(p)) {
	case PTR_TYPE_INTERNAL:
		*tree = CLR_PTR_TYPE(p);
		(*n)++;
		return note_tree_search(t, tree, n, key_sha1);
	case PTR_TYPE_SUBTREE:
		l = (struct leaf_node *) CLR_PTR_TYPE(p);
		if (!SUBTREE_SHA1_PREFIXCMP(key_sha1, l->key_oid.hash)) {
			/* unpack tree and resume search */
			(*tree)->a[i] = NULL;
			load_subtree(t, l, *tree, *n);
			free(l);
			return note_tree_search(t, tree, n, key_sha1);
		}
		/* fall through */
	default:
		return &((*tree)->a[i]);
	}
}

/*
 * To find a leaf_node:
 * Search to the tree location appropriate for the given key:
 * If a note entry with matching key, return the note entry, else return NULL.
 */
static struct leaf_node *note_tree_find(struct notes_tree *t,
		struct int_node *tree, unsigned char n,
		const unsigned char *key_sha1)
{
	void **p = note_tree_search(t, &tree, &n, key_sha1);
	if (GET_PTR_TYPE(*p) == PTR_TYPE_NOTE) {
		struct leaf_node *l = (struct leaf_node *) CLR_PTR_TYPE(*p);
		if (hasheq(key_sha1, l->key_oid.hash))
			return l;
	}
	return NULL;
}

/*
 * How to consolidate an int_node:
 * If there are > 1 non-NULL entries, give up and return non-zero.
 * Otherwise replace the int_node at the given index in the given parent node
 * with the only NOTE entry (or a NULL entry if no entries) from the given
 * tree, and return 0.
 */
static int note_tree_consolidate(struct int_node *tree,
	struct int_node *parent, unsigned char index)
{
	unsigned int i;
	void *p = NULL;

	assert(tree && parent);
	assert(CLR_PTR_TYPE(parent->a[index]) == tree);

	for (i = 0; i < 16; i++) {
		if (GET_PTR_TYPE(tree->a[i]) != PTR_TYPE_NULL) {
			if (p) /* more than one entry */
				return -2;
			p = tree->a[i];
		}
	}

	if (p && (GET_PTR_TYPE(p) != PTR_TYPE_NOTE))
		return -2;
	/* replace tree with p in parent[index] */
	parent->a[index] = p;
	free(tree);
	return 0;
}

/*
 * To remove a leaf_node:
 * Search to the tree location appropriate for the given leaf_node's key:
 * - If location does not hold a matching entry, abort and do nothing.
 * - Copy the matching entry's value into the given entry.
 * - Replace the matching leaf_node with a NULL entry (and free the leaf_node).
 * - Consolidate int_nodes repeatedly, while walking up the tree towards root.
 */
static void note_tree_remove(struct notes_tree *t,
		struct int_node *tree, unsigned char n,
		struct leaf_node *entry)
{
	struct leaf_node *l;
	struct int_node *parent_stack[GIT_MAX_RAWSZ];
	unsigned char i, j;
	void **p = note_tree_search(t, &tree, &n, entry->key_oid.hash);

	assert(GET_PTR_TYPE(entry) == 0); /* no type bits set */
	if (GET_PTR_TYPE(*p) != PTR_TYPE_NOTE)
		return; /* type mismatch, nothing to remove */
	l = (struct leaf_node *) CLR_PTR_TYPE(*p);
	if (!oideq(&l->key_oid, &entry->key_oid))
		return; /* key mismatch, nothing to remove */

	/* we have found a matching entry */
	oidcpy(&entry->val_oid, &l->val_oid);
	free(l);
	*p = SET_PTR_TYPE(NULL, PTR_TYPE_NULL);

	/* consolidate this tree level, and parent levels, if possible */
	if (!n)
		return; /* cannot consolidate top level */
	/* first, build stack of ancestors between root and current node */
	parent_stack[0] = t->root;
	for (i = 0; i < n; i++) {
		j = GET_NIBBLE(i, entry->key_oid.hash);
		parent_stack[i + 1] = CLR_PTR_TYPE(parent_stack[i]->a[j]);
	}
	assert(i == n && parent_stack[i] == tree);
	/* next, unwind stack until note_tree_consolidate() is done */
	while (i > 0 &&
	       !note_tree_consolidate(parent_stack[i], parent_stack[i - 1],
				      GET_NIBBLE(i - 1, entry->key_oid.hash)))
		i--;
}

/*
 * To insert a leaf_node:
 * Search to the tree location appropriate for the given leaf_node's key:
 * - If location is unused (NULL), store the tweaked pointer directly there
 * - If location holds a note entry that matches the note-to-be-inserted, then
 *   combine the two notes (by calling the given combine_notes function).
 * - If location holds a note entry that matches the subtree-to-be-inserted,
 *   then unpack the subtree-to-be-inserted into the location.
 * - If location holds a matching subtree entry, unpack the subtree at that
 *   location, and restart the insert operation from that level.
 * - Else, create a new int_node, holding both the node-at-location and the
 *   node-to-be-inserted, and store the new int_node into the location.
 */
static int note_tree_insert(struct notes_tree *t, struct int_node *tree,
		unsigned char n, struct leaf_node *entry, unsigned char type,
		combine_notes_fn combine_notes)
{
	struct int_node *new_node;
	struct leaf_node *l;
	void **p = note_tree_search(t, &tree, &n, entry->key_oid.hash);
	int ret = 0;

	assert(GET_PTR_TYPE(entry) == 0); /* no type bits set */
	l = (struct leaf_node *) CLR_PTR_TYPE(*p);
	switch (GET_PTR_TYPE(*p)) {
	case PTR_TYPE_NULL:
		assert(!*p);
		if (is_null_oid(&entry->val_oid))
			free(entry);
		else
			*p = SET_PTR_TYPE(entry, type);
		return 0;
	case PTR_TYPE_NOTE:
		switch (type) {
		case PTR_TYPE_NOTE:
			if (oideq(&l->key_oid, &entry->key_oid)) {
				/* skip concatenation if l == entry */
				if (oideq(&l->val_oid, &entry->val_oid)) {
					free(entry);
					return 0;
				}

				ret = combine_notes(&l->val_oid,
						    &entry->val_oid);
				if (!ret && is_null_oid(&l->val_oid))
					note_tree_remove(t, tree, n, entry);
				free(entry);
				return ret;
			}
			break;
		case PTR_TYPE_SUBTREE:
			if (!SUBTREE_SHA1_PREFIXCMP(l->key_oid.hash,
						    entry->key_oid.hash)) {
				/* unpack 'entry' */
				load_subtree(t, entry, tree, n);
				free(entry);
				return 0;
			}
			break;
		}
		break;
	case PTR_TYPE_SUBTREE:
		if (!SUBTREE_SHA1_PREFIXCMP(entry->key_oid.hash, l->key_oid.hash)) {
			/* unpack 'l' and restart insert */
			*p = NULL;
			load_subtree(t, l, tree, n);
			free(l);
			return note_tree_insert(t, tree, n, entry, type,
						combine_notes);
		}
		break;
	}

	/* non-matching leaf_node */
	assert(GET_PTR_TYPE(*p) == PTR_TYPE_NOTE ||
	       GET_PTR_TYPE(*p) == PTR_TYPE_SUBTREE);
	if (is_null_oid(&entry->val_oid)) { /* skip insertion of empty note */
		free(entry);
		return 0;
	}
	new_node = (struct int_node *) xcalloc(1, sizeof(struct int_node));
	ret = note_tree_insert(t, new_node, n + 1, l, GET_PTR_TYPE(*p),
			       combine_notes);
	if (ret)
		return ret;
	*p = SET_PTR_TYPE(new_node, PTR_TYPE_INTERNAL);
	return note_tree_insert(t, new_node, n + 1, entry, type, combine_notes);
}

/* Free the entire notes data contained in the given tree */
static void note_tree_free(struct int_node *tree)
{
	unsigned int i;
	for (i = 0; i < 16; i++) {
		void *p = tree->a[i];
		switch (GET_PTR_TYPE(p)) {
		case PTR_TYPE_INTERNAL:
			note_tree_free(CLR_PTR_TYPE(p));
			/* fall through */
		case PTR_TYPE_NOTE:
		case PTR_TYPE_SUBTREE:
			free(CLR_PTR_TYPE(p));
		}
	}
}

static int non_note_cmp(const struct non_note *a, const struct non_note *b)
{
	return strcmp(a->path, b->path);
}

/* note: takes ownership of path string */
static void add_non_note(struct notes_tree *t, char *path,
		unsigned int mode, const unsigned char *sha1)
{
	struct non_note *p = t->prev_non_note, *n;
	n = (struct non_note *) xmalloc(sizeof(struct non_note));
	n->next = NULL;
	n->path = path;
	n->mode = mode;
	oidread(&n->oid, sha1);
	t->prev_non_note = n;

	if (!t->first_non_note) {
		t->first_non_note = n;
		return;
	}

	if (non_note_cmp(p, n) < 0)
		; /* do nothing  */
	else if (non_note_cmp(t->first_non_note, n) <= 0)
		p = t->first_non_note;
	else {
		/* n sorts before t->first_non_note */
		n->next = t->first_non_note;
		t->first_non_note = n;
		return;
	}

	/* n sorts equal or after p */
	while (p->next && non_note_cmp(p->next, n) <= 0)
		p = p->next;

	if (non_note_cmp(p, n) == 0) { /* n ~= p; overwrite p with n */
		assert(strcmp(p->path, n->path) == 0);
		p->mode = n->mode;
		oidcpy(&p->oid, &n->oid);
		free(n);
		t->prev_non_note = p;
		return;
	}

	/* n sorts between p and p->next */
	n->next = p->next;
	p->next = n;
}

static void load_subtree(struct notes_tree *t, struct leaf_node *subtree,
		struct int_node *node, unsigned int n)
{
	struct object_id object_oid;
	size_t prefix_len;
	void *buf;
	struct tree_desc desc;
	struct name_entry entry;
	const unsigned hashsz = the_hash_algo->rawsz;

	buf = fill_tree_descriptor(the_repository, &desc, &subtree->val_oid);
	if (!buf)
		die("Could not read %s for notes-index",
		     oid_to_hex(&subtree->val_oid));

	prefix_len = subtree->key_oid.hash[KEY_INDEX];
	if (prefix_len >= hashsz)
		BUG("prefix_len (%"PRIuMAX") is out of range", (uintmax_t)prefix_len);
	if (prefix_len * 2 < n)
		BUG("prefix_len (%"PRIuMAX") is too small", (uintmax_t)prefix_len);
	memcpy(object_oid.hash, subtree->key_oid.hash, prefix_len);
	while (tree_entry(&desc, &entry)) {
		unsigned char type;
		struct leaf_node *l;
		size_t path_len = strlen(entry.path);

		if (path_len == 2 * (hashsz - prefix_len)) {
			/* This is potentially the remainder of the SHA-1 */

			if (!S_ISREG(entry.mode))
				/* notes must be blobs */
				goto handle_non_note;

			if (hex_to_bytes(object_oid.hash + prefix_len, entry.path,
					 hashsz - prefix_len))
				goto handle_non_note; /* entry.path is not a SHA1 */

			type = PTR_TYPE_NOTE;
		} else if (path_len == 2) {
			/* This is potentially an internal node */
			size_t len = prefix_len;

			if (!S_ISDIR(entry.mode))
				/* internal nodes must be trees */
				goto handle_non_note;

			if (hex_to_bytes(object_oid.hash + len++, entry.path, 1))
				goto handle_non_note; /* entry.path is not a SHA1 */

			/*
			 * Pad the rest of the SHA-1 with zeros,
			 * except for the last byte, where we write
			 * the length:
			 */
			memset(object_oid.hash + len, 0, hashsz - len - 1);
			object_oid.hash[KEY_INDEX] = (unsigned char)len;

			type = PTR_TYPE_SUBTREE;
		} else {
			/* This can't be part of a note */
			goto handle_non_note;
		}

		CALLOC_ARRAY(l, 1);
		oidcpy(&l->key_oid, &object_oid);
		oidcpy(&l->val_oid, &entry.oid);
		oid_set_algo(&l->key_oid, the_hash_algo);
		oid_set_algo(&l->val_oid, the_hash_algo);
		if (note_tree_insert(t, node, n, l, type,
				     combine_notes_concatenate))
			die("Failed to load %s %s into notes tree "
			    "from %s",
			    type == PTR_TYPE_NOTE ? "note" : "subtree",
			    oid_to_hex(&object_oid), t->ref);

		continue;

handle_non_note:
		/*
		 * Determine full path for this non-note entry. The
		 * filename is already found in entry.path, but the
		 * directory part of the path must be deduced from the
		 * subtree containing this entry based on our
		 * knowledge that the overall notes tree follows a
		 * strict byte-based progressive fanout structure
		 * (i.e. using 2/38, 2/2/36, etc. fanouts).
		 */
		{
			struct strbuf non_note_path = STRBUF_INIT;
			const char *q = oid_to_hex(&subtree->key_oid);
			size_t i;
			for (i = 0; i < prefix_len; i++) {
				strbuf_addch(&non_note_path, *q++);
				strbuf_addch(&non_note_path, *q++);
				strbuf_addch(&non_note_path, '/');
			}
			strbuf_addstr(&non_note_path, entry.path);
			oid_set_algo(&entry.oid, the_hash_algo);
			add_non_note(t, strbuf_detach(&non_note_path, NULL),
				     entry.mode, entry.oid.hash);
		}
	}
	free(buf);
}

/*
 * Determine optimal on-disk fanout for this part of the notes tree
 *
 * Given a (sub)tree and the level in the internal tree structure, determine
 * whether or not the given existing fanout should be expanded for this
 * (sub)tree.
 *
 * Values of the 'fanout' variable:
 * - 0: No fanout (all notes are stored directly in the root notes tree)
 * - 1: 2/38 fanout
 * - 2: 2/2/36 fanout
 * - 3: 2/2/2/34 fanout
 * etc.
 */
static unsigned char determine_fanout(struct int_node *tree, unsigned char n,
		unsigned char fanout)
{
	/*
	 * The following is a simple heuristic that works well in practice:
	 * For each even-numbered 16-tree level (remember that each on-disk
	 * fanout level corresponds to _two_ 16-tree levels), peek at all 16
	 * entries at that tree level. If all of them are either int_nodes or
	 * subtree entries, then there are likely plenty of notes below this
	 * level, so we return an incremented fanout.
	 */
	unsigned int i;
	if ((n % 2) || (n > 2 * fanout))
		return fanout;
	for (i = 0; i < 16; i++) {
		switch (GET_PTR_TYPE(tree->a[i])) {
		case PTR_TYPE_SUBTREE:
		case PTR_TYPE_INTERNAL:
			continue;
		default:
			return fanout;
		}
	}
	return fanout + 1;
}

/* hex oid + '/' between each pair of hex digits + NUL */
#define FANOUT_PATH_MAX GIT_MAX_HEXSZ + FANOUT_PATH_SEPARATORS_MAX + 1

static void construct_path_with_fanout(const unsigned char *hash,
		unsigned char fanout, char *path)
{
	unsigned int i = 0, j = 0;
	const char *hex_hash = hash_to_hex(hash);
	assert(fanout < the_hash_algo->rawsz);
	while (fanout) {
		path[i++] = hex_hash[j++];
		path[i++] = hex_hash[j++];
		path[i++] = '/';
		fanout--;
	}
	xsnprintf(path + i, FANOUT_PATH_MAX - i, "%s", hex_hash + j);
}

static int for_each_note_helper(struct notes_tree *t, struct int_node *tree,
		unsigned char n, unsigned char fanout, int flags,
		each_note_fn fn, void *cb_data)
{
	unsigned int i;
	void *p;
	int ret = 0;
	struct leaf_node *l;
	static char path[FANOUT_PATH_MAX];

	fanout = determine_fanout(tree, n, fanout);
	for (i = 0; i < 16; i++) {
redo:
		p = tree->a[i];
		switch (GET_PTR_TYPE(p)) {
		case PTR_TYPE_INTERNAL:
			/* recurse into int_node */
			ret = for_each_note_helper(t, CLR_PTR_TYPE(p), n + 1,
				fanout, flags, fn, cb_data);
			break;
		case PTR_TYPE_SUBTREE:
			l = (struct leaf_node *) CLR_PTR_TYPE(p);
			/*
			 * Subtree entries in the note tree represent parts of
			 * the note tree that have not yet been explored. There
			 * is a direct relationship between subtree entries at
			 * level 'n' in the tree, and the 'fanout' variable:
			 * Subtree entries at level 'n < 2 * fanout' should be
			 * preserved, since they correspond exactly to a fanout
			 * directory in the on-disk structure. However, subtree
			 * entries at level 'n >= 2 * fanout' should NOT be
			 * preserved, but rather consolidated into the above
			 * notes tree level. We achieve this by unconditionally
			 * unpacking subtree entries that exist below the
			 * threshold level at 'n = 2 * fanout'.
			 */
			if (n < 2 * fanout &&
			    flags & FOR_EACH_NOTE_YIELD_SUBTREES) {
				/* invoke callback with subtree */
				unsigned int path_len =
					l->key_oid.hash[KEY_INDEX] * 2 + fanout;
				assert(path_len < FANOUT_PATH_MAX - 1);
				construct_path_with_fanout(l->key_oid.hash,
							   fanout,
							   path);
				/* Create trailing slash, if needed */
				if (path[path_len - 1] != '/')
					path[path_len++] = '/';
				path[path_len] = '\0';
				ret = fn(&l->key_oid, &l->val_oid,
					 path,
					 cb_data);
			}
			if (n >= 2 * fanout ||
			    !(flags & FOR_EACH_NOTE_DONT_UNPACK_SUBTREES)) {
				/* unpack subtree and resume traversal */
				tree->a[i] = NULL;
				load_subtree(t, l, tree, n);
				free(l);
				goto redo;
			}
			break;
		case PTR_TYPE_NOTE:
			l = (struct leaf_node *) CLR_PTR_TYPE(p);
			construct_path_with_fanout(l->key_oid.hash, fanout,
						   path);
			ret = fn(&l->key_oid, &l->val_oid, path,
				 cb_data);
			break;
		}
		if (ret)
			return ret;
	}
	return 0;
}

struct tree_write_stack {
	struct tree_write_stack *next;
	struct strbuf buf;
	char path[2]; /* path to subtree in next, if any */
};

static inline int matches_tree_write_stack(struct tree_write_stack *tws,
		const char *full_path)
{
	return  full_path[0] == tws->path[0] &&
		full_path[1] == tws->path[1] &&
		full_path[2] == '/';
}

static void write_tree_entry(struct strbuf *buf, unsigned int mode,
		const char *path, unsigned int path_len, const
		unsigned char *hash)
{
	strbuf_addf(buf, "%o %.*s%c", mode, path_len, path, '\0');
	strbuf_add(buf, hash, the_hash_algo->rawsz);
}

static void tree_write_stack_init_subtree(struct tree_write_stack *tws,
		const char *path)
{
	struct tree_write_stack *n;
	assert(!tws->next);
	assert(tws->path[0] == '\0' && tws->path[1] == '\0');
	n = (struct tree_write_stack *)
		xmalloc(sizeof(struct tree_write_stack));
	n->next = NULL;
	strbuf_init(&n->buf, 256 * (32 + the_hash_algo->hexsz)); /* assume 256 entries per tree */
	n->path[0] = n->path[1] = '\0';
	tws->next = n;
	tws->path[0] = path[0];
	tws->path[1] = path[1];
}

static int tree_write_stack_finish_subtree(struct tree_write_stack *tws)
{
	int ret;
	struct tree_write_stack *n = tws->next;
	struct object_id s;
	if (n) {
		ret = tree_write_stack_finish_subtree(n);
		if (ret)
			return ret;
		ret = write_object_file(n->buf.buf, n->buf.len, OBJ_TREE, &s);
		if (ret)
			return ret;
		strbuf_release(&n->buf);
		free(n);
		tws->next = NULL;
		write_tree_entry(&tws->buf, 040000, tws->path, 2, s.hash);
		tws->path[0] = tws->path[1] = '\0';
	}
	return 0;
}

static int write_each_note_helper(struct tree_write_stack *tws,
		const char *path, unsigned int mode,
		const struct object_id *oid)
{
	size_t path_len = strlen(path);
	unsigned int n = 0;
	int ret;

	/* Determine common part of tree write stack */
	while (tws && 3 * n < path_len &&
	       matches_tree_write_stack(tws, path + 3 * n)) {
		n++;
		tws = tws->next;
	}

	/* tws point to last matching tree_write_stack entry */
	ret = tree_write_stack_finish_subtree(tws);
	if (ret)
		return ret;

	/* Start subtrees needed to satisfy path */
	while (3 * n + 2 < path_len && path[3 * n + 2] == '/') {
		tree_write_stack_init_subtree(tws, path + 3 * n);
		n++;
		tws = tws->next;
	}

	/* There should be no more directory components in the given path */
	assert(memchr(path + 3 * n, '/', path_len - (3 * n)) == NULL);

	/* Finally add given entry to the current tree object */
	write_tree_entry(&tws->buf, mode, path + 3 * n, path_len - (3 * n),
			 oid->hash);

	return 0;
}

struct write_each_note_data {
	struct tree_write_stack *root;
	struct non_note **nn_list;
	struct non_note *nn_prev;
};

static int write_each_non_note_until(const char *note_path,
		struct write_each_note_data *d)
{
	struct non_note *p = d->nn_prev;
	struct non_note *n = p ? p->next : *d->nn_list;
	int cmp = 0, ret;
	while (n && (!note_path || (cmp = strcmp(n->path, note_path)) <= 0)) {
		if (note_path && cmp == 0)
			; /* do nothing, prefer note to non-note */
		else {
			ret = write_each_note_helper(d->root, n->path, n->mode,
						     &n->oid);
			if (ret)
				return ret;
		}
		p = n;
		n = n->next;
	}
	d->nn_prev = p;
	return 0;
}

static int write_each_note(const struct object_id *object_oid UNUSED,
		const struct object_id *note_oid, char *note_path,
		void *cb_data)
{
	struct write_each_note_data *d =
		(struct write_each_note_data *) cb_data;
	size_t note_path_len = strlen(note_path);
	unsigned int mode = 0100644;

	if (note_path[note_path_len - 1] == '/') {
		/* subtree entry */
		note_path_len--;
		note_path[note_path_len] = '\0';
		mode = 040000;
	}
	assert(note_path_len <= GIT_MAX_HEXSZ + FANOUT_PATH_SEPARATORS);

	/* Weave non-note entries into note entries */
	return  write_each_non_note_until(note_path, d) ||
		write_each_note_helper(d->root, note_path, mode, note_oid);
}

struct note_delete_list {
	struct note_delete_list *next;
	const unsigned char *sha1;
};

static int prune_notes_helper(const struct object_id *object_oid,
			      const struct object_id *note_oid UNUSED,
			      char *note_path UNUSED,
			      void *cb_data)
{
	struct note_delete_list **l = (struct note_delete_list **) cb_data;
	struct note_delete_list *n;

	if (repo_has_object_file(the_repository, object_oid))
		return 0; /* nothing to do for this note */

	/* failed to find object => prune this note */
	n = (struct note_delete_list *) xmalloc(sizeof(*n));
	n->next = *l;
	n->sha1 = object_oid->hash;
	*l = n;
	return 0;
}

int combine_notes_concatenate(struct object_id *cur_oid,
			      const struct object_id *new_oid)
{
	char *cur_msg = NULL, *new_msg = NULL, *buf;
	unsigned long cur_len, new_len, buf_len;
	enum object_type cur_type, new_type;
	int ret;

	/* read in both note blob objects */
	if (!is_null_oid(new_oid))
		new_msg = repo_read_object_file(the_repository, new_oid,
						&new_type, &new_len);
	if (!new_msg || !new_len || new_type != OBJ_BLOB) {
		free(new_msg);
		return 0;
	}
	if (!is_null_oid(cur_oid))
		cur_msg = repo_read_object_file(the_repository, cur_oid,
						&cur_type, &cur_len);
	if (!cur_msg || !cur_len || cur_type != OBJ_BLOB) {
		free(cur_msg);
		free(new_msg);
		oidcpy(cur_oid, new_oid);
		return 0;
	}

	/* we will separate the notes by two newlines anyway */
	if (cur_msg[cur_len - 1] == '\n')
		cur_len--;

	/* concatenate cur_msg and new_msg into buf */
	buf_len = cur_len + 2 + new_len;
	buf = (char *) xmalloc(buf_len);
	memcpy(buf, cur_msg, cur_len);
	buf[cur_len] = '\n';
	buf[cur_len + 1] = '\n';
	memcpy(buf + cur_len + 2, new_msg, new_len);
	free(cur_msg);
	free(new_msg);

	/* create a new blob object from buf */
	ret = write_object_file(buf, buf_len, OBJ_BLOB, cur_oid);
	free(buf);
	return ret;
}

int combine_notes_overwrite(struct object_id *cur_oid,
			    const struct object_id *new_oid)
{
	oidcpy(cur_oid, new_oid);
	return 0;
}

int combine_notes_ignore(struct object_id *cur_oid UNUSED,
			 const struct object_id *new_oid UNUSED)
{
	return 0;
}

/*
 * Add the lines from the named object to list, with trailing
 * newlines removed.
 */
static int string_list_add_note_lines(struct string_list *list,
				      const struct object_id *oid)
{
	char *data;
	unsigned long len;
	enum object_type t;

	if (is_null_oid(oid))
		return 0;

	/* read_sha1_file NUL-terminates */
	data = repo_read_object_file(the_repository, oid, &t, &len);
	if (t != OBJ_BLOB || !data || !len) {
		free(data);
		return t != OBJ_BLOB || !data;
	}

	/*
	 * If the last line of the file is EOL-terminated, this will
	 * add an empty string to the list.  But it will be removed
	 * later, along with any empty strings that came from empty
	 * lines within the file.
	 */
	string_list_split(list, data, '\n', -1);
	free(data);
	return 0;
}

static int string_list_join_lines_helper(struct string_list_item *item,
					 void *cb_data)
{
	struct strbuf *buf = cb_data;
	strbuf_addstr(buf, item->string);
	strbuf_addch(buf, '\n');
	return 0;
}

int combine_notes_cat_sort_uniq(struct object_id *cur_oid,
				const struct object_id *new_oid)
{
	struct string_list sort_uniq_list = STRING_LIST_INIT_DUP;
	struct strbuf buf = STRBUF_INIT;
	int ret = 1;

	/* read both note blob objects into unique_lines */
	if (string_list_add_note_lines(&sort_uniq_list, cur_oid))
		goto out;
	if (string_list_add_note_lines(&sort_uniq_list, new_oid))
		goto out;
	string_list_remove_empty_items(&sort_uniq_list, 0);
	string_list_sort(&sort_uniq_list);
	string_list_remove_duplicates(&sort_uniq_list, 0);

	/* create a new blob object from sort_uniq_list */
	if (for_each_string_list(&sort_uniq_list,
				 string_list_join_lines_helper, &buf))
		goto out;

	ret = write_object_file(buf.buf, buf.len, OBJ_BLOB, cur_oid);

out:
	strbuf_release(&buf);
	string_list_clear(&sort_uniq_list, 0);
	return ret;
}

static int string_list_add_one_ref(const char *refname,
				   const struct object_id *oid UNUSED,
				   int flag UNUSED, void *cb)
{
	struct string_list *refs = cb;
	if (!unsorted_string_list_has_string(refs, refname))
		string_list_append(refs, refname);
	return 0;
}

/*
 * The list argument must have strdup_strings set on it.
 */
void string_list_add_refs_by_glob(struct string_list *list, const char *glob)
{
	assert(list->strdup_strings);
	if (has_glob_specials(glob)) {
		for_each_glob_ref(string_list_add_one_ref, glob, list);
	} else {
		struct object_id oid;
		if (repo_get_oid(the_repository, glob, &oid))
			warning("notes ref %s is invalid", glob);
		if (!unsorted_string_list_has_string(list, glob))
			string_list_append(list, glob);
	}
}

void string_list_add_refs_from_colon_sep(struct string_list *list,
					 const char *globs)
{
	struct string_list split = STRING_LIST_INIT_NODUP;
	char *globs_copy = xstrdup(globs);
	int i;

	string_list_split_in_place(&split, globs_copy, ':', -1);
	string_list_remove_empty_items(&split, 0);

	for (i = 0; i < split.nr; i++)
		string_list_add_refs_by_glob(list, split.items[i].string);

	string_list_clear(&split, 0);
	free(globs_copy);
}

static int notes_display_config(const char *k, const char *v, void *cb)
{
	int *load_refs = cb;

	if (*load_refs && !strcmp(k, "notes.displayref")) {
		if (!v)
			return config_error_nonbool(k);
		string_list_add_refs_by_glob(&display_notes_refs, v);
	}

	return 0;
}

const char *default_notes_ref(void)
{
	const char *notes_ref = NULL;
	if (!notes_ref)
		notes_ref = getenv(GIT_NOTES_REF_ENVIRONMENT);
	if (!notes_ref)
		notes_ref = notes_ref_name; /* value of core.notesRef config */
	if (!notes_ref)
		notes_ref = GIT_NOTES_DEFAULT_REF;
	return notes_ref;
}

void init_notes(struct notes_tree *t, const char *notes_ref,
		combine_notes_fn combine_notes, int flags)
{
	struct object_id oid, object_oid;
	unsigned short mode;
	struct leaf_node root_tree;

	if (!t)
		t = &default_notes_tree;
	assert(!t->initialized);

	if (!notes_ref)
		notes_ref = default_notes_ref();
	update_ref_namespace(NAMESPACE_NOTES, xstrdup(notes_ref));

	if (!combine_notes)
		combine_notes = combine_notes_concatenate;

	t->root = (struct int_node *) xcalloc(1, sizeof(struct int_node));
	t->first_non_note = NULL;
	t->prev_non_note = NULL;
	t->ref = xstrdup_or_null(notes_ref);
	t->update_ref = (flags & NOTES_INIT_WRITABLE) ? t->ref : NULL;
	t->combine_notes = combine_notes;
	t->initialized = 1;
	t->dirty = 0;

	if (flags & NOTES_INIT_EMPTY || !notes_ref ||
	    repo_get_oid_treeish(the_repository, notes_ref, &object_oid))
		return;
	if (flags & NOTES_INIT_WRITABLE && read_ref(notes_ref, &object_oid))
		die("Cannot use notes ref %s", notes_ref);
	if (get_tree_entry(the_repository, &object_oid, "", &oid, &mode))
		die("Failed to read notes tree referenced by %s (%s)",
		    notes_ref, oid_to_hex(&object_oid));

	oidclr(&root_tree.key_oid);
	oidcpy(&root_tree.val_oid, &oid);
	load_subtree(t, &root_tree, t->root, 0);
}

struct notes_tree **load_notes_trees(struct string_list *refs, int flags)
{
	struct string_list_item *item;
	int counter = 0;
	struct notes_tree **trees;
	ALLOC_ARRAY(trees, refs->nr + 1);
	for_each_string_list_item(item, refs) {
		struct notes_tree *t = xcalloc(1, sizeof(struct notes_tree));
		init_notes(t, item->string, combine_notes_ignore, flags);
		trees[counter++] = t;
	}
	trees[counter] = NULL;
	return trees;
}

void init_display_notes(struct display_notes_opt *opt)
{
	memset(opt, 0, sizeof(*opt));
	opt->use_default_notes = -1;
}

void enable_default_display_notes(struct display_notes_opt *opt, int *show_notes)
{
	opt->use_default_notes = 1;
	*show_notes = 1;
}

void enable_ref_display_notes(struct display_notes_opt *opt, int *show_notes,
		const char *ref) {
	struct strbuf buf = STRBUF_INIT;
	strbuf_addstr(&buf, ref);
	expand_notes_ref(&buf);
	string_list_append(&opt->extra_notes_refs,
			strbuf_detach(&buf, NULL));
	*show_notes = 1;
}

void disable_display_notes(struct display_notes_opt *opt, int *show_notes)
{
	opt->use_default_notes = -1;
	/* we have been strdup'ing ourselves, so trick
	 * string_list into free()ing strings */
	opt->extra_notes_refs.strdup_strings = 1;
	string_list_clear(&opt->extra_notes_refs, 0);
	opt->extra_notes_refs.strdup_strings = 0;
	*show_notes = 0;
}

void load_display_notes(struct display_notes_opt *opt)
{
	char *display_ref_env;
	int load_config_refs = 0;
	display_notes_refs.strdup_strings = 1;

	assert(!display_notes_trees);

	if (!opt || opt->use_default_notes > 0 ||
	    (opt->use_default_notes == -1 && !opt->extra_notes_refs.nr)) {
		string_list_append(&display_notes_refs, default_notes_ref());
		display_ref_env = getenv(GIT_NOTES_DISPLAY_REF_ENVIRONMENT);
		if (display_ref_env) {
			string_list_add_refs_from_colon_sep(&display_notes_refs,
							    display_ref_env);
			load_config_refs = 0;
		} else
			load_config_refs = 1;
	}

	git_config(notes_display_config, &load_config_refs);

	if (opt) {
		struct string_list_item *item;
		for_each_string_list_item(item, &opt->extra_notes_refs)
			string_list_add_refs_by_glob(&display_notes_refs,
						     item->string);
	}

	display_notes_trees = load_notes_trees(&display_notes_refs, 0);
	string_list_clear(&display_notes_refs, 0);
}

int add_note(struct notes_tree *t, const struct object_id *object_oid,
		const struct object_id *note_oid, combine_notes_fn combine_notes)
{
	struct leaf_node *l;

	if (!t)
		t = &default_notes_tree;
	assert(t->initialized);
	t->dirty = 1;
	if (!combine_notes)
		combine_notes = t->combine_notes;
	l = (struct leaf_node *) xmalloc(sizeof(struct leaf_node));
	oidcpy(&l->key_oid, object_oid);
	oidcpy(&l->val_oid, note_oid);
	return note_tree_insert(t, t->root, 0, l, PTR_TYPE_NOTE, combine_notes);
}

int remove_note(struct notes_tree *t, const unsigned char *object_sha1)
{
	struct leaf_node l;

	if (!t)
		t = &default_notes_tree;
	assert(t->initialized);
	oidread(&l.key_oid, object_sha1);
	oidclr(&l.val_oid);
	note_tree_remove(t, t->root, 0, &l);
	if (is_null_oid(&l.val_oid)) /* no note was removed */
		return 1;
	t->dirty = 1;
	return 0;
}

const struct object_id *get_note(struct notes_tree *t,
		const struct object_id *oid)
{
	struct leaf_node *found;

	if (!t)
		t = &default_notes_tree;
	assert(t->initialized);
	found = note_tree_find(t, t->root, 0, oid->hash);
	return found ? &found->val_oid : NULL;
}

int for_each_note(struct notes_tree *t, int flags, each_note_fn fn,
		void *cb_data)
{
	if (!t)
		t = &default_notes_tree;
	assert(t->initialized);
	return for_each_note_helper(t, t->root, 0, 0, flags, fn, cb_data);
}

int write_notes_tree(struct notes_tree *t, struct object_id *result)
{
	struct tree_write_stack root;
	struct write_each_note_data cb_data;
	int ret;
	int flags;

	if (!t)
		t = &default_notes_tree;
	assert(t->initialized);

	/* Prepare for traversal of current notes tree */
	root.next = NULL; /* last forward entry in list is grounded */
	strbuf_init(&root.buf, 256 * (32 + the_hash_algo->hexsz)); /* assume 256 entries */
	root.path[0] = root.path[1] = '\0';
	cb_data.root = &root;
	cb_data.nn_list = &(t->first_non_note);
	cb_data.nn_prev = NULL;

	/* Write tree objects representing current notes tree */
	flags = FOR_EACH_NOTE_DONT_UNPACK_SUBTREES |
		FOR_EACH_NOTE_YIELD_SUBTREES;
	ret = for_each_note(t, flags, write_each_note, &cb_data) ||
	      write_each_non_note_until(NULL, &cb_data) ||
	      tree_write_stack_finish_subtree(&root) ||
	      write_object_file(root.buf.buf, root.buf.len, OBJ_TREE, result);
	strbuf_release(&root.buf);
	return ret;
}

void prune_notes(struct notes_tree *t, int flags)
{
	struct note_delete_list *l = NULL;

	if (!t)
		t = &default_notes_tree;
	assert(t->initialized);

	for_each_note(t, 0, prune_notes_helper, &l);

	while (l) {
		if (flags & NOTES_PRUNE_VERBOSE)
			printf("%s\n", hash_to_hex(l->sha1));
		if (!(flags & NOTES_PRUNE_DRYRUN))
			remove_note(t, l->sha1);
		l = l->next;
	}
}

void free_notes(struct notes_tree *t)
{
	if (!t)
		t = &default_notes_tree;
	if (t->root)
		note_tree_free(t->root);
	free(t->root);
	while (t->first_non_note) {
		t->prev_non_note = t->first_non_note->next;
		free(t->first_non_note->path);
		free(t->first_non_note);
		t->first_non_note = t->prev_non_note;
	}
	free(t->ref);
	memset(t, 0, sizeof(struct notes_tree));
}

/*
 * Fill the given strbuf with the notes associated with the given object.
 *
 * If the given notes_tree structure is not initialized, it will be auto-
 * initialized to the default value (see documentation for init_notes() above).
 * If the given notes_tree is NULL, the internal/default notes_tree will be
 * used instead.
 *
 * (raw != 0) gives the %N userformat; otherwise, the note message is given
 * for human consumption.
 */
static void format_note(struct notes_tree *t, const struct object_id *object_oid,
			struct strbuf *sb, const char *output_encoding, int raw)
{
	static const char utf8[] = "utf-8";
	const struct object_id *oid;
	char *msg, *msg_p;
	unsigned long linelen, msglen;
	enum object_type type;

	if (!t)
		t = &default_notes_tree;
	if (!t->initialized)
		init_notes(t, NULL, NULL, 0);

	oid = get_note(t, object_oid);
	if (!oid)
		return;

	if (!(msg = repo_read_object_file(the_repository, oid, &type, &msglen)) || type != OBJ_BLOB) {
		free(msg);
		return;
	}

	if (output_encoding && *output_encoding &&
	    !is_encoding_utf8(output_encoding)) {
		char *reencoded = reencode_string(msg, output_encoding, utf8);
		if (reencoded) {
			free(msg);
			msg = reencoded;
			msglen = strlen(msg);
		}
	}

	/* we will end the annotation by a newline anyway */
	if (msglen && msg[msglen - 1] == '\n')
		msglen--;

	if (!raw) {
		const char *ref = t->ref;
		if (!ref || !strcmp(ref, GIT_NOTES_DEFAULT_REF)) {
			strbuf_addstr(sb, "\nNotes:\n");
		} else {
			skip_prefix(ref, "refs/", &ref);
			skip_prefix(ref, "notes/", &ref);
			strbuf_addf(sb, "\nNotes (%s):\n", ref);
		}
	}

	for (msg_p = msg; msg_p < msg + msglen; msg_p += linelen + 1) {
		linelen = strchrnul(msg_p, '\n') - msg_p;

		if (!raw)
			strbuf_addstr(sb, "    ");
		strbuf_add(sb, msg_p, linelen);
		strbuf_addch(sb, '\n');
	}

	free(msg);
}

void format_display_notes(const struct object_id *object_oid,
			  struct strbuf *sb, const char *output_encoding, int raw)
{
	int i;
	assert(display_notes_trees);
	for (i = 0; display_notes_trees[i]; i++)
		format_note(display_notes_trees[i], object_oid, sb,
			    output_encoding, raw);
}

int copy_note(struct notes_tree *t,
	      const struct object_id *from_obj, const struct object_id *to_obj,
	      int force, combine_notes_fn combine_notes)
{
	const struct object_id *note = get_note(t, from_obj);
	const struct object_id *existing_note = get_note(t, to_obj);

	if (!force && existing_note)
		return 1;

	if (note)
		return add_note(t, to_obj, note, combine_notes);
	else if (existing_note)
		return add_note(t, to_obj, null_oid(), combine_notes);

	return 0;
}

void expand_notes_ref(struct strbuf *sb)
{
	if (starts_with(sb->buf, "refs/notes/"))
		return; /* we're happy */
	else if (starts_with(sb->buf, "notes/"))
		strbuf_insertstr(sb, 0, "refs/");
	else
		strbuf_insertstr(sb, 0, "refs/notes/");
}

void expand_loose_notes_ref(struct strbuf *sb)
{
	struct object_id object;

	if (repo_get_oid(the_repository, sb->buf, &object)) {
		/* fallback to expand_notes_ref */
		expand_notes_ref(sb);
	}
}
