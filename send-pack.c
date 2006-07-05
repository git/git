#include "cache.h"
#include "commit.h"
#include "tag.h"
#include "refs.h"
#include "pkt-line.h"
#include "exec_cmd.h"

static const char send_pack_usage[] =
"git-send-pack [--all] [--exec=git-receive-pack] <remote> [<head>...]\n"
"  --all and explicit <head> specification are mutually exclusive.";
static const char *exec = "git-receive-pack";
static int verbose = 0;
static int send_all = 0;
static int force_update = 0;
static int use_thin_pack = 0;

static int is_zero_sha1(const unsigned char *sha1)
{
	int i;

	for (i = 0; i < 20; i++) {
		if (*sha1++)
			return 0;
	}
	return 1;
}

static void exec_pack_objects(void)
{
	static const char *args[] = {
		"pack-objects",
		"--stdout",
		NULL
	};
	execv_git_cmd(args);
	die("git-pack-objects exec failed (%s)", strerror(errno));
}

static void exec_rev_list(struct ref *refs)
{
	struct ref *ref;
	static const char *args[1000];
	int i = 0, j;

	args[i++] = "rev-list";	/* 0 */
	if (use_thin_pack)	/* 1 */
		args[i++] = "--objects-edge";
	else
		args[i++] = "--objects";

	/* First send the ones we care about most */
	for (ref = refs; ref; ref = ref->next) {
		if (900 < i)
			die("git-rev-list environment overflow");
		if (!is_zero_sha1(ref->new_sha1)) {
			char *buf = malloc(100);
			args[i++] = buf;
			snprintf(buf, 50, "%s", sha1_to_hex(ref->new_sha1));
			buf += 50;
			if (!is_zero_sha1(ref->old_sha1) &&
			    has_sha1_file(ref->old_sha1)) {
				args[i++] = buf;
				snprintf(buf, 50, "^%s",
					 sha1_to_hex(ref->old_sha1));
			}
		}
	}

	/* Then a handful of the remainder
	 * NEEDSWORK: we would be better off if used the newer ones first.
	 */
	for (ref = refs, j = i + 16;
	     i < 900 && i < j && ref;
	     ref = ref->next) {
		if (is_zero_sha1(ref->new_sha1) &&
		    !is_zero_sha1(ref->old_sha1) &&
		    has_sha1_file(ref->old_sha1)) {
			char *buf = malloc(42);
			args[i++] = buf;
			snprintf(buf, 42, "^%s", sha1_to_hex(ref->old_sha1));
		}
	}
	args[i] = NULL;
	execv_git_cmd(args);
	die("git-rev-list exec failed (%s)", strerror(errno));
}

static void rev_list(int fd, struct ref *refs)
{
	int pipe_fd[2];
	pid_t pack_objects_pid;

	if (pipe(pipe_fd) < 0)
		die("rev-list setup: pipe failed");
	pack_objects_pid = fork();
	if (!pack_objects_pid) {
		dup2(pipe_fd[0], 0);
		dup2(fd, 1);
		close(pipe_fd[0]);
		close(pipe_fd[1]);
		close(fd);
		exec_pack_objects();
		die("pack-objects setup failed");
	}
	if (pack_objects_pid < 0)
		die("pack-objects fork failed");
	dup2(pipe_fd[1], 1);
	close(pipe_fd[0]);
	close(pipe_fd[1]);
	close(fd);
	exec_rev_list(refs);
}

