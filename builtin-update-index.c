/*
 * GIT - The information manager from hell
 *
 * Copyright (C) Linus Torvalds, 2005
 */
#include "cache.h"
#include "strbuf.h"
#include "quote.h"
#include "cache-tree.h"
#include "tree-walk.h"
#include "builtin.h"

/*
 * Default to not allowing changes to the list of files. The
 * tool doesn't actually care, but this makes it harder to add
 * files to the revision control by mistake by doing something
 * like "git-update-index *" and suddenly having all the object
 * files be revision controlled.
 */
static int allow_add;
static int allow_remove;
static int allow_replace;
static int info_only;
static int force_remove;
static int verbose;
static int mark_valid_only = 0;
#define MARK_VALID 1
#define UNMARK_VALID 2

static void report(const char *fmt, ...)
{
	va_list vp;

	if (!verbose)
		return;

	va_start(vp, fmt);
	vprintf(fmt, vp);
	putchar('\n');
	va_end(vp);
}

static int mark_valid(const char *path)
{
	int namelen = strlen(path);
	int pos = cache_name_pos(path, namelen);
	if (0 <= pos) {
		switch (mark_valid_only) {
		case MARK_VALID:
			active_cache[pos]->ce_flags |= htons(CE_VALID);
			break;
		case UNMARK_VALID:
			active_cache[pos]->ce_flags &= ~htons(CE_VALID);
			break;
		}
		cache_tree_invalidate_path(active_cache_tree, path);
		active_cache_changed = 1;
		return 0;
	}
	return -1;
}

static int add_file_to_cache(const char *path)
{
	int size, namelen, option, status;
	struct cache_entry *ce;
	struct stat st;

	status = lstat(path, &st);

	/* We probably want to do this in remove_file_from_cache() and
	 * add_cache_entry() instead...
	 */
	cache_tree_invalidate_path(active_cache_tree, path);

	if (status < 0 || S_ISDIR(st.st_mode)) {
		/* When we used to have "path" and now we want to add
		 * "path/file", we need a way to remove "path" before
		 * being able to add "path/file".  However,
		 * "git-update-index --remove path" would not work.
		 * --force-remove can be used but this is more user
		 * friendly, especially since we can do the opposite
		 * case just fine without --force-remove.
		 */
		if (status == 0 || (errno == ENOENT || errno == ENOTDIR)) {
			if (allow_remove) {
				if (remove_file_from_cache(path))
					return error("%s: cannot remove from the index",
					             path);
				else
					return 0;
			} else if (status < 0) {
				return error("%s: does not exist and --remove not passed",
				             path);
			}
		}
		if (0 == status)
			return error("%s: is a directory - add files inside instead",
			             path);
		else
			return error("lstat(\"%s\"): %s", path,
				     strerror(errno));
	}

	namelen = strlen(path);
	size = cache_entry_size(namelen);
	ce = xcalloc(1, size);
	memcpy(ce->name, path, namelen);
	ce->ce_flags = htons(namelen);
	fill_stat_cache_info(ce, &st);

	ce->ce_mode = create_ce_mode(st.st_mode);
	if (!trust_executable_bit) {
		/* If there is an existing entry, pick the mode bits
		 * from it.
		 */
		int pos = cache_name_pos(path, namelen);
		if (0 <= pos)
			ce->ce_mode = active_cache[pos]->ce_mode;
	}

	if (index_path(ce->sha1, path, &st, !info_only))
		return -1;
	option = allow_add ? ADD_CACHE_OK_TO_ADD : 0;
	option |= allow_replace ? ADD_CACHE_OK_TO_REPLACE : 0;
	if (add_cache_entry(ce, option))
		return error("%s: cannot add to the index - missing --add option?",
			     path);
	return 0;
}

