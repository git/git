#include "builtin.h"
#include "tree-walk.h"
#include "xdiff-interface.h"
#include "object-store.h"
#include "repository.h"
#include "blob.h"
#include "exec-cmd.h"
#include "merge-blobs.h"

static const char merge_tree_usage[] = "git merge-tree <base-tree> <branch1> <branch2>";

struct merge_list {
	struct merge_list *next;
	struct merge_list *link;	/* other stages for this object */

	unsigned int stage : 2;
	unsigned int mode;
	const char *path;
	struct blob *blob;
};

static struct merge_list *merge_result, **merge_result_end = &merge_result;

static void add_merge_entry(struct merge_list *entry)
{
	*merge_result_end = entry;
	merge_result_end = &entry->next;
}

static void merge_trees(struct tree_desc t[3], const char *base);

static const char *explanation(struct merge_list *entry)
{
	switch (entry->stage) {
	case 0:
		return "merged";
	case 3:
		return "added in remote";
	case 2:
		if (entry->link)
			return "added in both";
		return "added in local";
	}

	/* Existed in base */
	entry = entry->link;
	if (!entry)
		return "removed in both";

	if (entry->link)
		return "changed in both";

	if (entry->stage == 3)
		return "removed in local";
	return "removed in remote";
}

static void *result(struct merge_list *entry, unsigned long *size)
{
	enum object_type type;
	struct blob *base, *our, *their;
	const char *path = entry->path;

	if (!entry->stage)
		return read_object_file(&entry->blob->object.oid, &type, size);
	base = NULL;
	if (entry->stage == 1) {
		base = entry->blob;
		entry = entry->link;
	}
	our = NULL;
	if (entry && entry->stage == 2) {
		our = entry->blob;
		entry = entry->link;
	}
	their = NULL;
	if (entry)
		their = entry->blob;
	return merge_blobs(path, base, our, their, size);
}

static void *origin(struct merge_list *entry, unsigned long *size)
{
	enum object_type type;
	while (entry) {
		if (entry->stage == 2)
			return read_object_file(&entry->blob->object.oid,
						&type, size);
		entry = entry->link;
	}
	return NULL;
}

static int show_outf(void *priv_, mmbuffer_t *mb, int nbuf)
{
	int i;
	for (i = 0; i < nbuf; i++)
		printf("%.*s", (int) mb[i].size, mb[i].ptr);
	return 0;
}

static void show_diff(struct merge_list *entry)
{
	unsigned long size;
	mmfile_t src, dst;
	xpparam_t xpp;
	xdemitconf_t xecfg;
	xdemitcb_t ecb;

	xpp.flags = 0;
	memset(&xecfg, 0, sizeof(xecfg));
	xecfg.ctxlen = 3;
	ecb.outf = show_outf;
	ecb.priv = NULL;

	src.ptr = origin(entry, &size);
	if (!src.ptr)
		size = 0;
	src.size = size;
	dst.ptr = result(entry, &size);
	if (!dst.ptr)
		size = 0;
	dst.size = size;
	if (xdi_diff(&src, &dst, &xpp, &xecfg, &ecb))
		die("unable to generate diff");
	free(src.ptr);
	free(dst.ptr);
}

static void show_result_list(struct merge_list *entry)
{
	printf("%s\n", explanation(entry));
	do {
		struct merge_list *link = entry->link;
		static const char *desc[4] = { "result", "base", "our", "their" };
		printf("  %-6s %o %s %s\n", desc[entry->stage], entry->mode, oid_to_hex(&entry->blob->object.oid), entry->path);
		entry = link;
	} while (entry);
}

static void show_result(void)
{
	struct merge_list *walk;

	walk = merge_result;
	while (walk) {
		show_result_list(walk);
		show_diff(walk);
		walk = walk->next;
	}
}

/* An empty entry never compares same, not even to another empty entry */
static int same_entry(struct name_entry *a, struct name_entry *b)
{
	return	a->oid &&
		b->oid &&
		oideq(a->oid, b->oid) &&
		a->mode == b->mode;
}

static int both_empty(struct name_entry *a, struct name_entry *b)
{
	return !(a->oid || b->oid);
}

static struct merge_list *create_entry(unsigned stage, unsigned mode, const struct object_id *oid, const char *path)
{
	struct merge_list *res = xcalloc(1, sizeof(*res));

	res->stage = stage;
	res->path = path;
	res->mode = mode;
	res->blob = lookup_blob(the_repository, oid);
	return res;
}

static char *traverse_path(const struct traverse_info *info, const struct name_entry *n)
{
	char *path = xmallocz(traverse_path_len(info, n));
	return make_traverse_path(path, info, n);
}

static void resolve(const struct traverse_info *info, struct name_entry *ours, struct name_entry *result)
{
	struct merge_list *orig, *final;
	const char *path;

	/* If it's already ours, don't bother showing it */
	if (!ours)
		return;

	path = traverse_path(info, result);
	orig = create_entry(2, ours->mode, ours->oid, path);
	final = create_entry(0, result->mode, result->oid, path);

	final->link = orig;

	add_merge_entry(final);
}

static void unresolved_directory(const struct traverse_info *info,
				 struct name_entry n[3])
{
	char *newbase;
	struct name_entry *p;
	struct tree_desc t[3];
	void *buf0, *buf1, *buf2;

