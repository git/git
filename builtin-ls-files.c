/*
 * This merges the file listing in the directory cache index
 * with the actual working directory list, and shows different
 * combinations of the two.
 *
 * Copyright (C) Linus Torvalds, 2005
 */
#include "cache.h"
#include "quote.h"
#include "dir.h"
#include "builtin.h"

static int abbrev;
static int show_deleted;
static int show_cached;
static int show_others;
static int show_stage;
static int show_unmerged;
static int show_modified;
static int show_killed;
static int show_valid_bit;
static int line_terminator = '\n';

static int prefix_len;
static int prefix_offset;
static const char **pathspec;
static int error_unmatch;
static char *ps_matched;

static const char *tag_cached = "";
static const char *tag_unmerged = "";
static const char *tag_removed = "";
static const char *tag_other = "";
static const char *tag_killed = "";
static const char *tag_modified = "";


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

static void show_dir_entry(const char *tag, struct dir_entry *ent)
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

static void show_other_files(struct dir_struct *dir)
{
	int i;
	for (i = 0; i < dir->nr; i++) {
		/* We should not have a matching entry, but we
		 * may have an unmerged entry for this path.
		 */
		struct dir_entry *ent = dir->entries[i];
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

static void show_killed_files(struct dir_struct *dir)
{
	int i;
	for (i = 0; i < dir->nr; i++) {
		struct dir_entry *ent = dir->entries[i];
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
			show_dir_entry(tag_killed, dir->entries[i]);
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

static void show_files(struct dir_struct *dir, const char *prefix)
{
	int i;

	/* For cached/deleted files we don't need to even do the readdir */
	if (show_others || show_killed) {
		const char *path = ".", *base = "";
		int baselen = prefix_len;

		if (baselen)
			path = base = prefix;
		read_directory(dir, path, base, baselen);
		if (show_others)
			show_other_files(dir);
		if (show_killed)
			show_killed_files(dir);
	}
	if (show_cached | show_stage) {
		for (i = 0; i < active_nr; i++) {
			struct cache_entry *ce = active_cache[i];
			if (excluded(dir, ce->name) != dir->show_ignored)
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
			if (excluded(dir, ce->name) != dir->show_ignored)
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
static void prune_cache(const char *prefix)
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

static const char *verify_pathspec(const char *prefix)
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
	return real_prefix;
}

static const char ls_files_usage[] =
	"git-ls-files [-z] [-t] [-v] (--[cached|deleted|others|stage|unmerged|killed|modified])* "
	"[ --ignored ] [--exclude=<pattern>] [--exclude-from=<file>] "
	"[ --exclude-per-directory=<filename> ] [--full-name] [--abbrev] "
	"[--] [<file>]*";

int cmd_ls_files(int argc, const char **argv, const char *prefix)
{
	int i;
	int exc_given = 0, require_work_tree = 0;
	struct dir_struct dir;

	memset(&dir, 0, sizeof(dir));
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
			require_work_tree = 1;
			continue;
		}
		if (!strcmp(arg, "-o") || !strcmp(arg, "--others")) {
			show_others = 1;
			require_work_tree = 1;
			continue;
		}
		if (!strcmp(arg, "-i") || !strcmp(arg, "--ignored")) {
			dir.show_ignored = 1;
			require_work_tree = 1;
			continue;
		}
		if (!strcmp(arg, "-s") || !strcmp(arg, "--stage")) {
			show_stage = 1;
			continue;
		}
		if (!strcmp(arg, "-k") || !strcmp(arg, "--killed")) {
			show_killed = 1;
			require_work_tree = 1;
			continue;
		}
		if (!strcmp(arg, "--directory")) {
			dir.show_other_directories = 1;
			continue;
		}
		if (!strcmp(arg, "--no-empty-directory")) {
			dir.hide_empty_directories = 1;
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
			add_exclude(argv[++i], "", 0, &dir.exclude_list[EXC_CMDL]);
			continue;
		}
		if (!strncmp(arg, "--exclude=", 10)) {
			exc_given = 1;
			add_exclude(arg+10, "", 0, &dir.exclude_list[EXC_CMDL]);
			continue;
		}
		if (!strcmp(arg, "-X") && i+1 < argc) {
			exc_given = 1;
			add_excludes_from_file(&dir, argv[++i]);
			continue;
		}
		if (!strncmp(arg, "--exclude-from=", 15)) {
			exc_given = 1;
			add_excludes_from_file(&dir, arg+15);
			continue;
		}
		if (!strncmp(arg, "--exclude-per-directory=", 24)) {
			exc_given = 1;
			dir.exclude_per_dir = arg + 24;
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

	if (require_work_tree &&
			(is_bare_repository() || is_inside_git_dir()))
		die("This operation must be run in a work tree");

	pathspec = get_pathspec(prefix, argv + i);

	/* Verify that the pathspec matches the prefix */
	if (pathspec)
		prefix = verify_pathspec(prefix);

	/* Treat unmatching pathspec elements as errors */
	if (pathspec && error_unmatch) {
		int num;
		for (num = 0; pathspec[num]; num++)
			;
		ps_matched = xcalloc(1, num);
	}

	if (dir.show_ignored && !exc_given) {
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
		prune_cache(prefix);
	show_files(&dir, prefix);

	if (ps_matched) {
		/* We need to make sure all pathspec matched otherwise
		 * it is an error.
		 */
		int num, errors = 0;
		for (num = 0; pathspec[num]; num++) {
			if (ps_matched[num])
				continue;
			error("pathspec '%s' did not match any file(s) known to git.",
			      pathspec[num] + prefix_offset);
			errors++;
		}

		if (errors)
			fprintf(stderr, "Did you forget to 'git add'?\n");

		return errors ? 1 : 0;
	}

	return 0;
}