static int add_cacheinfo(unsigned int mode, const unsigned char *sha1,
			 const char *path, int stage)
{
	int size, len, option;
	struct cache_entry *ce;

	if (!verify_path(path))
		return -1;

	len = strlen(path);
	size = cache_entry_size(len);
	ce = xcalloc(1, size);

	memcpy(ce->sha1, sha1, 20);
	memcpy(ce->name, path, len);
	ce->ce_flags = create_ce_flags(len, stage);
	ce->ce_mode = create_ce_mode(mode);
	if (assume_unchanged)
		ce->ce_flags |= htons(CE_VALID);
	option = allow_add ? ADD_CACHE_OK_TO_ADD : 0;
	option |= allow_replace ? ADD_CACHE_OK_TO_REPLACE : 0;
	if (add_cache_entry(ce, option))
		return error("%s: cannot add to the index - missing --add option?",
			     path);
	report("add '%s'", path);
	cache_tree_invalidate_path(active_cache_tree, path);
	return 0;
}

static void chmod_path(int flip, const char *path)
{
	int pos;
	struct cache_entry *ce;
	unsigned int mode;

	pos = cache_name_pos(path, strlen(path));
	if (pos < 0)
		goto fail;
	ce = active_cache[pos];
	mode = ntohl(ce->ce_mode);
	if (!S_ISREG(mode))
		goto fail;
	switch (flip) {
	case '+':
		ce->ce_mode |= htonl(0111); break;
	case '-':
		ce->ce_mode &= htonl(~0111); break;
	default:
		goto fail;
	}
	cache_tree_invalidate_path(active_cache_tree, path);
	active_cache_changed = 1;
	report("chmod %cx '%s'", flip, path);
	return;
 fail:
	die("git-update-index: cannot chmod %cx '%s'", flip, path);
}

static void update_one(const char *path, const char *prefix, int prefix_length)
{
	const char *p = prefix_path(prefix, prefix_length, path);
	if (!verify_path(p)) {
		fprintf(stderr, "Ignoring path %s\n", path);
		goto free_return;
	}
	if (mark_valid_only) {
		if (mark_valid(p))
			die("Unable to mark file %s", path);
		goto free_return;
	}
	cache_tree_invalidate_path(active_cache_tree, path);

	if (force_remove) {
		if (remove_file_from_cache(p))
			die("git-update-index: unable to remove %s", path);
		report("remove '%s'", path);
		goto free_return;
	}
	if (add_file_to_cache(p))
		die("Unable to process file %s", path);
	report("add '%s'", path);
 free_return:
	if (p < path || p > path + strlen(path))
		free((char*)p);
}

static void read_index_info(int line_termination)
{
	struct strbuf buf;
	strbuf_init(&buf);
	while (1) {
		char *ptr, *tab;
		char *path_name;
		unsigned char sha1[20];
		unsigned int mode;
		int stage;

		/* This reads lines formatted in one of three formats:
		 *
		 * (1) mode         SP sha1          TAB path
		 * The first format is what "git-apply --index-info"
		 * reports, and used to reconstruct a partial tree
		 * that is used for phony merge base tree when falling
		 * back on 3-way merge.
		 *
		 * (2) mode SP type SP sha1          TAB path
		 * The second format is to stuff git-ls-tree output
		 * into the index file.
		 *
		 * (3) mode         SP sha1 SP stage TAB path
		 * This format is to put higher order stages into the
		 * index file and matches git-ls-files --stage output.
		 */
		read_line(&buf, stdin, line_termination);
		if (buf.eof)
			break;

		mode = strtoul(buf.buf, &ptr, 8);
		if (ptr == buf.buf || *ptr != ' ')
			goto bad_line;

		tab = strchr(ptr, '\t');
		if (!tab || tab - ptr < 41)
			goto bad_line;

		if (tab[-2] == ' ' && '0' <= tab[-1] && tab[-1] <= '3') {
			stage = tab[-1] - '0';
			ptr = tab + 1; /* point at the head of path */
			tab = tab - 2; /* point at tail of sha1 */
		}
		else {
			stage = 0;
			ptr = tab + 1; /* point at the head of path */
		}

		if (get_sha1_hex(tab - 40, sha1) || tab[-41] != ' ')
			goto bad_line;

		if (line_termination && ptr[0] == '"')
			path_name = unquote_c_style(ptr, NULL);
		else
			path_name = ptr;

		if (!verify_path(path_name)) {
			fprintf(stderr, "Ignoring path %s\n", path_name);
			if (path_name != ptr)
				free(path_name);
			continue;
		}
		cache_tree_invalidate_path(active_cache_tree, path_name);

		if (!mode) {
			/* mode == 0 means there is no such path -- remove */
			if (remove_file_from_cache(path_name))
				die("git-update-index: unable to remove %s",
				    ptr);
		}
		else {
			/* mode ' ' sha1 '\t' name
			 * ptr[-1] points at tab,
			 * ptr[-41] is at the beginning of sha1
			 */
			ptr[-42] = ptr[-1] = 0;
			if (add_cacheinfo(mode, sha1, path_name, stage))
				die("git-update-index: unable to update %s",
				    path_name);
		}
		if (path_name != ptr)
			free(path_name);
		continue;

	bad_line:
		die("malformed index info %s", buf.buf);
	}
}