static int pack_objects(int fd, struct ref *refs)
{
	pid_t rev_list_pid;

	rev_list_pid = fork();
	if (!rev_list_pid) {
		rev_list(fd, refs);
		die("rev-list setup failed");
	}
	if (rev_list_pid < 0)
		die("rev-list fork failed");
	/*
	 * We don't wait for the rev-list pipeline in the parent:
	 * we end up waiting for the other end instead
	 */
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
	if (!o || o->type != TYPE_COMMIT)
		return 0;
	old = (struct commit *) o;

	o = deref_tag(parse_object(new_sha1), NULL, 0);
	if (!o || o->type != TYPE_COMMIT)
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

static int one_local_ref(const char *refname, const unsigned char *sha1)
{
	struct ref *ref;
	int len = strlen(refname) + 1;
	ref = xcalloc(1, sizeof(*ref) + len);
	memcpy(ref->new_sha1, sha1, 20);
	memcpy(ref->name, refname, len);
	*local_tail = ref;
	local_tail = &ref->next;
	return 0;
}

static void get_local_heads(void)
{
	local_tail = &local_refs;
	for_each_ref(one_local_ref);
}

static int receive_status(int in)
{
	char line[1000];
	int ret = 0;
	int len = packet_read_line(in, line, sizeof(line));
	if (len < 10 || memcmp(line, "unpack ", 7)) {
		fprintf(stderr, "did not receive status back\n");
		return -1;
	}
	if (memcmp(line, "unpack ok\n", 10)) {
		fputs(line, stderr);
		ret = -1;
	}
	while (1) {
		len = packet_read_line(in, line, sizeof(line));
		if (!len)
			break;
		if (len < 3 ||
		    (memcmp(line, "ok", 2) && memcmp(line, "ng", 2))) {
			fprintf(stderr, "protocol error: %s\n", line);
			ret = -1;
			break;
		}
		if (!memcmp(line, "ok", 2))
			continue;
		fputs(line, stderr);
		ret = -1;
	}
	return ret;
}

static int send_pack(int in, int out, int nr_refspec, char **refspec)
{
	struct ref *ref;
	int new_refs;
	int ret = 0;
	int ask_for_status_report = 0;
	int expect_status_report = 0;

	/* No funny business with the matcher */
	remote_tail = get_remote_heads(in, &remote_refs, 0, NULL, REF_NORMAL);
	get_local_heads();

	/* Does the other end support the reporting? */
	if (server_supports("report-status"))
		ask_for_status_report = 1;

	/* match them up */
	if (!remote_tail)
		remote_tail = &remote_refs;
	if (match_refs(local_refs, remote_refs, &remote_tail,
		       nr_refspec, refspec, send_all))
		return -1;

	if (!remote_refs) {
		fprintf(stderr, "No refs in common and none specified; doing nothing.\n");
		return 0;
	}

	/*
	 * Finally, tell the other end!
	 */
	new_refs = 0;
	for (ref = remote_refs; ref; ref = ref->next) {
		char old_hex[60], *new_hex;
		if (!ref->peer_ref)
			continue;
		if (!memcmp(ref->old_sha1, ref->peer_ref->new_sha1, 20)) {
			if (verbose)
				fprintf(stderr, "'%s': up-to-date\n", ref->name);
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
		 */

		if (!force_update &&
		    !is_zero_sha1(ref->old_sha1) &&
		    !ref->force) {
			if (!has_sha1_file(ref->old_sha1) ||
			    !ref_newer(ref->peer_ref->new_sha1,
				       ref->old_sha1)) {
				/* We do not have the remote ref, or
				 * we know that the remote ref is not
				 * an ancestor of what we are trying to
				 * push.  Either way this can be losing
				 * commits at the remote end and likely
				 * we were not up to date to begin with.
				 */
				error("remote '%s' is not a strict "
				      "subset of local ref '%s'. "
				      "maybe you are not up-to-date and "
				      "need to pull first?",
				      ref->name,
				      ref->peer_ref->name);
				ret = -2;
				continue;
			}
		}
		memcpy(ref->new_sha1, ref->peer_ref->new_sha1, 20);
		if (is_zero_sha1(ref->new_sha1)) {
			error("cannot happen anymore");
			ret = -3;
			continue;
		}
		new_refs++;
		strcpy(old_hex, sha1_to_hex(ref->old_sha1));
		new_hex = sha1_to_hex(ref->new_sha1);

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
		fprintf(stderr, "updating '%s'", ref->name);
		if (strcmp(ref->name, ref->peer_ref->name))
			fprintf(stderr, " using '%s'", ref->peer_ref->name);
		fprintf(stderr, "\n  from %s\n  to   %s\n", old_hex, new_hex);
	}

	packet_flush(out);
	if (new_refs)
		pack_objects(out, remote_refs);
	close(out);

	if (expect_status_report) {
		if (receive_status(in))
			ret = -4;
	}

	if (!new_refs && ret == 0)
		fprintf(stderr, "Everything up-to-date\n");
	return ret;
}


int main(int argc, char **argv)
{
	int i, nr_heads = 0;
	char *dest = NULL;
	char **heads = NULL;
	int fd[2], ret;
	pid_t pid;

	setup_git_directory();
	git_config(git_default_config);

	argv++;
	for (i = 1; i < argc; i++, argv++) {
		char *arg = *argv;

		if (*arg == '-') {
			if (!strncmp(arg, "--exec=", 7)) {
				exec = arg + 7;
				continue;
			}
			if (!strcmp(arg, "--all")) {
				send_all = 1;
				continue;
			}
			if (!strcmp(arg, "--force")) {
				force_update = 1;
				continue;
			}
			if (!strcmp(arg, "--verbose")) {
				verbose = 1;
				continue;
			}
			if (!strcmp(arg, "--thin")) {
				use_thin_pack = 1;
				continue;
			}
			usage(send_pack_usage);
		}
		if (!dest) {
			dest = arg;
			continue;
		}
		heads = argv;
		nr_heads = argc - i;
		break;
	}
	if (!dest)
		usage(send_pack_usage);
	if (heads && send_all)
		usage(send_pack_usage);
	pid = git_connect(fd, dest, exec);
	if (pid < 0)
		return 1;
	ret = send_pack(fd[0], fd[1], nr_heads, heads);
	close(fd[0]);
	close(fd[1]);
	finish_connect(pid);
	return ret;
}
