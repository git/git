#include "cache.h"
#include "commit.h"
#include "tag.h"
#include "refs.h"
#include "pkt-line.h"
#include "run-command.h"
#include "remote.h"
#include "send-pack.h"

static const char send_pack_usage[] =
"git send-pack [--all | --mirror] [--dry-run] [--force] [--receive-pack=<git-receive-pack>] [--verbose] [--thin] [<host>:]<directory> [<ref>...]\n"
"  --all and explicit <ref> specification are mutually exclusive.";

static struct send_pack_args args = {
	/* .receivepack = */ "git-receive-pack",
};

/*
 * Make a pack stream and spit it out into file descriptor fd
 */
static int pack_objects(int fd, struct ref *refs)
{
	/*
	 * The child becomes pack-objects --revs; we feed
	 * the revision parameters to it via its stdin and
	 * let its stdout go back to the other end.
	 */
	const char *argv[] = {
		"pack-objects",
		"--all-progress",
		"--revs",
		"--stdout",
		NULL,
		NULL,
	};
	struct child_process po;

	if (args.use_thin_pack)
		argv[4] = "--thin";
	memset(&po, 0, sizeof(po));
	po.argv = argv;
	po.in = -1;
	po.out = fd;
	po.git_cmd = 1;
	if (start_command(&po))
		die("git-pack-objects failed (%s)", strerror(errno));

	/*
	 * We feed the pack-objects we just spawned with revision
	 * parameters by writing to the pipe.
	 */
	while (refs) {
		char buf[42];

		if (!is_null_sha1(refs->old_sha1) &&
		    has_sha1_file(refs->old_sha1)) {
			memcpy(buf + 1, sha1_to_hex(refs->old_sha1), 40);
			buf[0] = '^';
			buf[41] = '\n';
			if (!write_or_whine(po.in, buf, 42,
						"send-pack: send refs"))
				break;
		}
		if (!is_null_sha1(refs->new_sha1)) {
			memcpy(buf, sha1_to_hex(refs->new_sha1), 40);
			buf[40] = '\n';
			if (!write_or_whine(po.in, buf, 41,
						"send-pack: send refs"))
				break;
		}
		refs = refs->next;
	}

	close(po.in);
	if (finish_command(&po))
		return error("pack-objects died with strange error");
	return 0;
}

static void unmark_and_free(struct commit_list *list, unsigned int mark)
{
	while (list) {
		struct commit_list *temp = list;
		temp->item->object.flags &= ~mark;
		list = temp->next;
		free(temp);
	}
}

static int ref_newer(const unsigned char *new_sha1,
		     const unsigned char *old_sha1)
{
	struct object *o;
	struct commit *old, *new;
	struct commit_list *list, *used;
	int found = 0;

	/* Both new and old must be commit-ish and new is descendant of
	 * old.  Otherwise we require --force.
	 */
	o = deref_tag(parse_object(old_sha1), NULL, 0);
	if (!o || o->type != OBJ_COMMIT)
		return 0;
	old = (struct commit *) o;

	o = deref_tag(parse_object(new_sha1), NULL, 0);
	if (!o || o->type != OBJ_COMMIT)
		return 0;
	new = (struct commit *) o;

	if (parse_commit(new) < 0)
		return 0;

	used = list = NULL;
	commit_list_insert(new, &list);
	while (list) {
		new = pop_most_recent_commit(&list, 1);
		commit_list_insert(new, &used);
		if (new == old) {
			found = 1;
			break;
		}
	}
	unmark_and_free(list, 1);
	unmark_and_free(used, 1);
	return found;
}

static struct ref *local_refs, **local_tail;
static struct ref *remote_refs, **remote_tail;

static int one_local_ref(const char *refname, const unsigned char *sha1, int flag, void *cb_data)
{
	struct ref *ref;
	int len = strlen(refname) + 1;
	ref = xcalloc(1, sizeof(*ref) + len);
	hashcpy(ref->new_sha1, sha1);
	memcpy(ref->name, refname, len);
	*local_tail = ref;
	local_tail = &ref->next;
	return 0;
}

static void get_local_heads(void)
{
	local_tail = &local_refs;
	for_each_ref(one_local_ref, NULL);
}

