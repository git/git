/*
 * This merges the file listing in the directory cache index
 * with the actual working directory list, and shows different
 * combinations of the two.
 *
 * Copyright (C) Linus Torvalds, 2005
 */
#include <dirent.h>
#include <fnmatch.h>

#include "cache.h"
#include "quote.h"
#include "builtin.h"

static int abbrev = 0;
static int show_deleted = 0;
static int show_cached = 0;
static int show_others = 0;
static int show_ignored = 0;
static int show_stage = 0;
static int show_unmerged = 0;
static int show_modified = 0;
static int show_killed = 0;
static int show_other_directories = 0;
static int hide_empty_directories = 0;
static int show_valid_bit = 0;
static int line_terminator = '\n';

static int prefix_len = 0, prefix_offset = 0;
static const char *prefix = NULL;
static const char **pathspec = NULL;
static int error_unmatch = 0;
static char *ps_matched = NULL;

static const char *tag_cached = "";
static const char *tag_unmerged = "";
static const char *tag_removed = "";
static const char *tag_other = "";
static const char *tag_killed = "";
static const char *tag_modified = "";

static const char *exclude_per_dir = NULL;

/* We maintain three exclude pattern lists:
 * EXC_CMDL lists patterns explicitly given on the command line.
 * EXC_DIRS lists patterns obtained from per-directory ignore files.
 * EXC_FILE lists patterns from fallback ignore files.
 */
#define EXC_CMDL 0
#define EXC_DIRS 1
#define EXC_FILE 2
static struct exclude_list {
	int nr;
	int alloc;
	struct exclude {
		const char *pattern;
		const char *base;
		int baselen;
	} **excludes;
} exclude_list[3];

static void add_exclude(const char *string, const char *base,
			int baselen, struct exclude_list *which)
{
	struct exclude *x = xmalloc(sizeof (*x));

	x->pattern = string;
	x->base = base;
	x->baselen = baselen;
	if (which->nr == which->alloc) {
		which->alloc = alloc_nr(which->alloc);
		which->excludes = realloc(which->excludes,
					  which->alloc * sizeof(x));
	}
	which->excludes[which->nr++] = x;
}

static int add_excludes_from_file_1(const char *fname,
				    const char *base,
				    int baselen,
				    struct exclude_list *which)
{
	int fd, i;
	long size;
	char *buf, *entry;

	fd = open(fname, O_RDONLY);
	if (fd < 0)
		goto err;
	size = lseek(fd, 0, SEEK_END);
	if (size < 0)
		goto err;
	lseek(fd, 0, SEEK_SET);
	if (size == 0) {
		close(fd);
		return 0;
	}
	buf = xmalloc(size+1);
	if (read(fd, buf, size) != size)
		goto err;
	close(fd);

	buf[size++] = '\n';
	entry = buf;
	for (i = 0; i < size; i++) {
		if (buf[i] == '\n') {
			if (entry != buf + i && entry[0] != '#') {
				buf[i - (i && buf[i-1] == '\r')] = 0;
				add_exclude(entry, base, baselen, which);
			}
			entry = buf + i + 1;
		}
	}
	return 0;

 err:
	if (0 <= fd)
		close(fd);
	return -1;
}

static void add_excludes_from_file(const char *fname)
{
	if (add_excludes_from_file_1(fname, "", 0,
				     &exclude_list[EXC_FILE]) < 0)
		die("cannot use %s as an exclude file", fname);
}

static int push_exclude_per_directory(const char *base, int baselen)
{
	char exclude_file[PATH_MAX];
	struct exclude_list *el = &exclude_list[EXC_DIRS];
	int current_nr = el->nr;

	if (exclude_per_dir) {
		memcpy(exclude_file, base, baselen);
		strcpy(exclude_file + baselen, exclude_per_dir);
		add_excludes_from_file_1(exclude_file, base, baselen, el);
	}
	return current_nr;
}

static void pop_exclude_per_directory(int stk)
{
	struct exclude_list *el = &exclude_list[EXC_DIRS];

	while (stk < el->nr)
		free(el->excludes[--el->nr]);
}

/* Scan the list and let the last match determines the fate.
 * Return 1 for exclude, 0 for include and -1 for undecided.
 */
static int excluded_1(const char *pathname,
		      int pathlen,
		      struct exclude_list *el)
{
	int i;

