#include "cache.h"
#include "refs.h"
#include "pkt-line.h"
#include "commit.h"
#include "tag.h"
#include "exec_cmd.h"
#include "sideband.h"

static int keep_pack;
static int quiet;
static int verbose;
static int fetch_all;
static int depth;
static const char fetch_pack_usage[] =
"git-fetch-pack [--all] [--quiet|-q] [--keep|-k] [--thin] [--exec=<git-upload-pack>] [--depth=<n>] [-v] [<host>:]<directory> [<refs>...]";
static const char *exec = "git-upload-pack";

#define COMPLETE	(1U << 0)
#define COMMON		(1U << 1)
#define COMMON_REF	(1U << 2)
#define SEEN		(1U << 3)
#define POPPED		(1U << 4)

/*
 * After sending this many "have"s if we do not get any new ACK , we
 * give up traversing our history.
 */
#define MAX_IN_VAIN 256

static struct commit_list *rev_list;
static int non_common_revs, multi_ack, use_thin_pack, use_sideband;

static void rev_list_push(struct commit *commit, int mark)
{
	if (!(commit->object.flags & mark)) {
		commit->object.flags |= mark;

		if (!(commit->object.parsed))
			parse_commit(commit);

		insert_by_date(commit, &rev_list);

		if (!(commit->object.flags & COMMON))
			non_common_revs++;
	}
}

static int rev_list_insert_ref(const char *path, const unsigned char *sha1, int flag, void *cb_data)
{
	struct object *o = deref_tag(parse_object(sha1), path, 0);

	if (o && o->type == OBJ_COMMIT)
		rev_list_push((struct commit *)o, SEEN);

	return 0;
}

/*
   This function marks a rev and its ancestors as common.
   In some cases, it is desirable to mark only the ancestors (for example
   when only the server does not yet know that they are common).
*/

static void mark_common(struct commit *commit,
		int ancestors_only, int dont_parse)
{
	if (commit != NULL && !(commit->object.flags & COMMON)) {
		struct object *o = (struct object *)commit;

		if (!ancestors_only)
			o->flags |= COMMON;

		if (!(o->flags & SEEN))
			rev_list_push(commit, SEEN);
		else {
			struct commit_list *parents;

			if (!ancestors_only && !(o->flags & POPPED))
				non_common_revs--;
			if (!o->parsed && !dont_parse)
				parse_commit(commit);

			for (parents = commit->parents;
					parents;
					parents = parents->next)
				mark_common(parents->item, 0, dont_parse);
		}
	}
}

/*
  Get the next rev to send, ignoring the common.
*/

static const unsigned char* get_rev(void)
{
	struct commit *commit = NULL;

	while (commit == NULL) {
		unsigned int mark;
		struct commit_list* parents;

		if (rev_list == NULL || non_common_revs == 0)
			return NULL;

		commit = rev_list->item;
		if (!(commit->object.parsed))
			parse_commit(commit);
		commit->object.flags |= POPPED;
		if (!(commit->object.flags & COMMON))
			non_common_revs--;
	
		parents = commit->parents;

		if (commit->object.flags & COMMON) {
			/* do not send "have", and ignore ancestors */
			commit = NULL;
			mark = COMMON | SEEN;
		} else if (commit->object.flags & COMMON_REF)
			/* send "have", and ignore ancestors */
			mark = COMMON | SEEN;
		else
			/* send "have", also for its ancestors */
			mark = SEEN;

		while (parents) {
			if (!(parents->item->object.flags & SEEN))
				rev_list_push(parents->item, mark);
			if (mark & COMMON)
				mark_common(parents->item, 1, 0);
			parents = parents->next;
		}

		rev_list = rev_list->next;
	}

	return commit->object.sha1;
}

static int find_common(int fd[2], unsigned char *result_sha1,
		       struct ref *refs)
{
	int fetching;
	int count = 0, flushes = 0, retval;
	const unsigned char *sha1;
	unsigned in_vain = 0;
	int got_continue = 0;

	for_each_ref(rev_list_insert_ref, NULL);