static int receive_status(int in, struct ref *refs)
{
	struct ref *hint;
	char line[1000];
	int ret = 0;
	int len = packet_read_line(in, line, sizeof(line));
	if (len < 10 || memcmp(line, "unpack ", 7))
		return error("did not receive remote status");
	if (memcmp(line, "unpack ok\n", 10)) {
		char *p = line + strlen(line) - 1;
		if (*p == '\n')
			*p = '\0';
		error("unpack failed: %s", line + 7);
		ret = -1;
	}
	hint = NULL;
	while (1) {
		char *refname;
		char *msg;
		len = packet_read_line(in, line, sizeof(line));
		if (!len)
			break;
		if (len < 3 ||
		    (memcmp(line, "ok ", 3) && memcmp(line, "ng ", 3))) {
			fprintf(stderr, "protocol error: %s\n", line);
			ret = -1;
			break;
		}

		line[strlen(line)-1] = '\0';
		refname = line + 3;
		msg = strchr(refname, ' ');
		if (msg)
			*msg++ = '\0';

		/* first try searching at our hint, falling back to all refs */
		if (hint)
			hint = find_ref_by_name(hint, refname);
		if (!hint)
			hint = find_ref_by_name(refs, refname);
		if (!hint) {
			warning("remote reported status on unknown ref: %s",
					refname);
			continue;
		}
		if (hint->status != REF_STATUS_EXPECTING_REPORT) {
			warning("remote reported status on unexpected ref: %s",
					refname);
			continue;
		}

		if (line[0] == 'o' && line[1] == 'k')
			hint->status = REF_STATUS_OK;
		else {
			hint->status = REF_STATUS_REMOTE_REJECT;
			ret = -1;
		}
		if (msg)
			hint->remote_status = xstrdup(msg);
		/* start our next search from the next ref */
		hint = hint->next;
	}
	return ret;
}

static void update_tracking_ref(struct remote *remote, struct ref *ref)
{
	struct refspec rs;

	if (ref->status != REF_STATUS_OK)
		return;

	rs.src = ref->name;
	rs.dst = NULL;

	if (!remote_find_tracking(remote, &rs)) {
		if (args.verbose)
			fprintf(stderr, "updating local tracking ref '%s'\n", rs.dst);
		if (ref->deletion) {
			delete_ref(rs.dst, NULL);
		} else
			update_ref("update by push", rs.dst,
					ref->new_sha1, NULL, 0, 0);
		free(rs.dst);
	}
}

static const char *prettify_ref(const struct ref *ref)
{
	const char *name = ref->name;
	return name + (
		!prefixcmp(name, "refs/heads/") ? 11 :
		!prefixcmp(name, "refs/tags/") ? 10 :
		!prefixcmp(name, "refs/remotes/") ? 13 :
		0);
}

#define SUMMARY_WIDTH (2 * DEFAULT_ABBREV + 3)

static void print_ref_status(char flag, const char *summary, struct ref *to, struct ref *from, const char *msg)
{
	fprintf(stderr, " %c %-*s ", flag, SUMMARY_WIDTH, summary);
	if (from)
		fprintf(stderr, "%s -> %s", prettify_ref(from), prettify_ref(to));
	else
		fputs(prettify_ref(to), stderr);
	if (msg) {
		fputs(" (", stderr);
		fputs(msg, stderr);
		fputc(')', stderr);
	}
	fputc('\n', stderr);
}

static const char *status_abbrev(unsigned char sha1[20])
{
	return find_unique_abbrev(sha1, DEFAULT_ABBREV);
}

static void print_ok_ref_status(struct ref *ref)
{
	if (ref->deletion)
		print_ref_status('-', "[deleted]", ref, NULL, NULL);
	else if (is_null_sha1(ref->old_sha1))
		print_ref_status('*',
			(!prefixcmp(ref->name, "refs/tags/") ? "[new tag]" :
			  "[new branch]"),
			ref, ref->peer_ref, NULL);
	else {
		char quickref[84];
		char type;
		const char *msg;

		strcpy(quickref, status_abbrev(ref->old_sha1));
		if (ref->nonfastforward) {
			strcat(quickref, "...");
			type = '+';
			msg = "forced update";
		} else {
			strcat(quickref, "..");
			type = ' ';
			msg = NULL;
		}
		strcat(quickref, status_abbrev(ref->new_sha1));

		print_ref_status(type, quickref, ref, ref->peer_ref, msg);
	}
}

static int print_one_push_status(struct ref *ref, const char *dest, int count)
{
	if (!count)
		fprintf(stderr, "To %s\n", dest);

	switch(ref->status) {
	case REF_STATUS_NONE:
		print_ref_status('X', "[no match]", ref, NULL, NULL);
		break;
	case REF_STATUS_REJECT_NODELETE:
		print_ref_status('!', "[rejected]", ref, NULL,
				"remote does not support deleting refs");
		break;
	case REF_STATUS_UPTODATE:
		print_ref_status('=', "[up to date]", ref,
				ref->peer_ref, NULL);
		break;
	case REF_STATUS_REJECT_NONFASTFORWARD:
		print_ref_status('!', "[rejected]", ref, ref->peer_ref,
				"non-fast forward");
		break;
	case REF_STATUS_REMOTE_REJECT:
		print_ref_status('!', "[remote rejected]", ref,
				ref->deletion ? NULL : ref->peer_ref,
				ref->remote_status);
		break;
	case REF_STATUS_EXPECTING_REPORT:
		print_ref_status('!', "[remote failure]", ref,
				ref->deletion ? NULL : ref->peer_ref,
				"remote failed to report status");
		break;
	case REF_STATUS_OK:
		print_ok_ref_status(ref);
		break;
	}

	return 1;
}