static const char update_index_usage[] =
"git-update-index [-q] [--add] [--replace] [--remove] [--unmerged] [--refresh] [--really-refresh] [--cacheinfo] [--chmod=(+|-)x] [--assume-unchanged] [--info-only] [--force-remove] [--stdin] [--index-info] [--unresolve] [--again] [--ignore-missing] [-z] [--verbose] [--] <file>...";

static unsigned char head_sha1[20];
static unsigned char merge_head_sha1[20];

static struct cache_entry *read_one_ent(const char *which,
					unsigned char *ent, const char *path,
					int namelen, int stage)
{
	unsigned mode;
	unsigned char sha1[20];
	int size;
	struct cache_entry *ce;

	if (get_tree_entry(ent, path, sha1, &mode)) {
		if (which)
			error("%s: not in %s branch.", path, which);
		return NULL;
	}
	if (mode == S_IFDIR) {
		if (which)
			error("%s: not a blob in %s branch.", path, which);
		return NULL;
	}
	size = cache_entry_size(namelen);
	ce = xcalloc(1, size);

	memcpy(ce->sha1, sha1, 20);
	memcpy(ce->name, path, namelen);
	ce->ce_flags = create_ce_flags(namelen, stage);
	ce->ce_mode = create_ce_mode(mode);
	return ce;
}

static int unresolve_one(const char *path)
{
	int namelen = strlen(path);
	int pos;
	int ret = 0;
	struct cache_entry *ce_2 = NULL, *ce_3 = NULL;

	/* See if there is such entry in the index. */
	pos = cache_name_pos(path, namelen);
	if (pos < 0) {
		/* If there isn't, either it is unmerged, or
		 * resolved as "removed" by mistake.  We do not
		 * want to do anything in the former case.
		 */
		pos = -pos-1;
		if (pos < active_nr) {
			struct cache_entry *ce = active_cache[pos];
			if (ce_namelen(ce) == namelen &&
			    !memcmp(ce->name, path, namelen)) {
				fprintf(stderr,
					"%s: skipping still unmerged path.\n",
					path);
				goto free_return;
			}
		}
	}

	/* Grab blobs from given path from HEAD and MERGE_HEAD,
	 * stuff HEAD version in stage #2,
	 * stuff MERGE_HEAD version in stage #3.
	 */
	ce_2 = read_one_ent("our", head_sha1, path, namelen, 2);
	ce_3 = read_one_ent("their", merge_head_sha1, path, namelen, 3);

	if (!ce_2 || !ce_3) {
		ret = -1;
		goto free_return;
	}
	if (!memcmp(ce_2->sha1, ce_3->sha1, 20) &&
	    ce_2->ce_mode == ce_3->ce_mode) {
		fprintf(stderr, "%s: identical in both, skipping.\n",
			path);
		goto free_return;
	}

	cache_tree_invalidate_path(active_cache_tree, path);
	remove_file_from_cache(path);
	if (add_cache_entry(ce_2, ADD_CACHE_OK_TO_ADD)) {
		error("%s: cannot add our version to the index.", path);
		ret = -1;
		goto free_return;
	}
	if (!add_cache_entry(ce_3, ADD_CACHE_OK_TO_ADD))
		return 0;
	error("%s: cannot add their version to the index.", path);
	ret = -1;
 free_return:
	free(ce_2);
	free(ce_3);
	return ret;
}