	for (p = n; p < n + 3; p++) {
		if (p->mode && S_ISDIR(p->mode))
			break;
	}
	if (n + 3 <= p)
		return; /* there is no tree here */

	newbase = traverse_path(info, p);

#define ENTRY_OID(e) (((e)->mode && S_ISDIR((e)->mode)) ? (e)->oid : NULL)
	buf0 = fill_tree_descriptor(t + 0, ENTRY_OID(n + 0));
	buf1 = fill_tree_descriptor(t + 1, ENTRY_OID(n + 1));
	buf2 = fill_tree_descriptor(t + 2, ENTRY_OID(n + 2));
#undef ENTRY_OID

	merge_trees(t, newbase);

	free(buf0);
	free(buf1);
	free(buf2);
	free(newbase);
}


static struct merge_list *link_entry(unsigned stage, const struct traverse_info *info, struct name_entry *n, struct merge_list *entry)
{
	const char *path;
	struct merge_list *link;

	if (!n->mode)
		return entry;
	if (entry)
		path = entry->path;
	else
		path = traverse_path(info, n);
	link = create_entry(stage, n->mode, n->oid, path);
	link->link = entry;
	return link;
}

static void unresolved(const struct traverse_info *info, struct name_entry n[3])
{
	struct merge_list *entry = NULL;
	int i;
	unsigned dirmask = 0, mask = 0;

	for (i = 0; i < 3; i++) {
		mask |= (1 << i);
		/*
		 * Treat missing entries as directories so that we return
		 * after unresolved_directory has handled this.
		 */
		if (!n[i].mode || S_ISDIR(n[i].mode))
			dirmask |= (1 << i);
	}

	unresolved_directory(info, n);

	if (dirmask == mask)
		return;

	if (n[2].mode && !S_ISDIR(n[2].mode))
		entry = link_entry(3, info, n + 2, entry);
	if (n[1].mode && !S_ISDIR(n[1].mode))
		entry = link_entry(2, info, n + 1, entry);
	if (n[0].mode && !S_ISDIR(n[0].mode))
		entry = link_entry(1, info, n + 0, entry);

	add_merge_entry(entry);
}

/*
 * Merge two trees together (t[1] and t[2]), using a common base (t[0])
 * as the origin.
 *
 * This walks the (sorted) trees in lock-step, checking every possible
 * name. Note that directories automatically sort differently from other
 * files (see "base_name_compare"), so you'll never see file/directory
 * conflicts, because they won't ever compare the same.
 *
 * IOW, if a directory changes to a filename, it will automatically be
 * seen as the directory going away, and the filename being created.
 *
 * Think of this as a three-way diff.
 *
 * The output will be either:
 *  - successful merge
 *	 "0 mode sha1 filename"
 *    NOTE NOTE NOTE! FIXME! We really really need to walk the index
 *    in parallel with this too!
 *
 *  - conflict:
 *	"1 mode sha1 filename"
 *	"2 mode sha1 filename"
 *	"3 mode sha1 filename"
 *    where not all of the 1/2/3 lines may exist, of course.
 *
 * The successful merge rules are the same as for the three-way merge
 * in git-read-tree.
 */
static int threeway_callback(int n, unsigned long mask, unsigned long dirmask, struct name_entry *entry, struct traverse_info *info)
{
	/* Same in both? */
	if (same_entry(entry+1, entry+2) || both_empty(entry+1, entry+2)) {
		/* Modified, added or removed identically */
		resolve(info, NULL, entry+1);
		return mask;
	}

	if (same_entry(entry+0, entry+1)) {
		if (entry[2].oid && !S_ISDIR(entry[2].mode)) {
			/* We did not touch, they modified -- take theirs */
			resolve(info, entry+1, entry+2);
			return mask;
		}
		/*
		 * If we did not touch a directory but they made it
		 * into a file, we fall through and unresolved()
		 * recurses down.  Likewise for the opposite case.
		 */
	}

	if (same_entry(entry+0, entry+2) || both_empty(entry+0, entry+2)) {
		/* We added, modified or removed, they did not touch -- take ours */
		resolve(info, NULL, entry+1);
		return mask;
	}

	unresolved(info, entry);
	return mask;
}

static void merge_trees(struct tree_desc t[3], const char *base)
{
	struct traverse_info info;

	setup_traverse_info(&info, base);
	info.fn = threeway_callback;
	traverse_trees(3, t, &info);
}

static void *get_tree_descriptor(struct tree_desc *desc, const char *rev)
{
	struct object_id oid;
	void *buf;

	if (get_oid(rev, &oid))
		die("unknown rev %s", rev);
	buf = fill_tree_descriptor(desc, &oid);
	if (!buf)
		die("%s is not a tree", rev);
	return buf;
}

int cmd_merge_tree(int argc, const char **argv, const char *prefix)
{
	struct tree_desc t[3];
	void *buf1, *buf2, *buf3;

	if (argc != 4)
		usage(merge_tree_usage);

	buf1 = get_tree_descriptor(t+0, argv[1]);
	buf2 = get_tree_descriptor(t+1, argv[2]);
	buf3 = get_tree_descriptor(t+2, argv[3]);
	merge_trees(t, "");
	free(buf1);
	free(buf2);
	free(buf3);

	show_result();
	return 0;
}
