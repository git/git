#include "cache.h"
#include "dir.h"

static int compare_untracked(const void *a_, const void *b_)
{
	const char *const *a = a_;
	const char *const *b = b_;
	return strcmp(*a, *b);
}

static int compare_dir(const void *a_, const void *b_)
{
	const struct untracked_cache_dir *const *a = a_;
	const struct untracked_cache_dir *const *b = b_;
	return strcmp((*a)->name, (*b)->name);
}

static void dump(struct untracked_cache_dir *ucd, struct strbuf *base)
{
	int i, len;
	QSORT(ucd->untracked, ucd->untracked_nr, compare_untracked);
	QSORT(ucd->dirs, ucd->dirs_nr, compare_dir);
	len = base->len;
	strbuf_addf(base, "%s/", ucd->name);
	printf("%s %s", base->buf,
	       oid_to_hex(&ucd->exclude_oid));
	if (ucd->recurse)
		fputs(" recurse", stdout);
	if (ucd->check_only)
		fputs(" check_only", stdout);
	if (ucd->valid)
		fputs(" valid", stdout);
	printf("\n");
	for (i = 0; i < ucd->untracked_nr; i++)
		printf("%s\n", ucd->untracked[i]);
	for (i = 0; i < ucd->dirs_nr; i++)
		dump(ucd->dirs[i], base);
	strbuf_setlen(base, len);
}

int cmd_main(int ac, const char **av)
{
	struct untracked_cache *uc;
	struct strbuf base = STRBUF_INIT;

	/* Hack to avoid modifying the untracked cache when we read it */
	ignore_untracked_cache_config = 1;

	setup_git_directory();
	if (read_cache() < 0)
		die("unable to read index file");
	uc = the_index.untracked;
	if (!uc) {
		printf("no untracked cache\n");
		return 0;
	}
	printf("info/exclude %s\n", oid_to_hex(&uc->ss_info_exclude.oid));
	printf("core.excludesfile %s\n", oid_to_hex(&uc->ss_excludes_file.oid));
	printf("exclude_per_dir %s\n", uc->exclude_per_dir);
	printf("flags %08x\n", uc->dir_flags);
	if (uc->root)
		dump(uc->root, &base);
	return 0;
}