static void read_head_pointers(void)
{
	if (read_ref(git_path("HEAD"), head_sha1))
		die("No HEAD -- no initial commit yet?\n");
	if (read_ref(git_path("MERGE_HEAD"), merge_head_sha1)) {
		fprintf(stderr, "Not in the middle of a merge.\n");
		exit(0);
	}
}

static int do_unresolve(int ac, const char **av,
			const char *prefix, int prefix_length)
{
	int i;
	int err = 0;

	/* Read HEAD and MERGE_HEAD; if MERGE_HEAD does not exist, we
	 * are not doing a merge, so exit with success status.
	 */
	read_head_pointers();

	for (i = 1; i < ac; i++) {
		const char *arg = av[i];
		const char *p = prefix_path(prefix, prefix_length, arg);
		err |= unresolve_one(p);
		if (p < arg || p > arg + strlen(arg))
			free((char*)p);
	}
	return err;
}

static int do_reupdate(int ac, const char **av,
		       const char *prefix, int prefix_length)
{
	/* Read HEAD and run update-index on paths that are
	 * merged and already different between index and HEAD.
	 */
	int pos;
	int has_head = 1;
	const char **pathspec = get_pathspec(prefix, av + 1);

	if (read_ref(git_path("HEAD"), head_sha1))
		/* If there is no HEAD, that means it is an initial
		 * commit.  Update everything in the index.
		 */
		has_head = 0;
 redo:
	for (pos = 0; pos < active_nr; pos++) {
		struct cache_entry *ce = active_cache[pos];
		struct cache_entry *old = NULL;
		int save_nr;

		if (ce_stage(ce) || !ce_path_match(ce, pathspec))
			continue;
		if (has_head)
			old = read_one_ent(NULL, head_sha1,
					   ce->name, ce_namelen(ce), 0);
		if (old && ce->ce_mode == old->ce_mode &&
		    !memcmp(ce->sha1, old->sha1, 20)) {
			free(old);
			continue; /* unchanged */
		}
		/* Be careful.  The working tree may not have the
		 * path anymore, in which case, under 'allow_remove',
		 * or worse yet 'allow_replace', active_nr may decrease.
		 */
		save_nr = active_nr;
		update_one(ce->name + prefix_length, prefix, prefix_length);
		if (save_nr != active_nr)
			goto redo;
	}
	return 0;
}