	if (el->nr) {
		for (i = el->nr - 1; 0 <= i; i--) {
			struct exclude *x = el->excludes[i];
			const char *exclude = x->pattern;
			int to_exclude = 1;

			if (*exclude == '!') {
				to_exclude = 0;
				exclude++;
			}

			if (!strchr(exclude, '/')) {
				/* match basename */
				const char *basename = strrchr(pathname, '/');
				basename = (basename) ? basename+1 : pathname;
				if (fnmatch(exclude, basename, 0) == 0)
					return to_exclude;
			}
			else {
				/* match with FNM_PATHNAME:
				 * exclude has base (baselen long) implicitly
				 * in front of it.
				 */
				int baselen = x->baselen;
				if (*exclude == '/')
					exclude++;

				if (pathlen < baselen ||
				    (baselen && pathname[baselen-1] != '/') ||
				    strncmp(pathname, x->base, baselen))
				    continue;

				if (fnmatch(exclude, pathname+baselen,
					    FNM_PATHNAME) == 0)
					return to_exclude;
			}
		}
	}
	return -1; /* undecided */
}

static int excluded(const char *pathname)
{
	int pathlen = strlen(pathname);
	int st;

	for (st = EXC_CMDL; st <= EXC_FILE; st++) {
		switch (excluded_1(pathname, pathlen, &exclude_list[st])) {
		case 0:
			return 0;
		case 1:
			return 1;
		}
	}
	return 0;
}

struct nond_on_fs {
	int len;
	char name[FLEX_ARRAY]; /* more */
};

static struct nond_on_fs **dir;
static int nr_dir;
static int dir_alloc;

static void add_name(const char *pathname, int len)
{
	struct nond_on_fs *ent;

	if (cache_name_pos(pathname, len) >= 0)
		return;

	if (nr_dir == dir_alloc) {
		dir_alloc = alloc_nr(dir_alloc);
		dir = xrealloc(dir, dir_alloc*sizeof(ent));
	}
	ent = xmalloc(sizeof(*ent) + len + 1);
	ent->len = len;
	memcpy(ent->name, pathname, len);
	ent->name[len] = 0;
	dir[nr_dir++] = ent;
}

static int dir_exists(const char *dirname, int len)
{
	int pos = cache_name_pos(dirname, len);
	if (pos >= 0)
		return 1;
	pos = -pos-1;
	if (pos >= active_nr) /* can't */
		return 0;
	return !strncmp(active_cache[pos]->name, dirname, len);
}

/*
 * Read a directory tree. We currently ignore anything but
 * directories, regular files and symlinks. That's because git
 * doesn't handle them at all yet. Maybe that will change some
 * day.
 *
 * Also, we ignore the name ".git" (even if it is not a directory).
 * That likely will not change.
 */
static int read_directory(const char *path, const char *base, int baselen)
{
	DIR *fdir = opendir(path);
	int contents = 0;

	if (fdir) {
		int exclude_stk;
		struct dirent *de;
		char fullname[MAXPATHLEN + 1];
		memcpy(fullname, base, baselen);

		exclude_stk = push_exclude_per_directory(base, baselen);

		while ((de = readdir(fdir)) != NULL) {
			int len;

			if ((de->d_name[0] == '.') &&
			    (de->d_name[1] == 0 ||
			     !strcmp(de->d_name + 1, ".") ||
			     !strcmp(de->d_name + 1, "git")))
				continue;
			len = strlen(de->d_name);
			memcpy(fullname + baselen, de->d_name, len+1);
			if (excluded(fullname) != show_ignored) {
				if (!show_ignored || DTYPE(de) != DT_DIR) {
					continue;
				}
			}

			switch (DTYPE(de)) {
			struct stat st;
			int subdir, rewind_base;
			default:
				continue;
			case DT_UNKNOWN:
				if (lstat(fullname, &st))
					continue;
				if (S_ISREG(st.st_mode) || S_ISLNK(st.st_mode))
					break;
				if (!S_ISDIR(st.st_mode))
					continue;
				/* fallthrough */
			case DT_DIR:
				memcpy(fullname + baselen + len, "/", 2);
				len++;
				rewind_base = nr_dir;
				subdir = read_directory(fullname, fullname,
				                        baselen + len);
				if (show_other_directories &&
				    (subdir || !hide_empty_directories) &&
				    !dir_exists(fullname, baselen + len)) {
					// Rewind the read subdirectory
					while (nr_dir > rewind_base)
						free(dir[--nr_dir]);
					break;
				}
				contents += subdir;
				continue;
			case DT_REG:
			case DT_LNK:
				break;
			}
			add_name(fullname, baselen + len);
			contents++;
		}
		closedir(fdir);

		pop_exclude_per_directory(exclude_stk);
	}

	return contents;
}

