/*
 * GIT - The information manager from hell
 *
 * Copyright (C) Linus Torvalds, 2005
 */
#include "cache.h"
#include "strbuf.h"
#include "quote.h"

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
static int allow_unmerged; /* --refresh needing merge is not error */
static int not_new; /* --refresh not having working tree files is not error */
static int quiet; /* --refresh needing update is not error */
static int info_only;
static int force_remove;
static int verbose;

/* Three functions to allow overloaded pointer return; see linux/err.h */
static inline void *ERR_PTR(long error)
{
	return (void *) error;
}

static inline long PTR_ERR(const void *ptr)
{
	return (long) ptr;
}

static inline long IS_ERR(const void *ptr)
{
	return (unsigned long)ptr > (unsigned long)-1000L;
}

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

static int add_file_to_cache(const char *path)
{
	int size, namelen, option, status;
	struct cache_entry *ce;
	struct stat st;

	status = lstat(path, &st);
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
	ce = xmalloc(size);
	memset(ce, 0, size);
	memcpy(ce->name, path, namelen);
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
	ce->ce_flags = htons(namelen);

	if (index_path(ce->sha1, path, &st, !info_only))
		return -1;
	option = allow_add ? ADD_CACHE_OK_TO_ADD : 0;
	option |= allow_replace ? ADD_CACHE_OK_TO_REPLACE : 0;
	if (add_cache_entry(ce, option))
		return error("%s: cannot add to the index - missing --add option?",
			     path);
	return 0;
}

/*
 * "refresh" does not calculate a new sha1 file or bring the
 * cache up-to-date for mode/content changes. But what it
 * _does_ do is to "re-match" the stat information of a file
 * with the cache, so that you can refresh the cache for a
 * file that hasn't been changed but where the stat entry is
 * out of date.
 *
 * For example, you'd want to do this after doing a "git-read-tree",
 * to link up the stat cache details with the proper files.
 */
static struct cache_entry *refresh_entry(struct cache_entry *ce)
{
	struct stat st;
	struct cache_entry *updated;
	int changed, size;

	if (lstat(ce->name, &st) < 0)
		return ERR_PTR(-errno);

	changed = ce_match_stat(ce, &st);
	if (!changed)
		return NULL;

	if (ce_modified(ce, &st))
		return ERR_PTR(-EINVAL);

	size = ce_size(ce);
	updated = xmalloc(size);
	memcpy(updated, ce, size);
	fill_stat_cache_info(updated, &st);
	return updated;
}

static int refresh_cache(void)
{
	int i;
	int has_errors = 0;

	for (i = 0; i < active_nr; i++) {
		struct cache_entry *ce, *new;
		ce = active_cache[i];
		if (ce_stage(ce)) {
			while ((i < active_nr) &&
			       ! strcmp(active_cache[i]->name, ce->name))
				i++;
			i--;
			if (allow_unmerged)
				continue;
			printf("%s: needs merge\n", ce->name);
			has_errors = 1;
			continue;
		}

		new = refresh_entry(ce);
		if (!new)
			continue;
		if (IS_ERR(new)) {
			if (not_new && PTR_ERR(new) == -ENOENT)
				continue;
			if (quiet)
				continue;
			printf("%s: needs update\n", ce->name);
			has_errors = 1;
			continue;
		}
		active_cache_changed = 1;
		/* You can NOT just free active_cache[i] here, since it
		 * might not be necessarily malloc()ed but can also come
		 * from mmap(). */
		active_cache[i] = new;
	}
	return has_errors;
}

/*
 * We fundamentally don't like some paths: we don't want
 * dot or dot-dot anywhere, and for obvious reasons don't
 * want to recurse into ".git" either.
 *
 * Also, we don't want double slashes or slashes at the
 * end that can make pathnames ambiguous.
 */
static int verify_dotfile(const char *rest)
{
	/*
	 * The first character was '.', but that
	 * has already been discarded, we now test
	 * the rest.
	 */
	switch (*rest) {
	/* "." is not allowed */
	case '\0': case '/':
		return 0;

	/*
	 * ".git" followed by  NUL or slash is bad. This
	 * shares the path end test with the ".." case.
	 */
	case 'g':
		if (rest[1] != 'i')
			break;
		if (rest[2] != 't')
			break;
		rest += 2;
	/* fallthrough */
	case '.':
		if (rest[1] == '\0' || rest[1] == '/')
			return 0;
	}
	return 1;
}