int cmd_update_index(int argc, const char **argv, const char *prefix)
{
	int i, newfd, entries, has_errors = 0, line_termination = '\n';
	int allow_options = 1;
	int read_from_stdin = 0;
	int prefix_length = prefix ? strlen(prefix) : 0;
	char set_executable_bit = 0;
	unsigned int refresh_flags = 0;
	struct lock_file *lock_file;

	git_config(git_default_config);

	/* We can't free this memory, it becomes part of a linked list parsed atexit() */
	lock_file = xcalloc(1, sizeof(struct lock_file));

	newfd = hold_lock_file_for_update(lock_file, get_index_file());
	if (newfd < 0)
		die("unable to create new cachefile");

	entries = read_cache();
	if (entries < 0)
		die("cache corrupted");

	for (i = 1 ; i < argc; i++) {
		const char *path = argv[i];

		if (allow_options && *path == '-') {
			if (!strcmp(path, "--")) {
				allow_options = 0;
				continue;
			}
			if (!strcmp(path, "-q")) {
				refresh_flags |= REFRESH_QUIET;
				continue;
			}
			if (!strcmp(path, "--add")) {
				allow_add = 1;
				continue;
			}
			if (!strcmp(path, "--replace")) {
				allow_replace = 1;
				continue;
			}
			if (!strcmp(path, "--remove")) {
				allow_remove = 1;
				continue;
			}
			if (!strcmp(path, "--unmerged")) {
				refresh_flags |= REFRESH_UNMERGED;
				continue;
			}
			if (!strcmp(path, "--refresh")) {
				has_errors |= refresh_cache(refresh_flags);
				continue;
			}
			if (!strcmp(path, "--really-refresh")) {
				has_errors |= refresh_cache(REFRESH_REALLY | refresh_flags);
				continue;
			}
			if (!strcmp(path, "--cacheinfo")) {
				unsigned char sha1[20];
				unsigned int mode;

				if (i+3 >= argc)
					die("git-update-index: --cacheinfo <mode> <sha1> <path>");

				if ((sscanf(argv[i+1], "%o", &mode) != 1) ||
				    get_sha1_hex(argv[i+2], sha1) ||
				    add_cacheinfo(mode, sha1, argv[i+3], 0))
					die("git-update-index: --cacheinfo"
					    " cannot add %s", argv[i+3]);
				i += 3;
				continue;
			}
			if (!strcmp(path, "--chmod=-x") ||
			    !strcmp(path, "--chmod=+x")) {
				if (argc <= i+1)
					die("git-update-index: %s <path>", path);
				set_executable_bit = path[8];
				continue;
			}
			if (!strcmp(path, "--assume-unchanged")) {
				mark_valid_only = MARK_VALID;
				continue;
			}
			if (!strcmp(path, "--no-assume-unchanged")) {
				mark_valid_only = UNMARK_VALID;
				continue;
			}
			if (!strcmp(path, "--info-only")) {
				info_only = 1;
				continue;
			}
			if (!strcmp(path, "--force-remove")) {
				force_remove = 1;
				continue;
			}
			if (!strcmp(path, "-z")) {
				line_termination = 0;
				continue;
			}
			if (!strcmp(path, "--stdin")) {
				if (i != argc - 1)
					die("--stdin must be at the end");
				read_from_stdin = 1;
				break;
			}
			if (!strcmp(path, "--index-info")) {
				if (i != argc - 1)
					die("--index-info must be at the end");
				allow_add = allow_replace = allow_remove = 1;
				read_index_info(line_termination);
				break;
			}
			if (!strcmp(path, "--unresolve")) {
				has_errors = do_unresolve(argc - i, argv + i,
							  prefix, prefix_length);
				if (has_errors)
					active_cache_changed = 0;
				goto finish;
			}
			if (!strcmp(path, "--again")) {
				has_errors = do_reupdate(argc - i, argv + i,
							 prefix, prefix_length);
				if (has_errors)
					active_cache_changed = 0;
				goto finish;
			}
			if (!strcmp(path, "--ignore-missing")) {
				refresh_flags |= REFRESH_IGNORE_MISSING;
				continue;
			}
			if (!strcmp(path, "--verbose")) {
				verbose = 1;
				continue;
			}
			if (!strcmp(path, "-h") || !strcmp(path, "--help"))
				usage(update_index_usage);
			die("unknown option %s", path);
		}
		update_one(path, prefix, prefix_length);
		if (set_executable_bit)
			chmod_path(set_executable_bit, path);
	}
	if (read_from_stdin) {
		struct strbuf buf;
		strbuf_init(&buf);
		while (1) {
			char *path_name;
			const char *p;
			read_line(&buf, stdin, line_termination);
			if (buf.eof)
				break;
			if (line_termination && buf.buf[0] == '"')
				path_name = unquote_c_style(buf.buf, NULL);
			else
				path_name = buf.buf;
			p = prefix_path(prefix, prefix_length, path_name);
			update_one(p, NULL, 0);
			if (set_executable_bit)
				chmod_path(set_executable_bit, p);
			if (p < path_name || p > path_name + strlen(path_name))
				free((char*) p);
			if (path_name != buf.buf)
				free(path_name);
		}
	}

 finish:
	if (active_cache_changed) {
		if (write_cache(newfd, active_cache, active_nr) ||
		    close(newfd) || commit_lock_file(lock_file))
			die("Unable to write new index file");
	}

	rollback_lock_file(lock_file);

	return has_errors ? 1 : 0;
}