static int cmp_name(const void *p1, const void *p2)
{
	const struct nond_on_fs *e1 = *(const struct nond_on_fs **)p1;
	const struct nond_on_fs *e2 = *(const struct nond_on_fs **)p2;

	return cache_name_compare(e1->name, e1->len,
				  e2->name, e2->len);
}

/*
 * Match a pathspec against a filename. The first "len" characters
 * are the common prefix
 */
static int match(const char **spec, char *ps_matched,
		 const char *filename, int len)
{
	const char *m;

	while ((m = *spec++) != NULL) {
		int matchlen = strlen(m + len);

		if (!matchlen)
			goto matched;
		if (!strncmp(m + len, filename + len, matchlen)) {
			if (m[len + matchlen - 1] == '/')
				goto matched;
			switch (filename[len + matchlen]) {
			case '/': case '\0':
				goto matched;
			}
		}
		if (!fnmatch(m + len, filename + len, 0))
			goto matched;
		if (ps_matched)
			ps_matched++;
		continue;
	matched:
		if (ps_matched)
			*ps_matched = 1;
		return 1;
	}
	return 0;
}

static void show_dir_entry(const char *tag, struct nond_on_fs *ent)
{
	int len = prefix_len;
	int offset = prefix_offset;

	if (len >= ent->len)
		die("git-ls-files: internal error - directory entry not superset of prefix");

	if (pathspec && !match(pathspec, ps_matched, ent->name, len))
		return;

	fputs(tag, stdout);
	write_name_quoted("", 0, ent->name + offset, line_terminator, stdout);
	putchar(line_terminator);
}

static void show_other_files(void)
{
	int i;
	for (i = 0; i < nr_dir; i++) {
		/* We should not have a matching entry, but we
		 * may have an unmerged entry for this path.
		 */
		struct nond_on_fs *ent = dir[i];
		int pos = cache_name_pos(ent->name, ent->len);
		struct cache_entry *ce;
		if (0 <= pos)
			die("bug in show-other-files");
		pos = -pos - 1;
		if (pos < active_nr) { 
			ce = active_cache[pos];
			if (ce_namelen(ce) == ent->len &&
			    !memcmp(ce->name, ent->name, ent->len))
				continue; /* Yup, this one exists unmerged */
		}
		show_dir_entry(tag_other, ent);
	}
}

static void show_killed_files(void)
{
	int i;
	for (i = 0; i < nr_dir; i++) {
		struct nond_on_fs *ent = dir[i];
		char *cp, *sp;
		int pos, len, killed = 0;

		for (cp = ent->name; cp - ent->name < ent->len; cp = sp + 1) {
			sp = strchr(cp, '/');
			if (!sp) {
				/* If ent->name is prefix of an entry in the
				 * cache, it will be killed.
				 */
				pos = cache_name_pos(ent->name, ent->len);
				if (0 <= pos)
					die("bug in show-killed-files");
				pos = -pos - 1;
				while (pos < active_nr &&
				       ce_stage(active_cache[pos]))
					pos++; /* skip unmerged */
				if (active_nr <= pos)
					break;
				/* pos points at a name immediately after
				 * ent->name in the cache.  Does it expect
				 * ent->name to be a directory?
				 */
				len = ce_namelen(active_cache[pos]);
				if ((ent->len < len) &&
				    !strncmp(active_cache[pos]->name,
					     ent->name, ent->len) &&
				    active_cache[pos]->name[ent->len] == '/')
					killed = 1;
				break;
			}
			if (0 <= cache_name_pos(ent->name, sp - ent->name)) {
				/* If any of the leading directories in
				 * ent->name is registered in the cache,
				 * ent->name will be killed.
				 */
				killed = 1;
				break;
			}
		}
		if (killed)
			show_dir_entry(tag_killed, dir[i]);
	}
}

static void show_ce_entry(const char *tag, struct cache_entry *ce)
{
	int len = prefix_len;
	int offset = prefix_offset;

	if (len >= ce_namelen(ce))
		die("git-ls-files: internal error - cache entry not superset of prefix");

	if (pathspec && !match(pathspec, ps_matched, ce->name, len))
		return;

	if (tag && *tag && show_valid_bit &&
	    (ce->ce_flags & htons(CE_VALID))) {
		static char alttag[4];
		memcpy(alttag, tag, 3);
		if (isalpha(tag[0]))
			alttag[0] = tolower(tag[0]);
		else if (tag[0] == '?')
			alttag[0] = '!';
		else {
			alttag[0] = 'v';
			alttag[1] = tag[0];
			alttag[2] = ' ';
			alttag[3] = 0;
		}
		tag = alttag;
	}

	if (!show_stage) {
		fputs(tag, stdout);
		write_name_quoted("", 0, ce->name + offset,
				  line_terminator, stdout);
		putchar(line_terminator);
	}
	else {
		printf("%s%06o %s %d\t",
		       tag,
		       ntohl(ce->ce_mode),
		       abbrev ? find_unique_abbrev(ce->sha1,abbrev)
				: sha1_to_hex(ce->sha1),
		       ce_stage(ce));
		write_name_quoted("", 0, ce->name + offset,
				  line_terminator, stdout);
		putchar(line_terminator);
	}
}

