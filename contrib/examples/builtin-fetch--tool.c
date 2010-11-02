#include "builtin.h"
#include "cache.h"
#include "refs.h"
#include "commit.h"
#include "sigchain.h"

static char *get_stdin(void)
{
	struct strbuf buf = STRBUF_INIT;
	if (strbuf_read(&buf, 0, 1024) < 0) {
		die_errno("error reading standard input");
	}
	return strbuf_detach(&buf, NULL);
}

static void show_new(enum object_type type, unsigned char *sha1_new)
{
	fprintf(stderr, "  %s: %s\n", typename(type),
		find_unique_abbrev(sha1_new, DEFAULT_ABBREV));
}

static int update_ref_env(const char *action,
		      const char *refname,
		      unsigned char *sha1,
		      unsigned char *oldval)
{
	char msg[1024];
	const char *rla = getenv("GIT_REFLOG_ACTION");

	if (!rla)
		rla = "(reflog update)";
	if (snprintf(msg, sizeof(msg), "%s: %s", rla, action) >= sizeof(msg))
		warning("reflog message too long: %.*s...", 50, msg);
	return update_ref(msg, refname, sha1, oldval, 0, QUIET_ON_ERR);
}

static int update_local_ref(const char *name,
			    const char *new_head,
			    const char *note,
			    int verbose, int force)
{
	unsigned char sha1_old[20], sha1_new[20];
	char oldh[41], newh[41];
	struct commit *current, *updated;
	enum object_type type;

	if (get_sha1_hex(new_head, sha1_new))
		die("malformed object name %s", new_head);

	type = sha1_object_info(sha1_new, NULL);
	if (type < 0)
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
		const char *msg;
	just_store:
		/* new ref */
		if (!strncmp(name, "refs/tags/", 10))
			msg = "storing tag";
		else
			msg = "storing head";
		fprintf(stderr, "* %s: storing %s\n",
			name, note);
		show_new(type, sha1_new);
		return update_ref_env(msg, name, sha1_new, NULL);
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
		return update_ref_env("updating tag", name, sha1_new, NULL);
	}

	current = lookup_commit_reference(sha1_old);
	updated = lookup_commit_reference(sha1_new);
	if (!current || !updated)
		goto just_store;

	strcpy(oldh, find_unique_abbrev(current->object.sha1, DEFAULT_ABBREV));
	strcpy(newh, find_unique_abbrev(sha1_new, DEFAULT_ABBREV));

	if (in_merge_bases(current, &updated, 1)) {
		fprintf(stderr, "* %s: fast-forward to %s\n",
			name, note);
		fprintf(stderr, "  old..new: %s..%s\n", oldh, newh);
		return update_ref_env("fast-forward", name, sha1_new, sha1_old);
	}
	if (!force) {
		fprintf(stderr,
			"* %s: not updating to non-fast-forward %s\n",
			name, note);
		fprintf(stderr,
			"  old...new: %s...%s\n", oldh, newh);
		return 1;
	}
	fprintf(stderr,
		"* %s: forcing update to non-fast-forward %s\n",
		name, note);
	fprintf(stderr, "  old...new: %s...%s\n", oldh, newh);
	return update_ref_env("forced-update", name, sha1_new, sha1_old);
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
	commit = lookup_commit_reference_gently(sha1, 1);
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
		kind = "remote-tracking branch";
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

	note_len = 0;
	if (*what) {
		if (*kind)
			note_len += sprintf(note + note_len, "%s ", kind);
		note_len += sprintf(note + note_len, "'%s' of ", what);
	}
	note_len += sprintf(note + note_len, "%.*s", remote_len, remote);
	fprintf(fp, "%s\t%s\t%s\n",
		sha1_to_hex(commit ? commit->object.sha1 : sha1),
		not_for_merge ? "not-for-merge" : "",
		note);
	return update_local_ref(local_name, head, note, verbose, force);
}

static char *keep;
static void remove_keep(void)
{
	if (keep && *keep)
		unlink(keep);
}

static void remove_keep_on_signal(int signo)
{
	remove_keep();
	sigchain_pop(signo);
	raise(signo);
}

static char *find_local_name(const char *remote_name, const char *refs,
			     int *force_p, int *not_for_merge_p)
{
	const char *ref = refs;
	int len = strlen(remote_name);

	while (ref) {
		const char *next;
		int single_force, not_for_merge;

		while (*ref == '\n')
			ref++;
		if (!*ref)
			break;
		next = strchr(ref, '\n');

		single_force = not_for_merge = 0;
		if (*ref == '+') {
			single_force = 1;
			ref++;
		}
		if (*ref == '.') {
			not_for_merge = 1;
			ref++;
			if (*ref == '+') {
				single_force = 1;
				ref++;
			}
		}
		if (!strncmp(remote_name, ref, len) && ref[len] == ':') {
			const char *local_part = ref + len + 1;
			int retlen;

			if (!next)
				retlen = strlen(local_part);
			else
				retlen = next - local_part;
			*force_p = single_force;
			*not_for_merge_p = not_for_merge;
			return xmemdupz(local_part, retlen);
		}
		ref = next;
	}
	return NULL;
}