	fetching = 0;
	for ( ; refs ; refs = refs->next) {
		unsigned char *remote = refs->old_sha1;
		struct object *o;

		/*
		 * If that object is complete (i.e. it is an ancestor of a
		 * local ref), we tell them we have it but do not have to
		 * tell them about its ancestors, which they already know
		 * about.
		 *
		 * We use lookup_object here because we are only
		 * interested in the case we *know* the object is
		 * reachable and we have already scanned it.
		 */
		if (((o = lookup_object(remote)) != NULL) &&
				(o->flags & COMPLETE)) {
			continue;
		}

		if (!fetching)
			packet_write(fd[1], "want %s%s%s%s%s%s\n",
				     sha1_to_hex(remote),
				     (multi_ack ? " multi_ack" : ""),
				     (use_sideband == 2 ? " side-band-64k" : ""),
				     (use_sideband == 1 ? " side-band" : ""),
				     (use_thin_pack ? " thin-pack" : ""),
				     " ofs-delta");
		else
			packet_write(fd[1], "want %s\n", sha1_to_hex(remote));
		fetching++;
	}
	if (is_repository_shallow())
		write_shallow_commits(fd[1], 1);
	if (depth > 0)
		packet_write(fd[1], "deepen %d", depth);
	packet_flush(fd[1]);
	if (!fetching)
		return 1;

	if (depth > 0) {
		char line[1024];
		unsigned char sha1[20];
		int len;

		while ((len = packet_read_line(fd[0], line, sizeof(line)))) {
			if (!strncmp("shallow ", line, 8)) {
				if (get_sha1_hex(line + 8, sha1))
					die("invalid shallow line: %s", line);
				register_shallow(sha1);
				continue;
			}
			if (!strncmp("unshallow ", line, 10)) {
				if (get_sha1_hex(line + 10, sha1))
					die("invalid unshallow line: %s", line);
				if (!lookup_object(sha1))
					die("object not found: %s", line);
				/* make sure that it is parsed as shallow */
				parse_object(sha1);
				if (unregister_shallow(sha1))
					die("no shallow found: %s", line);
				continue;
			}
			die("expected shallow/unshallow, got %s", line);
		}
	}

	flushes = 0;
	retval = -1;
	while ((sha1 = get_rev())) {
		packet_write(fd[1], "have %s\n", sha1_to_hex(sha1));
		if (verbose)
			fprintf(stderr, "have %s\n", sha1_to_hex(sha1));
		in_vain++;
		if (!(31 & ++count)) {
			int ack;

			packet_flush(fd[1]);
			flushes++;

			/*
			 * We keep one window "ahead" of the other side, and
			 * will wait for an ACK only on the next one
			 */
			if (count == 32)
				continue;

			do {
				ack = get_ack(fd[0], result_sha1);
				if (verbose && ack)
					fprintf(stderr, "got ack %d %s\n", ack,
							sha1_to_hex(result_sha1));
				if (ack == 1) {
					flushes = 0;
					multi_ack = 0;
					retval = 0;
					goto done;
				} else if (ack == 2) {
					struct commit *commit =
						lookup_commit(result_sha1);
					mark_common(commit, 0, 1);
					retval = 0;
					in_vain = 0;
					got_continue = 1;
				}
			} while (ack);
			flushes--;
			if (got_continue && MAX_IN_VAIN < in_vain) {
				if (verbose)
					fprintf(stderr, "giving up\n");
				break; /* give up */
			}
		}
	}
done:
	packet_write(fd[1], "done\n");
	if (verbose)
		fprintf(stderr, "done\n");
	if (retval != 0) {
		multi_ack = 0;
		flushes++;
	}
	while (flushes || multi_ack) {
		int ack = get_ack(fd[0], result_sha1);
		if (ack) {
			if (verbose)
				fprintf(stderr, "got ack (%d) %s\n", ack,
					sha1_to_hex(result_sha1));
			if (ack == 1)
				return 0;
			multi_ack = 1;
			continue;
		}
		flushes--;
	}
	return retval;
}

static struct commit_list *complete;