static void show_files(void)
{
	int i;

	/* For cached/deleted files we don't need to even do the readdir */
	if (show_others || show_killed) {
		const char *path = ".", *base = "";
		int baselen = prefix_len;

		if (baselen) {
			path = base = prefix;
			if (exclude_per_dir) {
				char *p, *pp = xmalloc(baselen+1);
				memcpy(pp, prefix, baselen+1);
				p = pp;
				while (1) {
					char save = *p;
					*p = 0;
					push_exclude_per_directory(pp, p-pp);
					*p++ = save;
					if (!save)
						break;
					p = strchr(p, '/');
					if (p)
						p++;
					else
						p = pp + baselen;
				}
				free(pp);
			}
		}
		read_directory(path, base, baselen);
		qsort(dir, nr_dir, sizeof(struct nond_on_fs *), cmp_name);
		if (show_others)
			show_other_files();
		if (show_killed)
			show_killed_files();
	}
	if (show_cached | show_stage) {
		for (i = 0; i < active_nr; i++) {
			struct cache_entry *ce = active_cache[i];
			if (excluded(ce->name) != show_ignored)
				continue;
			if (show_unmerged && !ce_stage(ce))
				continue;
			show_ce_entry(ce_stage(ce) ? tag_unmerged : tag_cached, ce);
		}
	}
	if (show_deleted | show_modified) {
		for (i = 0; i < active_nr; i++) {
			struct cache_entry *ce = active_cache[i];
			struct stat st;
			int err;
			if (excluded(ce->name) != show_ignored)
				continue;
			err = lstat(ce->name, &st);
			if (show_deleted && err)
				show_ce_entry(tag_removed, ce);
			if (show_modified && ce_modified(ce, &st, 0))
				show_ce_entry(tag_modified, ce);
		}
	}
}

/*
 * Prune the index to only contain stuff starting with "prefix"
 */
static void prune_cache(void)
{
	int pos = cache_name_pos(prefix, prefix_len);
	unsigned int first, last;

	if (pos < 0)
		pos = -pos-1;
	active_cache += pos;
	active_nr -= pos;
	first = 0;
	last = active_nr;
	while (last > first) {
		int next = (last + first) >> 1;
		struct cache_entry *ce = active_cache[next];
		if (!strncmp(ce->name, prefix, prefix_len)) {
			first = next+1;
			continue;
		}
		last = next;
	}
	active_nr = last;
}

static void verify_pathspec(void)
{
	const char **p, *n, *prev;
	char *real_prefix;
	unsigned long max;

	prev = NULL;
	max = PATH_MAX;
	for (p = pathspec; (n = *p) != NULL; p++) {
		int i, len = 0;
		for (i = 0; i < max; i++) {
			char c = n[i];
			if (prev && prev[i] != c)
				break;
			if (!c || c == '*' || c == '?')
				break;
			if (c == '/')
				len = i+1;
		}
		prev = n;
		if (len < max) {
			max = len;
			if (!max)
				break;
		}
	}

	if (prefix_offset > max || memcmp(prev, prefix, prefix_offset))
		die("git-ls-files: cannot generate relative filenames containing '..'");

	real_prefix = NULL;
	prefix_len = max;
	if (max) {
		real_prefix = xmalloc(max + 1);
		memcpy(real_prefix, prev, max);
		real_prefix[max] = 0;
	}
	prefix = real_prefix;
}

static const char ls_files_usage[] =
	"git-ls-files [-z] [-t] [-v] (--[cached|deleted|others|stage|unmerged|killed|modified])* "
	"[ --ignored ] [--exclude=<pattern>] [--exclude-from=<file>] "
	"[ --exclude-per-directory=<filename> ] [--full-name] [--abbrev] "
	"[--] [<file>]*";

