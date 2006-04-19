#include "cache.h"
#include "tree-walk.h"

static const char unresolve_usage[] =
"git-unresolve <paths>...";

static struct cache_file cache_file;
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
		error("%s: not in %s branch.", path, which);
		return NULL;
	}
	if (mode == S_IFDIR) {
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
		die("Cannot read HEAD -- no initial commit yet?");
	if (read_ref(git_path("MERGE_HEAD"), merge_head_sha1)) {
		fprintf(stderr, "Not in the middle of a merge.\n");
		exit(0);
	}
}

int main(int ac, char **av)
{
	int i;
	int err = 0;
	int newfd;

	if (ac < 2)
		usage(unresolve_usage);

	git_config(git_default_config);

	/* Read HEAD and MERGE_HEAD; if MERGE_HEAD does not exist, we
	 * are not doing a merge, so exit with success status.
	 */
	read_head_pointers();

	/* Otherwise we would need to update the cache. */
	newfd= hold_index_file_for_update(&cache_file, get_index_file());
	if (newfd < 0)
		die("unable to create new cachefile");

	if (read_cache() < 0)
		die("cache corrupted");

	for (i = 1; i < ac; i++) {
		char *arg = av[i];
		err |= unresolve_one(arg);
	}
	if (err)
		die("Error encountered; index not updated.");

	if (active_cache_changed) {
		if (write_cache(newfd, active_cache, active_nr) ||
		    commit_index_file(&cache_file))
			die("Unable to write new cachefile");
	}
	return 0;
}