static int mark_complete(const char *path, const unsigned char *sha1, int flag, void *cb_data)
{
	struct object *o = parse_object(sha1);

	while (o && o->type == OBJ_TAG) {
		struct tag *t = (struct tag *) o;
		if (!t->tagged)
			break; /* broken repository */
		o->flags |= COMPLETE;
		o = parse_object(t->tagged->sha1);
	}
	if (o && o->type == OBJ_COMMIT) {
		struct commit *commit = (struct commit *)o;
		commit->object.flags |= COMPLETE;
		insert_by_date(commit, &complete);
	}
	return 0;
}

static void mark_recent_complete_commits(unsigned long cutoff)
{
	while (complete && cutoff <= complete->item->date) {
		if (verbose)
			fprintf(stderr, "Marking %s as complete\n",
				sha1_to_hex(complete->item->object.sha1));
		pop_most_recent_commit(&complete, COMPLETE);
	}
}

static void filter_refs(struct ref **refs, int nr_match, char **match)
{
	struct ref **return_refs;
	struct ref *newlist = NULL;
	struct ref **newtail = &newlist;
	struct ref *ref, *next;
	struct ref *fastarray[32];

	if (nr_match && !fetch_all) {
		if (ARRAY_SIZE(fastarray) < nr_match)
			return_refs = xcalloc(nr_match, sizeof(struct ref *));
		else {
			return_refs = fastarray;
			memset(return_refs, 0, sizeof(struct ref *) * nr_match);
		}
	}
	else
		return_refs = NULL;

	for (ref = *refs; ref; ref = next) {
		next = ref->next;
		if (!memcmp(ref->name, "refs/", 5) &&
		    check_ref_format(ref->name + 5))
			; /* trash */
		else if (fetch_all &&
			 (!depth || strncmp(ref->name, "refs/tags/", 10) )) {
			*newtail = ref;
			ref->next = NULL;
			newtail = &ref->next;
			continue;
		}
		else {
			int order = path_match(ref->name, nr_match, match);
			if (order) {
				return_refs[order-1] = ref;
				continue; /* we will link it later */
			}
		}
		free(ref);
	}

	if (!fetch_all) {
		int i;
		for (i = 0; i < nr_match; i++) {
			ref = return_refs[i];
			if (ref) {
				*newtail = ref;
				ref->next = NULL;
				newtail = &ref->next;
			}
		}
		if (return_refs != fastarray)
			free(return_refs);
	}
	*refs = newlist;
}

static int everything_local(struct ref **refs, int nr_match, char **match)
{
	struct ref *ref;
	int retval;
	unsigned long cutoff = 0;

	track_object_refs = 0;
	save_commit_buffer = 0;

	for (ref = *refs; ref; ref = ref->next) {
		struct object *o;

		o = parse_object(ref->old_sha1);
		if (!o)
			continue;

		/* We already have it -- which may mean that we were
		 * in sync with the other side at some time after
		 * that (it is OK if we guess wrong here).
		 */
		if (o->type == OBJ_COMMIT) {
			struct commit *commit = (struct commit *)o;
			if (!cutoff || cutoff < commit->date)
				cutoff = commit->date;
		}
	}

	if (!depth) {
		for_each_ref(mark_complete, NULL);
		if (cutoff)
			mark_recent_complete_commits(cutoff);
	}

	/*
	 * Mark all complete remote refs as common refs.
	 * Don't mark them common yet; the server has to be told so first.
	 */
	for (ref = *refs; ref; ref = ref->next) {
		struct object *o = deref_tag(lookup_object(ref->old_sha1),
					     NULL, 0);

		if (!o || o->type != OBJ_COMMIT || !(o->flags & COMPLETE))
			continue;

		if (!(o->flags & SEEN)) {
			rev_list_push((struct commit *)o, COMMON_REF | SEEN);

			mark_common((struct commit *)o, 1, 1);
		}
	}

	filter_refs(refs, nr_match, match);

	for (retval = 1, ref = *refs; ref ; ref = ref->next) {
		const unsigned char *remote = ref->old_sha1;
		unsigned char local[20];
		struct object *o;

		o = lookup_object(remote);
		if (!o || !(o->flags & COMPLETE)) {
			retval = 0;
			if (!verbose)
				continue;
			fprintf(stderr,
				"want %s (%s)\n", sha1_to_hex(remote),
				ref->name);
			continue;
		}

		hashcpy(ref->new_sha1, local);
		if (!verbose)
			continue;
		fprintf(stderr,
			"already have %s (%s)\n", sha1_to_hex(remote),
			ref->name);
	}
	return retval;
}