static void print_push_status(const char *dest, struct ref *refs)
{
	struct ref *ref;
	int n = 0;

	if (args.verbose) {
		for (ref = refs; ref; ref = ref->next)
			if (ref->status == REF_STATUS_UPTODATE)
				n += print_one_push_status(ref, dest, n);
	}

	for (ref = refs; ref; ref = ref->next)
		if (ref->status == REF_STATUS_OK)
			n += print_one_push_status(ref, dest, n);

	for (ref = refs; ref; ref = ref->next) {
		if (ref->status != REF_STATUS_NONE &&
		    ref->status != REF_STATUS_UPTODATE &&
		    ref->status != REF_STATUS_OK)
			n += print_one_push_status(ref, dest, n);
	}
}

static int refs_pushed(struct ref *ref)
{
	for (; ref; ref = ref->next) {
		switch(ref->status) {
		case REF_STATUS_NONE:
		case REF_STATUS_UPTODATE:
			break;
		default:
			return 1;
		}
	}
	return 0;
}

static int do_send_pack(int in, int out, struct remote *remote, const char *dest, int nr_refspec, const char **refspec)
{
	struct ref *ref;
	int new_refs;
	int ask_for_status_report = 0;
	int allow_deleting_refs = 0;
	int expect_status_report = 0;
	int flags = MATCH_REFS_NONE;
	int ret;

	if (args.send_all)
		flags |= MATCH_REFS_ALL;
	if (args.send_mirror)
		flags |= MATCH_REFS_MIRROR;

	/* No funny business with the matcher */
	remote_tail = get_remote_heads(in, &remote_refs, 0, NULL, REF_NORMAL);
	get_local_heads();

	/* Does the other end support the reporting? */
	if (server_supports("report-status"))
		ask_for_status_report = 1;
	if (server_supports("delete-refs"))
		allow_deleting_refs = 1;

	/* match them up */
	if (!remote_tail)
		remote_tail = &remote_refs;
	if (match_refs(local_refs, remote_refs, &remote_tail,
		       nr_refspec, refspec, flags)) {
		close(out);
		return -1;
	}

	if (!remote_refs) {
		fprintf(stderr, "No refs in common and none specified; doing nothing.\n"
			"Perhaps you should specify a branch such as 'master'.\n");
		close(out);
		return 0;
	}

	/*
	 * Finally, tell the other end!
	 */
	new_refs = 0;
	for (ref = remote_refs; ref; ref = ref->next) {
		const unsigned char *new_sha1;

		if (!ref->peer_ref) {
			if (!args.send_mirror)
				continue;
			new_sha1 = null_sha1;
		}
		else
			new_sha1 = ref->peer_ref->new_sha1;


		ref->deletion = is_null_sha1(new_sha1);
		if (ref->deletion && !allow_deleting_refs) {
			ref->status = REF_STATUS_REJECT_NODELETE;
			continue;
		}
		if (!ref->deletion &&
		    !hashcmp(ref->old_sha1, new_sha1)) {
			ref->status = REF_STATUS_UPTODATE;
			continue;
		}

		/* This part determines what can overwrite what.
		 * The rules are:
		 *
		 * (0) you can always use --force or +A:B notation to
		 *     selectively force individual ref pairs.
		 *
		 * (1) if the old thing does not exist, it is OK.
		 *
		 * (2) if you do not have the old thing, you are not allowed
		 *     to overwrite it; you would not know what you are losing
		 *     otherwise.
		 *
		 * (3) if both new and old are commit-ish, and new is a
		 *     descendant of old, it is OK.
		 *
		 * (4) regardless of all of the above, removing :B is
		 *     always allowed.
		 */

		ref->nonfastforward =
		    !ref->deletion &&
		    !is_null_sha1(ref->old_sha1) &&
		    (!has_sha1_file(ref->old_sha1)
		      || !ref_newer(new_sha1, ref->old_sha1));

		if (ref->nonfastforward && !ref->force && !args.force_update) {
			ref->status = REF_STATUS_REJECT_NONFASTFORWARD;
			continue;
		}

		hashcpy(ref->new_sha1, new_sha1);
		if (!ref->deletion)
			new_refs++;

		if (!args.dry_run) {
			char *old_hex = sha1_to_hex(ref->old_sha1);
			char *new_hex = sha1_to_hex(ref->new_sha1);

			if (ask_for_status_report) {
				packet_write(out, "%s %s %s%c%s",
					old_hex, new_hex, ref->name, 0,
					"report-status");
				ask_for_status_report = 0;
				expect_status_report = 1;
			}
			else
				packet_write(out, "%s %s %s",
					old_hex, new_hex, ref->name);
		}
		ref->status = expect_status_report ?
			REF_STATUS_EXPECTING_REPORT :
			REF_STATUS_OK;
	}

	packet_flush(out);
	if (new_refs && !args.dry_run) {
		if (pack_objects(out, remote_refs) < 0)
			return -1;
	}
	else
		close(out);

	if (expect_status_report)
		ret = receive_status(in, remote_refs);
	else
		ret = 0;

	print_push_status(dest, remote_refs);

	if (!args.dry_run && remote) {
		for (ref = remote_refs; ref; ref = ref->next)
			update_tracking_ref(remote, ref);
	}

	if (!refs_pushed(remote_refs))
		fprintf(stderr, "Everything up-to-date\n");
	if (ret < 0)
		return ret;
	for (ref = remote_refs; ref; ref = ref->next) {
		switch (ref->status) {
		case REF_STATUS_NONE:
		case REF_STATUS_UPTODATE:
		case REF_STATUS_OK:
			break;
		default:
			return -1;
		}
	}
	return 0;
}

