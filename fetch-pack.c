#include "cache.h"
#include "refs.h"
#include "pkt-line.h"
#include "commit.h"
#include "tag.h"
#include <time.h>
#include <sys/wait.h>

static int quiet;
static int verbose;
static const char fetch_pack_usage[] =
"git-fetch-pack [-q] [-v] [--exec=upload-pack] [host:]directory <refs>...";
static const char *exec = "git-upload-pack";

#define COMPLETE	(1U << 0)

static int find_common(int fd[2], unsigned char *result_sha1,
		       struct ref *refs)
{
	int fetching;
	static char line[1000];
	static char rev_command[1024];
	int count = 0, flushes = 0, retval, rev_command_len;
	FILE *revs;

	strcpy(rev_command, "git-rev-list $(git-rev-parse --all)");
	rev_command_len = strlen(rev_command);
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
			struct commit_list *p;
			struct commit *commit =
				(struct commit *) (o = deref_tag(o));
			if (!o)
				goto repair;
			if (o->type != commit_type)
				continue;
			p = commit->parents;
			while (p &&
			       rev_command_len + 44 < sizeof(rev_command)) {
				snprintf(rev_command + rev_command_len, 44,
					 " ^%s",
					 sha1_to_hex(p->item->object.sha1));
				rev_command_len += 43;
				p = p->next;
			}
			continue;
		}
	repair:
		packet_write(fd[1], "want %s\n", sha1_to_hex(remote));
		fetching++;
	}
	packet_flush(fd[1]);
	if (!fetching)
		return 1;

	revs = popen(rev_command, "r");
	if (!revs)
		die("unable to run 'git-rev-list'");

	flushes = 1;
	retval = -1;
	while (fgets(line, sizeof(line), revs) != NULL) {
		unsigned char sha1[20];
		if (get_sha1_hex(line, sha1))
			die("git-fetch-pack: expected object name, got crud");
		packet_write(fd[1], "have %s\n", sha1_to_hex(sha1));
		if (verbose)
			fprintf(stderr, "have %s\n", sha1_to_hex(sha1));
		if (!(31 & ++count)) {
			packet_flush(fd[1]);
			flushes++;

			/*
			 * We keep one window "ahead" of the other side, and
			 * will wait for an ACK only on the next one
			 */
			if (count == 32)
				continue;
			if (get_ack(fd[0], result_sha1)) {
				flushes = 0;
				retval = 0;
				if (verbose)
					fprintf(stderr, "got ack\n");
				break;
			}
			flushes--;
		}
	}
	pclose(revs);
	packet_write(fd[1], "done\n");
	if (verbose)
		fprintf(stderr, "done\n");
	while (flushes) {
		flushes--;
		if (get_ack(fd[0], result_sha1)) {
			if (verbose)
				fprintf(stderr, "got ack\n");
			return 0;
		}
	}
	return retval;
}

static struct commit_list *complete = NULL;

static int mark_complete(const char *path, const unsigned char *sha1)
{
	struct object *o = parse_object(sha1);

	while (o && o->type == tag_type) {
		struct tag *t = (struct tag *) o;
		if (!t->tagged)
			break; /* broken repository */
		o->flags |= COMPLETE;
		o = parse_object(t->tagged->sha1);
	}
	if (o && o->type == commit_type) {
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

static int everything_local(struct ref *refs)
{
	struct ref *ref;
	int retval;
	unsigned long cutoff = 0;

	track_object_refs = 0;
	save_commit_buffer = 0;

	for (ref = refs; ref; ref = ref->next) {
		struct object *o;

		o = parse_object(ref->old_sha1);
		if (!o)
			continue;

		/* We already have it -- which may mean that we were
		 * in sync with the other side at some time after
		 * that (it is OK if we guess wrong here).
		 */
		if (o->type == commit_type) {
			struct commit *commit = (struct commit *)o;
			if (!cutoff || cutoff < commit->date)
				cutoff = commit->date;
		}
	}

	for_each_ref(mark_complete);
	if (cutoff)
		mark_recent_complete_commits(cutoff);

	for (retval = 1; refs ; refs = refs->next) {
		const unsigned char *remote = refs->old_sha1;
		unsigned char local[20];
		struct object *o;

		o = parse_object(remote);
		if (!o || !(o->flags & COMPLETE)) {
			retval = 0;
			if (!verbose)
				continue;
			fprintf(stderr,
				"want %s (%s)\n", sha1_to_hex(remote),
				refs->name);
			continue;
		}

		memcpy(refs->new_sha1, local, 20);
		if (!verbose)
			continue;
		fprintf(stderr,
			"already have %s (%s)\n", sha1_to_hex(remote),
			refs->name);
	}
	return retval;
}

static int fetch_pack(int fd[2], int nr_match, char **match)
{
	struct ref *ref;
	unsigned char sha1[20];
	int status;
	pid_t pid;

	get_remote_heads(fd[0], &ref, nr_match, match, 1);
	if (!ref) {
		packet_flush(fd[1]);
		die("no matching remote head");
	}
	if (everything_local(ref)) {
		packet_flush(fd[1]);
		goto all_done;
	}
	if (find_common(fd, sha1, ref) < 0)
		fprintf(stderr, "warning: no common commits\n");
	pid = fork();
	if (pid < 0)
		die("git-fetch-pack: unable to fork off git-unpack-objects");
	if (!pid) {
		dup2(fd[0], 0);
		close(fd[0]);
		close(fd[1]);
		execlp("git-unpack-objects", "git-unpack-objects",
		       quiet ? "-q" : NULL, NULL);
		die("git-unpack-objects exec failed");
	}
	close(fd[0]);
	close(fd[1]);
	while (waitpid(pid, &status, 0) < 0) {
		if (errno != EINTR)
			die("waiting for git-unpack-objects: %s", strerror(errno));
	}
	if (WIFEXITED(status)) {
		int code = WEXITSTATUS(status);
		if (code)
			die("git-unpack-objects died with error code %d", code);
all_done:
		while (ref) {
			printf("%s %s\n",
			       sha1_to_hex(ref->old_sha1), ref->name);
			ref = ref->next;
		}
		return 0;
	}
	if (WIFSIGNALED(status)) {
		int sig = WTERMSIG(status);
		die("git-unpack-objects died of signal %d", sig);
	}
	die("Sherlock Holmes! git-unpack-objects died of unnatural causes %d!", status);
}

int main(int argc, char **argv)
{
	int i, ret, nr_heads;
	char *dest = NULL, **heads;
	int fd[2];
	pid_t pid;

	nr_heads = 0;
	heads = NULL;
	for (i = 1; i < argc; i++) {
		char *arg = argv[i];

		if (*arg == '-') {
			if (!strncmp("--exec=", arg, 7)) {
				exec = arg + 7;
				continue;
			}
			if (!strcmp("-q", arg)) {
				quiet = 1;
				continue;
			}
			if (!strcmp("-v", arg)) {
				verbose = 1;
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
	ret = fetch_pack(fd, nr_heads, heads);
	close(fd[0]);
	close(fd[1]);
	finish_connect(pid);
	return ret;
}