static int fetch_native_store(FILE *fp,
			      const char *remote,
			      const char *remote_nick,
			      const char *refs,
			      int verbose, int force)
{
	char buffer[1024];
	int err = 0;

	sigchain_push_common(remove_keep_on_signal);
	atexit(remove_keep);

	while (fgets(buffer, sizeof(buffer), stdin)) {
		int len;
		char *cp;
		char *local_name;
		int single_force, not_for_merge;

		for (cp = buffer; *cp && !isspace(*cp); cp++)
			;
		if (*cp)
			*cp++ = 0;
		len = strlen(cp);
		if (len && cp[len-1] == '\n')
			cp[--len] = 0;
		if (!strcmp(buffer, "failed"))
			die("Fetch failure: %s", remote);
		if (!strcmp(buffer, "pack"))
			continue;
		if (!strcmp(buffer, "keep")) {
			char *od = get_object_directory();
			int len = strlen(od) + strlen(cp) + 50;
			keep = xmalloc(len);
			sprintf(keep, "%s/pack/pack-%s.keep", od, cp);
			continue;
		}

		local_name = find_local_name(cp, refs,
					     &single_force, &not_for_merge);
		if (!local_name)
			continue;
		err |= append_fetch_head(fp,
					 buffer, remote, cp, remote_nick,
					 local_name, not_for_merge,
					 verbose, force || single_force);
	}
	return err;
}

static int parse_reflist(const char *reflist)
{
	const char *ref;

	printf("refs='");
	for (ref = reflist; ref; ) {
		const char *next;
		while (*ref && isspace(*ref))
			ref++;
		if (!*ref)
			break;
		for (next = ref; *next && !isspace(*next); next++)
			;
		printf("\n%.*s", (int)(next - ref), ref);
		ref = next;
	}
	printf("'\n");

	printf("rref='");
	for (ref = reflist; ref; ) {
		const char *next, *colon;
		while (*ref && isspace(*ref))
			ref++;
		if (!*ref)
			break;
		for (next = ref; *next && !isspace(*next); next++)
			;
		if (*ref == '.')
			ref++;
		if (*ref == '+')
			ref++;
		colon = strchr(ref, ':');
		putchar('\n');
		printf("%.*s", (int)((colon ? colon : next) - ref), ref);
		ref = next;
	}
	printf("'\n");
	return 0;
}

static int expand_refs_wildcard(const char *ls_remote_result, int numrefs,
				const char **refs)
{
	int i, matchlen, replacelen;
	int found_one = 0;
	const char *remote = *refs++;
	numrefs--;

	if (numrefs == 0) {
		fprintf(stderr, "Nothing specified for fetching with remote.%s.fetch\n",
			remote);
		printf("empty\n");
	}

	for (i = 0; i < numrefs; i++) {
		const char *ref = refs[i];
		const char *lref = ref;
		const char *colon;
		const char *tail;
		const char *ls;
		const char *next;

		if (*lref == '+')
			lref++;
		colon = strchr(lref, ':');
		tail = lref + strlen(lref);
		if (!(colon &&
		      2 < colon - lref &&
		      colon[-1] == '*' &&
		      colon[-2] == '/' &&
		      2 < tail - (colon + 1) &&
		      tail[-1] == '*' &&
		      tail[-2] == '/')) {
			/* not a glob */
			if (!found_one++)
				printf("explicit\n");
			printf("%s\n", ref);
			continue;
		}

		/* glob */
		if (!found_one++)
			printf("glob\n");

		/* lref to colon-2 is remote hierarchy name;
		 * colon+1 to tail-2 is local.
		 */
		matchlen = (colon-1) - lref;
		replacelen = (tail-1) - (colon+1);
		for (ls = ls_remote_result; ls; ls = next) {
			const char *eol;
			unsigned char sha1[20];
			int namelen;

			while (*ls && isspace(*ls))
				ls++;
			next = strchr(ls, '\n');
			eol = !next ? (ls + strlen(ls)) : next;
			if (!memcmp("^{}", eol-3, 3))
				continue;
			if (eol - ls < 40)
				continue;
			if (get_sha1_hex(ls, sha1))
				continue;
			ls += 40;
			while (ls < eol && isspace(*ls))
				ls++;
			/* ls to next (or eol) is the name.
			 * is it identical to lref to colon-2?
			 */
			if ((eol - ls) <= matchlen ||
			    strncmp(ls, lref, matchlen))
				continue;

			/* Yes, it is a match */
			namelen = eol - ls;
			if (lref != ref)
				putchar('+');
			printf("%.*s:%.*s%.*s\n",
			       namelen, ls,
			       replacelen, colon + 1,
			       namelen - matchlen, ls + matchlen);
		}
	}
	return 0;
}