int cmd_ls_files(int argc, const char **argv, char** envp)
{
	int i;
	int exc_given = 0;

	prefix = setup_git_directory();
	if (prefix)
		prefix_offset = strlen(prefix);
	git_config(git_default_config);

	for (i = 1; i < argc; i++) {
		const char *arg = argv[i];

		if (!strcmp(arg, "--")) {
			i++;
			break;
		}
		if (!strcmp(arg, "-z")) {
			line_terminator = 0;
			continue;
		}
		if (!strcmp(arg, "-t") || !strcmp(arg, "-v")) {
			tag_cached = "H ";
			tag_unmerged = "M ";
			tag_removed = "R ";
			tag_modified = "C ";
			tag_other = "? ";
			tag_killed = "K ";
			if (arg[1] == 'v')
				show_valid_bit = 1;
			continue;
		}
		if (!strcmp(arg, "-c") || !strcmp(arg, "--cached")) {
			show_cached = 1;
			continue;
		}
		if (!strcmp(arg, "-d") || !strcmp(arg, "--deleted")) {
			show_deleted = 1;
			continue;
		}
		if (!strcmp(arg, "-m") || !strcmp(arg, "--modified")) {
			show_modified = 1;
			continue;
		}
		if (!strcmp(arg, "-o") || !strcmp(arg, "--others")) {
			show_others = 1;
			continue;
		}
		if (!strcmp(arg, "-i") || !strcmp(arg, "--ignored")) {
			show_ignored = 1;
			continue;
		}
		if (!strcmp(arg, "-s") || !strcmp(arg, "--stage")) {
			show_stage = 1;
			continue;
		}
		if (!strcmp(arg, "-k") || !strcmp(arg, "--killed")) {
			show_killed = 1;
			continue;
		}
		if (!strcmp(arg, "--directory")) {
			show_other_directories = 1;
			continue;
		}
		if (!strcmp(arg, "--no-empty-directory")) {
			hide_empty_directories = 1;
			continue;
		}
		if (!strcmp(arg, "-u") || !strcmp(arg, "--unmerged")) {
			/* There's no point in showing unmerged unless
			 * you also show the stage information.
			 */
			show_stage = 1;
			show_unmerged = 1;
			continue;
		}
		if (!strcmp(arg, "-x") && i+1 < argc) {
			exc_given = 1;
			add_exclude(argv[++i], "", 0, &exclude_list[EXC_CMDL]);
			continue;
		}
		if (!strncmp(arg, "--exclude=", 10)) {
			exc_given = 1;
			add_exclude(arg+10, "", 0, &exclude_list[EXC_CMDL]);
			continue;
		}
		if (!strcmp(arg, "-X") && i+1 < argc) {
			exc_given = 1;
			add_excludes_from_file(argv[++i]);
			continue;
		}
		if (!strncmp(arg, "--exclude-from=", 15)) {
			exc_given = 1;
			add_excludes_from_file(arg+15);
			continue;
		}
		if (!strncmp(arg, "--exclude-per-directory=", 24)) {
			exc_given = 1;
			exclude_per_dir = arg + 24;
			continue;
		}
		if (!strcmp(arg, "--full-name")) {
			prefix_offset = 0;
			continue;
		}
		if (!strcmp(arg, "--error-unmatch")) {
			error_unmatch = 1;
			continue;
		}
		if (!strncmp(arg, "--abbrev=", 9)) {
			abbrev = strtoul(arg+9, NULL, 10);
			if (abbrev && abbrev < MINIMUM_ABBREV)
				abbrev = MINIMUM_ABBREV;
			else if (abbrev > 40)
				abbrev = 40;
			continue;
		}
		if (!strcmp(arg, "--abbrev")) {
			abbrev = DEFAULT_ABBREV;
			continue;
		}
		if (*arg == '-')
			usage(ls_files_usage);
		break;
	}

	pathspec = get_pathspec(prefix, argv + i);

	/* Verify that the pathspec matches the prefix */
	if (pathspec)
		verify_pathspec();

	/* Treat unmatching pathspec elements as errors */
	if (pathspec && error_unmatch) {
		int num;
		for (num = 0; pathspec[num]; num++)
			;
		ps_matched = xcalloc(1, num);
	}

	if (show_ignored && !exc_given) {
		fprintf(stderr, "%s: --ignored needs some exclude pattern\n",
			argv[0]);
		exit(1);
	}

	/* With no flags, we default to showing the cached files */
	if (!(show_stage | show_deleted | show_others | show_unmerged |
	      show_killed | show_modified))
		show_cached = 1;

	read_cache();
	if (prefix)
		prune_cache();
	show_files();

	if (ps_matched) {
		/* We need to make sure all pathspec matched otherwise
		 * it is an error.
		 */
		int num, errors = 0;
		for (num = 0; pathspec[num]; num++) {
			if (ps_matched[num])
				continue;
			error("pathspec '%s' did not match any.",
			      pathspec[num] + prefix_offset);
			errors++;
		}
		return errors ? 1 : 0;
	}

	return 0;
}
