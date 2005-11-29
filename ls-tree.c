/*
 * GIT - The information manager from hell
 *
 * Copyright (C) Linus Torvalds, 2005
 */
#include "cache.h"
#include "blob.h"
#include "tree.h"
#include "quote.h"

static int line_termination = '\n';
#define LS_RECURSIVE 1
#define LS_TREE_ONLY 2
static int ls_options = 0;

static struct tree_entry_list root_entry;

static void prepare_root(unsigned char *sha1)
{
	unsigned char rsha[20];
	unsigned long size;
	void *buf;
	struct tree *root_tree;

	buf = read_object_with_reference(sha1, "tree", &size, rsha);
	free(buf);
	if (!buf)
		die("Could not read %s", sha1_to_hex(sha1));

	root_tree = lookup_tree(rsha);
	if (!root_tree)
		die("Could not read %s", sha1_to_hex(sha1));

	/* Prepare a fake entry */
	root_entry.directory = 1;
	root_entry.executable = root_entry.symlink = 0;
	root_entry.mode = S_IFDIR;
	root_entry.name = "";
	root_entry.item.tree = root_tree;
	root_entry.parent = NULL;
}

static int prepare_children(struct tree_entry_list *elem)
{
	if (!elem->directory)
		return -1;
	if (!elem->item.tree->object.parsed) {
		struct tree_entry_list *e;
		if (parse_tree(elem->item.tree))
			return -1;
		/* Set up the parent link */
		for (e = elem->item.tree->entries; e; e = e->next)
			e->parent = elem;
	}
	return 0;
}

static struct tree_entry_list *find_entry(const char *path, char *pathbuf)
{
	const char *next, *slash;
	int len;
	struct tree_entry_list *elem = &root_entry, *oldelem = NULL;

	*(pathbuf) = '\0';

	/* Find tree element, descending from root, that
	 * corresponds to the named path, lazily expanding
	 * the tree if possible.
	 */

	while (path) {
		/* The fact we still have path means that the caller
		 * wants us to make sure that elem at this point is a
		 * directory, and possibly descend into it.  Even what
		 * is left is just trailing slashes, we loop back to
		 * here, and this call to prepare_children() will
		 * catch elem not being a tree.  Nice.
		 */
		if (prepare_children(elem))
			return NULL;

		slash = strchr(path, '/');
		if (!slash) {
			len = strlen(path);
			next = NULL;
		}
		else {
			next = slash + 1;
			len = slash - path;
		}
		if (len) {
			if (oldelem) {
				pathbuf += sprintf(pathbuf, "%s/", oldelem->name);
			}

			/* (len == 0) if the original path was "drivers/char/"
			 * and we have run already two rounds, having elem
			 * pointing at the drivers/char directory.
			 */
			elem = elem->item.tree->entries;
			while (elem) {
				if ((strlen(elem->name) == len) &&
				    !strncmp(elem->name, path, len)) {
					/* found */
					break;
				}
				elem = elem->next;
			}
			if (!elem)
				return NULL;

			oldelem = elem;
		}
		path = next;
	}

	return elem;
}

static const char *entry_type(struct tree_entry_list *e)
{
	return (e->directory ? "tree" : "blob");
}

static const char *entry_hex(struct tree_entry_list *e)
{
	return sha1_to_hex(e->directory
			   ? e->item.tree->object.sha1
			   : e->item.blob->object.sha1);
}

/* forward declaration for mutually recursive routines */
static int show_entry(struct tree_entry_list *, int, char *pathbuf);

static int show_children(struct tree_entry_list *e, int level, char *pathbuf)
{
	int oldlen = strlen(pathbuf);

	if (e != &root_entry)
		sprintf(pathbuf + oldlen, "%s/", e->name);

	if (prepare_children(e))
		die("internal error: ls-tree show_children called with non tree");
	e = e->item.tree->entries;
	while (e) {
		show_entry(e, level, pathbuf);
		e = e->next;
	}

	pathbuf[oldlen] = '\0';

	return 0;
}

static int show_entry(struct tree_entry_list *e, int level, char *pathbuf)
{
	int err = 0; 

	if (e != &root_entry) {
		int pathlen = strlen(pathbuf);
		printf("%06o %s %s	",
		       e->mode, entry_type(e), entry_hex(e));
		write_name_quoted(pathbuf, pathlen, e->name,
				  line_termination, stdout);
		putchar(line_termination);
	}

	if (e->directory) {
		/* If this is a directory, we have the following cases:
		 * (1) This is the top-level request (explicit path from the
		 *     command line, or "root" if there is no command line).
		 *  a. Without any flag.  We show direct children.  We do not 
		 *     recurse into them.
		 *  b. With -r.  We do recurse into children.
		 *  c. With -d.  We do not recurse into children.
		 * (2) We came here because our caller is either (1-a) or
		 *     (1-b).
		 *  a. Without any flag.  We do not show our children (which
		 *     are grandchildren for the original request).
		 *  b. With -r.  We continue to recurse into our children.
		 *  c. With -d.  We should not have come here to begin with.
		 */
		if (level == 0 && !(ls_options & LS_TREE_ONLY))
			/* case (1)-a and (1)-b */
			err = err | show_children(e, level+1, pathbuf);
		else if (level && ls_options & LS_RECURSIVE)
			/* case (2)-b */
			err = err | show_children(e, level+1, pathbuf);
	}
	return err;
}

static int list_one(const char *path)
{
	int err = 0;
	char pathbuf[MAXPATHLEN + 1];
	struct tree_entry_list *e = find_entry(path, pathbuf);
	if (!e) {
		/* traditionally ls-tree does not complain about
		 * missing path.  We may change this later to match
		 * what "/bin/ls -a" does, which is to complain.
		 */
		return err;
	}
	err = err | show_entry(e, 0, pathbuf);
	return err;
}

static int list(char **path)
{
	int i;
	int err = 0;
	for (i = 0; path[i]; i++)
		err = err | list_one(path[i]);
	return err;
}

static const char ls_tree_usage[] =
	"git-ls-tree [-d] [-r] [-z] <tree-ish> [path...]";

int main(int argc, char **argv)
{
	static char *path0[] = { "", NULL };
	char **path;
	unsigned char sha1[20];

	while (1 < argc && argv[1][0] == '-') {
		switch (argv[1][1]) {
		case 'z':
			line_termination = 0;
			break;
		case 'r':
			ls_options |= LS_RECURSIVE;
			break;
		case 'd':
			ls_options |= LS_TREE_ONLY;
			break;
		default:
			usage(ls_tree_usage);
		}
		argc--; argv++;
	}

	if (argc < 2)
		usage(ls_tree_usage);
	if (get_sha1(argv[1], sha1) < 0)
		usage(ls_tree_usage);

	path = (argc == 2) ? path0 : (argv + 2);
	prepare_root(sha1);
	if (list(path) < 0)
		die("list failed");
	return 0;
}