static pid_t setup_sideband(int fd[2], int xd[2])
{
	pid_t side_pid;

	if (!use_sideband) {
		fd[0] = xd[0];
		fd[1] = xd[1];
		return 0;
	}
	/* xd[] is talking with upload-pack; subprocess reads from
	 * xd[0], spits out band#2 to stderr, and feeds us band#1
	 * through our fd[0].
	 */
	if (pipe(fd) < 0)
		die("fetch-pack: unable to set up pipe");
	side_pid = fork();
	if (side_pid < 0)
		die("fetch-pack: unable to fork off sideband demultiplexer");
	if (!side_pid) {
		/* subprocess */
		close(fd[0]);
		if (xd[0] != xd[1])
			close(xd[1]);
		if (recv_sideband("fetch-pack", xd[0], fd[1], 2))
			exit(1);
		exit(0);
	}
	close(xd[0]);
	close(fd[1]);
	fd[1] = xd[1];
	return side_pid;
}

static int get_pack(int xd[2], const char **argv)
{
	int status;
	pid_t pid, side_pid;
	int fd[2];

	side_pid = setup_sideband(fd, xd);
	pid = fork();
	if (pid < 0)
		die("fetch-pack: unable to fork off %s", argv[0]);
	if (!pid) {
		dup2(fd[0], 0);
		close(fd[0]);
		close(fd[1]);
		execv_git_cmd(argv);
		die("%s exec failed", argv[0]);
	}
	close(fd[0]);
	close(fd[1]);
	while (waitpid(pid, &status, 0) < 0) {
		if (errno != EINTR)
			die("waiting for %s: %s", argv[0], strerror(errno));
	}
	if (WIFEXITED(status)) {
		int code = WEXITSTATUS(status);
		if (code)
			die("%s died with error code %d", argv[0], code);
		return 0;
	}
	if (WIFSIGNALED(status)) {
		int sig = WTERMSIG(status);
		die("%s died of signal %d", argv[0], sig);
	}
	die("%s died of unnatural causes %d", argv[0], status);
}

static int explode_rx_pack(int xd[2])
{
	const char *argv[3] = { "unpack-objects", quiet ? "-q" : NULL, NULL };
	return get_pack(xd, argv);
}

static int keep_rx_pack(int xd[2])
{
	const char *argv[6];
	char keep_arg[256];
	int n = 0;

	argv[n++] = "index-pack";
	argv[n++] = "--stdin";
	if (!quiet)
		argv[n++] = "-v";
	if (use_thin_pack)
		argv[n++] = "--fix-thin";
	if (keep_pack > 1) {
		int s = sprintf(keep_arg, "--keep=fetch-pack %i on ", getpid());
		if (gethostname(keep_arg + s, sizeof(keep_arg) - s))
			strcpy(keep_arg + s, "localhost");
		argv[n++] = keep_arg;
	}
	argv[n] = NULL;
	return get_pack(xd, argv);
}

static int fetch_pack(int fd[2], int nr_match, char **match)
{
	struct ref *ref;
	unsigned char sha1[20];
	int status;

	get_remote_heads(fd[0], &ref, 0, NULL, 0);
	if (is_repository_shallow() && !server_supports("shallow"))
		die("Server does not support shallow clients");
	if (server_supports("multi_ack")) {
		if (verbose)
			fprintf(stderr, "Server supports multi_ack\n");
		multi_ack = 1;
	}
	if (server_supports("side-band-64k")) {
		if (verbose)
			fprintf(stderr, "Server supports side-band-64k\n");
		use_sideband = 2;
	}
	else if (server_supports("side-band")) {
		if (verbose)
			fprintf(stderr, "Server supports side-band\n");
		use_sideband = 1;
	}
	if (!ref) {
		packet_flush(fd[1]);
		die("no matching remote head");
	}
	if (everything_local(&ref, nr_match, match)) {
		packet_flush(fd[1]);
		goto all_done;
	}
	if (find_common(fd, sha1, ref) < 0)
		if (keep_pack != 1)
			/* When cloning, it is not unusual to have
			 * no common commit.
			 */
			fprintf(stderr, "warning: no common commits\n");

	status = (keep_pack) ? keep_rx_pack(fd) : explode_rx_pack(fd);
	if (status)
		die("git-fetch-pack: fetch failed.");

 all_done:
	while (ref) {
		printf("%s %s\n",
		       sha1_to_hex(ref->old_sha1), ref->name);
		ref = ref->next;
	}
	return 0;
}