static int pick_rref(int sha1_only, const char *rref, const char *ls_remote_result)
{
	int err = 0;
	int lrr_count = lrr_count, i, pass;
	const char *cp;
	struct lrr {
		const char *line;
		const char *name;
		int namelen;
		int shown;
	} *lrr_list = lrr_list;

	for (pass = 0; pass < 2; pass++) {
		/* pass 0 counts and allocates, pass 1 fills... */
		cp = ls_remote_result;
		i = 0;
		while (1) {
			const char *np;
			while (*cp && isspace(*cp))
				cp++;
			if (!*cp)
				break;
			np = strchrnul(cp, '\n');
			if (pass) {
				lrr_list[i].line = cp;
				lrr_list[i].name = cp + 41;
				lrr_list[i].namelen = np - (cp + 41);
			}
			i++;
			cp = np;
		}
		if (!pass) {
			lrr_count = i;
			lrr_list = xcalloc(lrr_count, sizeof(*lrr_list));
		}
	}

	while (1) {
		const char *next;
		int rreflen;
		int i;

		while (*rref && isspace(*rref))
			rref++;
		if (!*rref)
			break;
		next = strchrnul(rref, '\n');
		rreflen = next - rref;

		for (i = 0; i < lrr_count; i++) {
			struct lrr *lrr = &(lrr_list[i]);

			if (rreflen == lrr->namelen &&
			    !memcmp(lrr->name, rref, rreflen)) {
				if (!lrr->shown)
					printf("%.*s\n",
					       sha1_only ? 40 : lrr->namelen + 41,
					       lrr->line);
				lrr->shown = 1;
				break;
			}
		}
		if (lrr_count <= i) {
			error("pick-rref: %.*s not found", rreflen, rref);
			err = 1;
		}
		rref = next;
	}
	free(lrr_list);
	return err;
}

int cmd_fetch__tool(int argc, const char **argv, const char *prefix)
{
	int verbose = 0;
	int force = 0;
	int sopt = 0;

	while (1 < argc) {
		const char *arg = argv[1];
		if (!strcmp("-v", arg))
			verbose = 1;
		else if (!strcmp("-f", arg))
			force = 1;
		else if (!strcmp("-s", arg))
			sopt = 1;
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
		char *filename;

		if (argc != 8)
			return error("append-fetch-head takes 6 args");
		filename = git_path("FETCH_HEAD");
		fp = fopen(filename, "a");
		if (!fp)
			return error("cannot open %s: %s\n", filename, strerror(errno));
		result = append_fetch_head(fp, argv[2], argv[3],
					   argv[4], argv[5],
					   argv[6], !!argv[7][0],
					   verbose, force);
		fclose(fp);
		return result;
	}
	if (!strcmp("native-store", argv[1])) {
		int result;
		FILE *fp;
		char *filename;

		if (argc != 5)
			return error("fetch-native-store takes 3 args");
		filename = git_path("FETCH_HEAD");
		fp = fopen(filename, "a");
		if (!fp)
			return error("cannot open %s: %s\n", filename, strerror(errno));
		result = fetch_native_store(fp, argv[2], argv[3], argv[4],
					    verbose, force);
		fclose(fp);
		return result;
	}
	if (!strcmp("parse-reflist", argv[1])) {
		const char *reflist;
		if (argc != 3)
			return error("parse-reflist takes 1 arg");
		reflist = argv[2];
		if (!strcmp(reflist, "-"))
			reflist = get_stdin();
		return parse_reflist(reflist);
	}
	if (!strcmp("pick-rref", argv[1])) {
		const char *ls_remote_result;
		if (argc != 4)
			return error("pick-rref takes 2 args");
		ls_remote_result = argv[3];
		if (!strcmp(ls_remote_result, "-"))
			ls_remote_result = get_stdin();
		return pick_rref(sopt, argv[2], ls_remote_result);
	}
	if (!strcmp("expand-refs-wildcard", argv[1])) {
		const char *reflist;
		if (argc < 4)
			return error("expand-refs-wildcard takes at least 2 args");
		reflist = argv[2];
		if (!strcmp(reflist, "-"))
			reflist = get_stdin();
		return expand_refs_wildcard(reflist, argc - 3, argv + 3);
	}

	return error("Unknown subcommand: %s", argv[1]);
}
