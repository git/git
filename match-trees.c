#define USE_THE_REPOSITORY_VARIABLE

#include "git-compat-util.h"
#include "hex.h"
#include "match-trees.h"
#include "strbuf.h"
#include "tree.h"
#include "tree-walk.h"
#include "object-store-ll.h"

static int score_missing(unsigned mode)
{
	int score;

	if (S_ISDIR(mode))
		score = -1000;
	else if (S_ISLNK(mode))
		score = -500;
	else
		score = -50;
	return score;
}

static int score_differs(unsigned mode1, unsigned mode2)
{
	int score;

	if (S_ISDIR(mode1) != S_ISDIR(mode2))
		score = -100;
	else if (S_ISLNK(mode1) != S_ISLNK(mode2))
		score = -50;
	else
		score = -5;
	return score;
}

static int score_matches(unsigned mode1, unsigned mode2)
{
	int score;

	/* Heh, we found SHA-1 collisions between different kind of objects */
	if (S_ISDIR(mode1) != S_ISDIR(mode2))
		score = -100;
	else if (S_ISLNK(mode1) != S_ISLNK(mode2))
		score = -50;

	else if (S_ISDIR(mode1))
		score = 1000;
	else if (S_ISLNK(mode1))
		score = 500;
	else
		score = 250;
	return score;
}

static void *fill_tree_desc_strict(struct tree_desc *desc,
				   const struct object_id *hash)
{
	void *buffer;
	enum object_type type;
	unsigned long size;

	buffer = repo_read_object_file(the_repository, hash, &type, &size);
	if (!buffer)
		die("unable to read tree (%s)", oid_to_hex(hash));
	if (type != OBJ_TREE)
		die("%s is not a tree", oid_to_hex(hash));
	init_tree_desc(desc, hash, buffer, size);
	return buffer;
}

static int base_name_entries_compare(const struct name_entry *a,
				     const struct name_entry *b)
{
	return base_name_compare(a->path, tree_entry_len(a), a->mode,
				 b->path, tree_entry_len(b), b->mode);
}

/*
 * Inspect two trees, and give a score that tells how similar they are.
 */
static int score_trees(const struct object_id *hash1, const struct object_id *hash2)
{
	struct tree_desc one;
	struct tree_desc two;
	void *one_buf = fill_tree_desc_strict(&one, hash1);
	void *two_buf = fill_tree_desc_strict(&two, hash2);
	int score = 0;

	for (;;) {
		int cmp;

		if (one.size && two.size)
			cmp = base_name_entries_compare(&one.entry, &two.entry);
		else if (one.size)
			/* two lacks this entry */
			cmp = -1;
		else if (two.size)
			/* two has more entries */
			cmp = 1;
		else
			break;

		if (cmp < 0) {
			/* path1 does not appear in two */
			score += score_missing(one.entry.mode);
			update_tree_entry(&one);
		} else if (cmp > 0) {
			/* path2 does not appear in one */
			score += score_missing(two.entry.mode);
			update_tree_entry(&two);
		} else {
			/* path appears in both */
			if (!oideq(&one.entry.oid, &two.entry.oid)) {
				/* they are different */
				score += score_differs(one.entry.mode,
						       two.entry.mode);
			} else {
				/* same subtree or blob */
				score += score_matches(one.entry.mode,
						       two.entry.mode);
			}
			update_tree_entry(&one);
			update_tree_entry(&two);
		}
	}
	free(one_buf);
	free(two_buf);
	return score;
}

/*
 * Match one itself and its subtrees with two and pick the best match.
 */
static void match_trees(const struct object_id *hash1,
			const struct object_id *hash2,
			int *best_score,
			char **best_match,
			const char *base,
			int recurse_limit)
{
	struct tree_desc one;
	void *one_buf = fill_tree_desc_strict(&one, hash1);

	while (one.size) {
		const char *path;
		const struct object_id *elem;
		unsigned short mode;
		int score;

		elem = tree_entry_extract(&one, &path, &mode);
		if (!S_ISDIR(mode))
			goto next;
		score = score_trees(elem, hash2);
		if (*best_score < score) {
			free(*best_match);
			*best_match = xstrfmt("%s%s", base, path);
			*best_score = score;
		}
		if (recurse_limit) {
			char *newbase = xstrfmt("%s%s/", base, path);
			match_trees(elem, hash2, best_score, best_match,
				    newbase, recurse_limit - 1);
			free(newbase);
		}

	next:
		update_tree_entry(&one);
	}
	free(one_buf);
}

/*
 * A tree "oid1" has a subdirectory at "prefix".  Come up with a tree object by
 * replacing it with another tree "oid2".
 */
