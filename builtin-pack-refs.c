#include "cache.h"
#include "refs.h"

static const char builtin_pack_refs_usage[] =
"git-pack-refs [--all] [--prune]";

struct ref_to_prune {
	struct ref_to_prune *next;
	unsigned char sha1[20];
	char name[FLEX_ARRAY];
};

struct pack_refs_cb_data {
	int prune;
	int all;
	struct ref_to_prune *ref_to_prune;
	FILE *refs_file;
};

static int do_not_prune(int flags)
{
	/* If it is already packed or if it is a symref,
	 * do not prune it.
	 */
	return (flags & (REF_ISSYMREF|REF_ISPACKED));
}

static int handle_one_ref(const char *path, const unsigned char *sha1,
			  int flags, void *cb_data)
{
	struct pack_refs_cb_data *cb = cb_data;

	if (!cb->all && strncmp(path, "refs/tags/", 10))
		return 0;
	/* Do not pack the symbolic refs */
	if (!(flags & REF_ISSYMREF))
		fprintf(cb->refs_file, "%s %s\n", sha1_to_hex(sha1), path);
	if (cb->prune && !do_not_prune(flags)) {
		int namelen = strlen(path) + 1;
		struct ref_to_prune *n = xcalloc(1, sizeof(*n) + namelen);
		hashcpy(n->sha1, sha1);
		strcpy(n->name, path);
		n->next = cb->ref_to_prune;
		cb->ref_to_prune = n;
	}
	return 0;
}

/* make sure nobody touched the ref, and unlink */
static void prune_ref(struct ref_to_prune *r)
{
	struct ref_lock *lock = lock_ref_sha1(r->name + 5, r->sha1);

	if (lock) {
		unlink(git_path("%s", r->name));
		unlock_ref(lock);
	}
}

static void prune_refs(struct ref_to_prune *r)
{
	while (r) {
		prune_ref(r);
		r = r->next;
	}
}

static struct lock_file packed;

int cmd_pack_refs(int argc, const char **argv, const char *prefix)
{
	int fd, i;
	struct pack_refs_cb_data cbdata;

	memset(&cbdata, 0, sizeof(cbdata));

	for (i = 1; i < argc; i++) {
		const char *arg = argv[i];
		if (!strcmp(arg, "--prune")) {
			cbdata.prune = 1;
			continue;
		}
		if (!strcmp(arg, "--all")) {
			cbdata.all = 1;
			continue;
		}
		/* perhaps other parameters later... */
		break;
	}
	if (i != argc)
		usage(builtin_pack_refs_usage);

	fd = hold_lock_file_for_update(&packed, git_path("packed-refs"), 1);
	cbdata.refs_file = fdopen(fd, "w");
	if (!cbdata.refs_file)
		die("unable to create ref-pack file structure (%s)",
		    strerror(errno));
	for_each_ref(handle_one_ref, &cbdata);
	fflush(cbdata.refs_file);
	fsync(fd);
	fclose(cbdata.refs_file);
	if (commit_lock_file(&packed) < 0)
		die("unable to overwrite old ref-pack file (%s)", strerror(errno));
	if (cbdata.prune)
		prune_refs(cbdata.ref_to_prune);
	return 0;
}