static void verify_remote_names(int nr_heads, const char **heads)
{
	int i;

	for (i = 0; i < nr_heads; i++) {
		const char *local = heads[i];
		const char *remote = strrchr(heads[i], ':');

		if (*local == '+')
			local++;

		/* A matching refspec is okay.  */
		if (remote == local && remote[1] == '\0')
			continue;

		remote = remote ? (remote + 1) : local;
		switch (check_ref_format(remote)) {
		case 0: /* ok */
		case CHECK_REF_FORMAT_ONELEVEL:
			/* ok but a single level -- that is fine for
			 * a match pattern.
			 */
		case CHECK_REF_FORMAT_WILDCARD:
			/* ok but ends with a pattern-match character */
			continue;
		}
		die("remote part of refspec is not a valid name in %s",
		    heads[i]);
	}
}

int cmd_send_pack(int argc, const char **argv, const char *prefix)
{
	int i, nr_heads = 0;
	const char **heads = NULL;
	const char *remote_name = NULL;
	struct remote *remote = NULL;
	const char *dest = NULL;

	argv++;
	for (i = 1; i < argc; i++, argv++) {
		const char *arg = *argv;

		if (*arg == '-') {
			if (!prefixcmp(arg, "--receive-pack=")) {
				args.receivepack = arg + 15;
				continue;
			}
			if (!prefixcmp(arg, "--exec=")) {
				args.receivepack = arg + 7;
				continue;
			}
			if (!prefixcmp(arg, "--remote=")) {
				remote_name = arg + 9;
				continue;
			}
			if (!strcmp(arg, "--all")) {
				args.send_all = 1;
				continue;
			}
			if (!strcmp(arg, "--dry-run")) {
				args.dry_run = 1;
				continue;
			}
			if (!strcmp(arg, "--mirror")) {
				args.send_mirror = 1;
				continue;
			}
			if (!strcmp(arg, "--force")) {
				args.force_update = 1;
				continue;
			}
			if (!strcmp(arg, "--verbose")) {
				args.verbose = 1;
				continue;
			}
			if (!strcmp(arg, "--thin")) {
				args.use_thin_pack = 1;
				continue;
			}
			usage(send_pack_usage);
		}
		if (!dest) {
			dest = arg;
			continue;
		}
		heads = (const char **) argv;
		nr_heads = argc - i;
		break;
	}
	if (!dest)
		usage(send_pack_usage);
	/*
	 * --all and --mirror are incompatible; neither makes sense
	 * with any refspecs.
	 */
	if ((heads && (args.send_all || args.send_mirror)) ||
					(args.send_all && args.send_mirror))
		usage(send_pack_usage);

	if (remote_name) {
		remote = remote_get(remote_name);
		if (!remote_has_url(remote, dest)) {
			die("Destination %s is not a uri for %s",
			    dest, remote_name);
		}
	}

	return send_pack(&args, dest, remote, nr_heads, heads);
}

int send_pack(struct send_pack_args *my_args,
	      const char *dest, struct remote *remote,
	      int nr_heads, const char **heads)
{
	int fd[2], ret;
	struct child_process *conn;

	memcpy(&args, my_args, sizeof(args));

	verify_remote_names(nr_heads, heads);

	conn = git_connect(fd, dest, args.receivepack, args.verbose ? CONNECT_VERBOSE : 0);
	ret = do_send_pack(fd[0], fd[1], remote, dest, nr_heads, heads);
	close(fd[0]);
	/* do_send_pack always closes fd[1] */
	ret |= finish_connect(conn);
	return !!ret;
}
