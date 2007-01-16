#include "cache.h"
#include "refs.h"
#include "commit.h"

static void show_new(char *type, unsigned char *sha1_new)
{
	fprintf(stderr, "  %s: %s\n", type,
		find_unique_abbrev(sha1_new, DEFAULT_ABBREV));
}

static int update_ref(const char *action,
		      const char *refname,
		      unsigned char *sha1,
		      unsigned char *oldval)
{
	int len;
	char msg[1024];
	char *rla = getenv("GIT_REFLOG_ACTION");
	static struct ref_lock *lock;

	if (!rla)
		rla = "(reflog update)";
	len = snprintf(msg, sizeof(msg), "%s: %s", rla, action);
	if (sizeof(msg) <= len)
		die("insanely long action");
	lock = lock_any_ref_for_update(refname, oldval);
	if (!lock)
		return 1;
	if (write_ref_sha1(lock, sha1, msg) < 0)
		return 1;
	return 0;
}

static int update_local_ref(const char *name,
			    const char *new_head,
			    const char *note,
			    int verbose, int force)
{
	char type[20];
	unsigned char sha1_old[20], sha1_new[20];
	char oldh[41], newh[41];
	struct commit *current, *updated;

	if (get_sha1_hex(new_head, sha1_new))
		die("malformed object name %s", new_head);
	if (sha1_object_info(sha1_new, type, NULL))
		die("object %s not found", new_head);

	if (!*name) {
		/* Not storing */
		if (verbose) {
			fprintf(stderr, "* fetched %s\n", note);
			show_new(type, sha1_new);
		}
		return 0;
	}

	if (get_sha1(name, sha1_old)) {
		char *msg;
	just_store:
		/* new ref */
		if (!strncmp(name, "refs/tags/", 10))
			msg = "storing tag";
		else
			msg = "storing head";
		fprintf(stderr, "* %s: storing %s\n",
			name, note);
		show_new(type, sha1_new);
		return update_ref(msg, name, sha1_new, NULL);
	}

	if (!hashcmp(sha1_old, sha1_new)) {
		if (verbose) {
			fprintf(stderr, "* %s: same as %s\n", name, note);
			show_new(type, sha1_new);
		}
		return 0;
	}

	if (!strncmp(name, "refs/tags/", 10)) {
		fprintf(stderr, "* %s: updating with %s\n", name, note);
		show_new(type, sha1_new);
		return update_ref("updating tag", name, sha1_new, NULL);
	}

	current = lookup_commit_reference(sha1_old);
	updated = lookup_commit_reference(sha1_new);
	if (!current || !updated)
		goto just_store;

	strcpy(oldh, find_unique_abbrev(current->object.sha1, DEFAULT_ABBREV));
	strcpy(newh, find_unique_abbrev(sha1_new, DEFAULT_ABBREV));

	if (in_merge_bases(current, &updated, 1)) {
		fprintf(stderr, "* %s: fast forward to %s\n",
			name, note);
		fprintf(stderr, "  old..new: %s..%s\n", oldh, newh);
		return update_ref("fast forward", name, sha1_new, sha1_old);
	}
	if (!force) {
		fprintf(stderr,
			"* %s: not updating to non-fast forward %s\n",
			name, note);
		fprintf(stderr,
			"  old...new: %s...%s\n", oldh, newh);
		return 1;
	}
	fprintf(stderr,
		"* %s: forcing update to non-fast forward %s\n",
		name, note);
	fprintf(stderr, "  old...new: %s...%s\n", oldh, newh);
	return update_ref("forced-update", name, sha1_new, sha1_old);
}

static int append_fetch_head(FILE *fp,
			     const char *head, const char *remote,
			     const char *remote_name, const char *remote_nick,
			     const char *local_name, int not_for_merge,
			     int verbose, int force)
{
	struct commit *commit;
	int remote_len, i, note_len;
	unsigned char sha1[20];
	char note[1024];
	const char *what, *kind;

	if (get_sha1(head, sha1))
		return error("Not a valid object name: %s", head);
	commit = lookup_commit_reference(sha1);
	if (!commit)
		not_for_merge = 1;

	if (!strcmp(remote_name, "HEAD")) {
		kind = "";
		what = "";
	}
	else if (!strncmp(remote_name, "refs/heads/", 11)) {
		kind = "branch";
		what = remote_name + 11;
	}
	else if (!strncmp(remote_name, "refs/tags/", 10)) {
		kind = "tag";
		what = remote_name + 10;
	}
	else if (!strncmp(remote_name, "refs/remotes/", 13)) {
		kind = "remote branch";
		what = remote_name + 13;
	}
	else {
		kind = "";
		what = remote_name;
	}

	remote_len = strlen(remote);
	for (i = remote_len - 1; remote[i] == '/' && 0 <= i; i--)
		;
	remote_len = i + 1;
	if (4 < i && !strncmp(".git", remote + i - 3, 4))
		remote_len = i - 3;
	note_len = sprintf(note, "%s\t%s\t",
			   sha1_to_hex(commit ? commit->object.sha1 : sha1),
			   not_for_merge ? "not-for-merge" : "");
	if (*what) {
		if (*kind)
			note_len += sprintf(note + note_len, "%s ", kind);
		note_len += sprintf(note + note_len, "'%s' of ", what);
	}
	note_len += sprintf(note + note_len, "%.*s", remote_len, remote);
	fprintf(fp, "%s\n", note);
	return update_local_ref(local_name, head, note, verbose, force);
}

int cmd_fetch__tool(int argc, const char **argv, const char *prefix)
{
	int verbose = 0;
	int force = 0;

	while (1 < argc) {
		const char *arg = argv[1];
		if (!strcmp("-v", arg))
			verbose = 1;
		else if (!strcmp("-f", arg))
			force = 1;
		else
			break;
		argc--;
		argv++;
	}

	if (argc <= 1)
		return error("Missing subcommand");

	if (!strcmp("append-fetch-head", argv[1])) {
		int result;
		FILE *fp;

		if (argc != 8)
			return error("append-fetch-head takes 6 args");
		fp = fopen(git_path("FETCH_HEAD"), "a");
		result = append_fetch_head(fp, argv[2], argv[3],
					   argv[4], argv[5],
					   argv[6], !!argv[7][0],
					   verbose, force);
		fclose(fp);
		return result;
	}
	if (!strcmp("update-local-ref", argv[1])) {
		if (argc != 5)
			return error("update-local-ref takes 3 args");
		return update_local_ref(argv[2], argv[3], argv[4],
					verbose, force);
	}
	return error("Unknown subcommand: %s", argv[1]);
}