static int remove_duplicates(int nr_heads, char **heads)
{
	int src, dst;

	for (src = dst = 0; src < nr_heads; src++) {
		/* If heads[src] is different from any of
		 * heads[0..dst], push it in.
		 */
		int i;
		for (i = 0; i < dst; i++) {
			if (!strcmp(heads[i], heads[src]))
				break;
		}
		if (i < dst)
			continue;
		if (src != dst)
			heads[dst] = heads[src];
		dst++;
	}
	heads[dst] = 0;
	return dst;
}

static struct lock_file lock;

int main(int argc, char **argv)
{
	int i, ret, nr_heads;
	char *dest = NULL, **heads;
	int fd[2];
	pid_t pid;
	struct stat st;

	setup_git_directory();

	nr_heads = 0;
	heads = NULL;
	for (i = 1; i < argc; i++) {
		char *arg = argv[i];

		if (*arg == '-') {
			if (!strncmp("--exec=", arg, 7)) {
				exec = arg + 7;
				continue;
			}
			if (!strcmp("--quiet", arg) || !strcmp("-q", arg)) {
				quiet = 1;
				continue;
			}
			if (!strcmp("--keep", arg) || !strcmp("-k", arg)) {
				keep_pack++;
				continue;
			}
			if (!strcmp("--thin", arg)) {
				use_thin_pack = 1;
				continue;
			}
			if (!strcmp("--all", arg)) {
				fetch_all = 1;
				continue;
			}
			if (!strcmp("-v", arg)) {
				verbose = 1;
				continue;
			}
			if (!strncmp("--depth=", arg, 8)) {
				depth = strtol(arg + 8, NULL, 0);
				if (stat(git_path("shallow"), &st))
					st.st_mtime = 0;
				continue;
			}
			usage(fetch_pack_usage);
		}
		dest = arg;
		heads = argv + i + 1;
		nr_heads = argc - i - 1;
		break;
	}
	if (!dest)
		usage(fetch_pack_usage);
	pid = git_connect(fd, dest, exec);
	if (pid < 0)
		return 1;
	if (heads && nr_heads)
		nr_heads = remove_duplicates(nr_heads, heads);
	ret = fetch_pack(fd, nr_heads, heads);
	close(fd[0]);
	close(fd[1]);
	ret |= finish_connect(pid);

	if (!ret && nr_heads) {
		/* If the heads to pull were given, we should have
		 * consumed all of them by matching the remote.
		 * Otherwise, 'git-fetch remote no-such-ref' would
		 * silently succeed without issuing an error.
		 */
		for (i = 0; i < nr_heads; i++)
			if (heads[i] && heads[i][0]) {
				error("no such remote ref %s", heads[i]);
				ret = 1;
			}
	}

	if (!ret && depth > 0) {
		struct cache_time mtime;
		char *shallow = git_path("shallow");
		int fd;

		mtime.sec = st.st_mtime;
#ifdef USE_NSEC
		mtime.usec = st.st_mtim.usec;
#endif
		if (stat(shallow, &st)) {
			if (mtime.sec)
				die("shallow file was removed during fetch");
		} else if (st.st_mtime != mtime.sec
#ifdef USE_NSEC
				|| st.st_mtim.usec != mtime.usec
#endif
			  )
			die("shallow file was changed during fetch");

		fd = hold_lock_file_for_update(&lock, shallow, 1);
		if (!write_shallow_commits(fd, 0)) {
			unlink(shallow);
			rollback_lock_file(&lock);
		} else {
			close(fd);
			commit_lock_file(&lock);
		}
	}

	return !!ret;
}
