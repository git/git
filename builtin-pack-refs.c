#include "cache.h"
#include "refs.h"

static const char *result_path, *lock_path;
static const char builtin_pack_refs_usage[] =
"git-pack-refs [--prune]";

struct ref_to_prune {
	struct ref_to_prune *next;
	unsigned char sha1[20];
	char name[FLEX_ARRAY];
};

struct pack_refs_cb_data {
	int prune;
	struct ref_to_prune *ref_to_prune;
	FILE *refs_file;
};

static void remove_lock_file(void)
{
	if (lock_path)
		unlink(lock_path);
}

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
	struct ref_lock *lock = lock_ref_sha1(r->name + 5, r->sha1, 1);

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
		/* perhaps other parameters later... */
		break;
	}
	if (i != argc)
		usage(builtin_pack_refs_usage);

	result_path = xstrdup(git_path("packed-refs"));
	lock_path = xstrdup(mkpath("%s.lock", result_path));

	fd = open(lock_path, O_CREAT | O_EXCL | O_WRONLY, 0666);
	if (fd < 0)
		die("unable to create new ref-pack file (%s)", strerror(errno));
	atexit(remove_lock_file);

	cbdata.refs_file = fdopen(fd, "w");
	if (!cbdata.refs_file)
		die("unable to create ref-pack file structure (%s)",
		    strerror(errno));
	for_each_ref(handle_one_ref, &cbdata);
	fsync(fd);
	fclose(cbdata.refs_file);
	if (rename(lock_path, result_path) < 0)
		die("unable to overwrite old ref-pack file (%s)", strerror(errno));
	lock_path = NULL;
	if (cbdata.prune)
		prune_refs(cbdata.ref_to_prune);
	return 0;
}