static int splice_tree(const struct object_id *oid1, const char *prefix,
		       const struct object_id *oid2, struct object_id *result)
{
	char *subpath;
	int toplen;
	char *buf;
	unsigned long sz;
	struct tree_desc desc;
	unsigned char *rewrite_here;
	const struct object_id *rewrite_with;
	struct object_id subtree;
	enum object_type type;
	int status;

	subpath = strchrnul(prefix, '/');
	toplen = subpath - prefix;
	if (*subpath)
		subpath++;

	buf = repo_read_object_file(the_repository, oid1, &type, &sz);
	if (!buf)
		die("cannot read tree %s", oid_to_hex(oid1));
	init_tree_desc(&desc, oid1, buf, sz);

	rewrite_here = NULL;
	while (desc.size) {
		const char *name;
		unsigned short mode;

		tree_entry_extract(&desc, &name, &mode);
		if (strlen(name) == toplen &&
		    !memcmp(name, prefix, toplen)) {
			if (!S_ISDIR(mode))
				die("entry %s in tree %s is not a tree", name,
				    oid_to_hex(oid1));

			/*
			 * We cast here for two reasons:
			 *
			 *   - to flip the "char *" (for the path) to "unsigned
			 *     char *" (for the hash stored after it)
			 *
			 *   - to discard the "const"; this is OK because we
			 *     know it points into our non-const "buf"
			 */
			rewrite_here = (unsigned char *)(desc.entry.path +
							 strlen(desc.entry.path) +
							 1);
			break;
		}
		update_tree_entry(&desc);
	}
	if (!rewrite_here)
		die("entry %.*s not found in tree %s", toplen, prefix,
		    oid_to_hex(oid1));
	if (*subpath) {
		struct object_id tree_oid;
		oidread(&tree_oid, rewrite_here, the_repository->hash_algo);
		status = splice_tree(&tree_oid, subpath, oid2, &subtree);
		if (status)
			return status;
		rewrite_with = &subtree;
	} else {
		rewrite_with = oid2;
	}
	hashcpy(rewrite_here, rewrite_with->hash, the_repository->hash_algo);
	status = write_object_file(buf, sz, OBJ_TREE, result);
	free(buf);
	return status;
}

/*
 * We are trying to come up with a merge between one and two that
 * results in a tree shape similar to one.  The tree two might
 * correspond to a subtree of one, in which case it needs to be
 * shifted down by prefixing otherwise empty directories.  On the
 * other hand, it could cover tree one and we might need to pick a
 * subtree of it.
 */
void shift_tree(struct repository *r,
		const struct object_id *hash1,
		const struct object_id *hash2,
		struct object_id *shifted,
		int depth_limit)
{
	char *add_prefix;
	char *del_prefix;
	int add_score, del_score;

	/*
	 * NEEDSWORK: this limits the recursion depth to hardcoded
	 * value '2' to avoid excessive overhead.
	 */
	if (!depth_limit)
		depth_limit = 2;

	add_score = del_score = score_trees(hash1, hash2);
	add_prefix = xcalloc(1, 1);
	del_prefix = xcalloc(1, 1);

	/*
	 * See if one's subtree resembles two; if so we need to prefix
	 * two with a few fake trees to match the prefix.
	 */
	match_trees(hash1, hash2, &add_score, &add_prefix, "", depth_limit);

	/*
	 * See if two's subtree resembles one; if so we need to
	 * pick only subtree of two.
	 */
	match_trees(hash2, hash1, &del_score, &del_prefix, "", depth_limit);

	/* Assume we do not have to do any shifting */
	oidcpy(shifted, hash2);

	if (add_score < del_score) {
		/* We need to pick a subtree of two */
		unsigned short mode;

		if (!*del_prefix)
			return;

		if (get_tree_entry(r, hash2, del_prefix, shifted, &mode))
			die("cannot find path %s in tree %s",
			    del_prefix, oid_to_hex(hash2));
		return;
	}

	if (!*add_prefix)
		return;

	splice_tree(hash1, add_prefix, hash2, shifted);
}

/*
 * The user says the trees will be shifted by this much.
 * Unfortunately we cannot fundamentally tell which one to
 * be prefixed, as recursive merge can work in either direction.
 */
void shift_tree_by(struct repository *r,
		   const struct object_id *hash1,
		   const struct object_id *hash2,
		   struct object_id *shifted,
		   const char *shift_prefix)
{
	struct object_id sub1, sub2;
	unsigned short mode1, mode2;
	unsigned candidate = 0;

	/* Can hash2 be a tree at shift_prefix in tree hash1? */
	if (!get_tree_entry(r, hash1, shift_prefix, &sub1, &mode1) &&
	    S_ISDIR(mode1))
		candidate |= 1;

	/* Can hash1 be a tree at shift_prefix in tree hash2? */
	if (!get_tree_entry(r, hash2, shift_prefix, &sub2, &mode2) &&
	    S_ISDIR(mode2))
		candidate |= 2;

	if (candidate == 3) {
		/* Both are plausible -- we need to evaluate the score */
		int best_score = score_trees(hash1, hash2);
		int score;

		candidate = 0;
		score = score_trees(&sub1, hash2);
		if (score > best_score) {
			candidate = 1;
			best_score = score;
		}
		score = score_trees(&sub2, hash1);
		if (score > best_score)
			candidate = 2;
	}

	if (!candidate) {
		/* Neither is plausible -- do not shift */
		oidcpy(shifted, hash2);
		return;
	}

	if (candidate == 1)
		/*
		 * shift tree2 down by adding shift_prefix above it
		 * to match tree1.
		 */
		splice_tree(hash1, shift_prefix, hash2, shifted);
	else
		/*
		 * shift tree2 up by removing shift_prefix from it
		 * to match tree1.
		 */
		oidcpy(shifted, &sub2);
}