static int verify_path(const char *path)
{
	char c;

	goto inside;
	for (;;) {
		if (!c)
			return 1;
		if (c == '/') {
inside:
			c = *path++;
			switch (c) {
			default:
				continue;
			case '/': case '\0':
				break;
			case '.':
				if (verify_dotfile(path))
					continue;
			}
			return 0;
		}
		c = *path++;
	}
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
	ce = xmalloc(size);
	memset(ce, 0, size);

	memcpy(ce->sha1, sha1, 20);
	memcpy(ce->name, path, len);
	ce->ce_flags = create_ce_flags(len, stage);
	ce->ce_mode = create_ce_mode(mode);
	option = allow_add ? ADD_CACHE_OK_TO_ADD : 0;
	option |= allow_replace ? ADD_CACHE_OK_TO_REPLACE : 0;
	if (add_cache_entry(ce, option))
		return error("%s: cannot add to the index - missing --add option?",
			     path);
	report("add '%s'", path);
	return 0;
}

static int chmod_path(int flip, const char *path)
{
	int pos;
	struct cache_entry *ce;
	unsigned int mode;

	pos = cache_name_pos(path, strlen(path));
	if (pos < 0)
		return -1;
	ce = active_cache[pos];
	mode = ntohl(ce->ce_mode);
	if (!S_ISREG(mode))
		return -1;
	switch (flip) {
	case '+':
		ce->ce_mode |= htonl(0111); break;
	case '-':
		ce->ce_mode &= htonl(~0111); break;
	default:
		return -1;
	}
	active_cache_changed = 1;
	return 0;
}

static struct cache_file cache_file;

static void update_one(const char *path, const char *prefix, int prefix_length)
{
	const char *p = prefix_path(prefix, prefix_length, path);
	if (!verify_path(p)) {
		fprintf(stderr, "Ignoring path %s\n", path);
		return;
	}
	if (force_remove) {
		if (remove_file_from_cache(p))
			die("git-update-index: unable to remove %s", path);
		report("remove '%s'", path);
		return;
	}
	if (add_file_to_cache(p))
		die("Unable to process file %s", path);
	report("add '%s'", path);
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

		if (tab[-2] == ' ' && '1' <= tab[-1] && tab[-1] <= '3') {
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
"git-update-index [-q] [--add] [--replace] [--remove] [--unmerged] [--refresh] [--cacheinfo] [--chmod=(+|-)x] [--info-only] [--force-remove] [--stdin] [--index-info] [--ignore-missing] [-z] [--verbose] [--] <file>...";

int main(int argc, const char **argv)
{
	int i, newfd, entries, has_errors = 0, line_termination = '\n';
	int allow_options = 1;
	int read_from_stdin = 0;
	const char *prefix = setup_git_directory();
	int prefix_length = prefix ? strlen(prefix) : 0;

	git_config(git_default_config);

	newfd = hold_index_file_for_update(&cache_file, get_index_file());
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
				quiet = 1;
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
				allow_unmerged = 1;
				continue;
			}
			if (!strcmp(path, "--refresh")) {
				has_errors |= refresh_cache();
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
				if (chmod_path(path[8], argv[++i]))
					die("git-update-index: %s cannot chmod %s", path, argv[i]);
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
				allow_add = allow_replace = allow_remove = 1;
				read_index_info(line_termination);
				continue;
			}
			if (!strcmp(path, "--ignore-missing")) {
				not_new = 1;
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
	}
	if (read_from_stdin) {
		struct strbuf buf;
		strbuf_init(&buf);
		while (1) {
			char *path_name;
			read_line(&buf, stdin, line_termination);
			if (buf.eof)
				break;
			if (line_termination && buf.buf[0] == '"')
				path_name = unquote_c_style(buf.buf, NULL);
			else
				path_name = buf.buf;
			update_one(path_name, prefix, prefix_length);
			if (path_name != buf.buf)
				free(path_name);
		}
	}
	if (active_cache_changed) {
		if (write_cache(newfd, active_cache, active_nr) ||
		    commit_index_file(&cache_file))
			die("Unable to write new cachefile");
	}

	return has_errors ? 1 : 0;
}
