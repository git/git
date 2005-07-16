#include "cache.h"
#include "refs.h"
#include "pkt-line.h"

static const char send_pack_usage[] =
"git-send-pack [--exec=git-receive-pack] [host:]directory [heads]*";
static const char *exec = "git-receive-pack";
static int send_all = 0;

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
	static char *args[] = {
		"git-pack-objects",
		"--stdout",
		NULL
	};
	execvp("git-pack-objects", args);
	die("git-pack-objects exec failed (%s)", strerror(errno));
}

static void exec_rev_list(struct ref *refs)
{
	static char *args[1000];
	int i = 0;

	args[i++] = "git-rev-list";	/* 0 */
	args[i++] = "--objects";	/* 1 */
	while (refs) {
		char *buf = malloc(100);
		if (i > 900)
			die("git-rev-list environment overflow");
		if (!is_zero_sha1(refs->old_sha1)) {
			args[i++] = buf;
			snprintf(buf, 50, "^%s", sha1_to_hex(refs->old_sha1));
			buf += 50;
		}
		if (!is_zero_sha1(refs->new_sha1)) {
			args[i++] = buf;
			snprintf(buf, 50, "%s", sha1_to_hex(refs->new_sha1));
		}
		refs = refs->next;
	}
	args[i] = NULL;
	execvp("git-rev-list", args);
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

static int read_ref(const char *ref, unsigned char *sha1)
{
	int fd, ret;
	char buffer[60];

	fd = open(git_path("%s", ref), O_RDONLY);
	if (fd < 0)
		return -1;
	ret = -1;
	if (read(fd, buffer, sizeof(buffer)) >= 40)
		ret = get_sha1_hex(buffer, sha1);
	close(fd);
	return ret;
}

static int ref_newer(const unsigned char *new_sha1, const unsigned char *old_sha1)
{
	if (!has_sha1_file(old_sha1))
		return 0;
	/*
	 * FIXME! It is not correct to say that the new one is newer
	 * just because we don't have the old one!
	 *
	 * We should really see if we can reach the old_sha1 commit
	 * from the new_sha1 one.
	 */
	return 1;
}

static int local_ref_nr_match;
static char **local_ref_match;
static struct ref *local_ref_list;
static struct ref **local_last_ref;

static int try_to_match(const char *refname, const unsigned char *sha1)
{
	struct ref *ref;
	int len;

	if (!path_match(refname, local_ref_nr_match, local_ref_match)) {
		if (!send_all)
			return 0;

		/* If we have it listed already, skip it */
		for (ref = local_ref_list ; ref ; ref = ref->next) {
			if (!strcmp(ref->name, refname))
				return 0;
		}
	}

	len = strlen(refname)+1;
	ref = xmalloc(sizeof(*ref) + len);
	memset(ref->old_sha1, 0, 20);
	memcpy(ref->new_sha1, sha1, 20);
	memcpy(ref->name, refname, len);
	ref->next = NULL;
	*local_last_ref = ref;
	local_last_ref = &ref->next;
	return 0;
}

static int send_pack(int in, int out, int nr_match, char **match)
{
	struct ref *ref_list, **last_ref;
	struct ref *ref;
	int new_refs;

	/* First we get all heads, whether matching or not.. */
	last_ref = get_remote_heads(in, &ref_list, 0, NULL);

	/*
	 * Go through the refs, see if we want to update
	 * any of them..
	 */
	for (ref = ref_list; ref; ref = ref->next) {
		unsigned char new_sha1[20];
		char *name = ref->name;

		if (nr_match && !path_match(name, nr_match, match))
			continue;

		if (read_ref(name, new_sha1) < 0)
			continue;

		if (!memcmp(ref->old_sha1, new_sha1, 20)) {
			fprintf(stderr, "'%s' unchanged\n", name);
			continue;
		}

		if (!ref_newer(new_sha1, ref->old_sha1)) {
			error("remote '%s' points to object I don't have", name);
			continue;
		}

		/* Ok, mark it for update */
		memcpy(ref->new_sha1, new_sha1, 20);
	}

	/*
	 * See if we have any refs that the other end didn't have
	 */
	if (nr_match) {
		local_ref_nr_match = nr_match;
		local_ref_match = match;
		local_ref_list = ref_list;
		local_last_ref = last_ref;
		for_each_ref(try_to_match);
	}

	/*
	 * Finally, tell the other end!
	 */
	new_refs = 0;
	for (ref = ref_list; ref; ref = ref->next) {
		char old_hex[60], *new_hex;
		if (is_zero_sha1(ref->new_sha1))
			continue;
		new_refs++;
		strcpy(old_hex, sha1_to_hex(ref->old_sha1));
		new_hex = sha1_to_hex(ref->new_sha1);
		packet_write(out, "%s %s %s", old_hex, new_hex, ref->name);
		fprintf(stderr, "'%s': updating from %s to %s\n", ref->name, old_hex, new_hex);
	}
	
	packet_flush(out);
	if (new_refs)
		pack_objects(out, ref_list);
	close(out);
	return 0;
}

int main(int argc, char **argv)
{
	int i, nr_heads = 0;
	char *dest = NULL;
	char **heads = NULL;
	int fd[2], ret;
	pid_t pid;

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
	pid = git_connect(fd, dest, exec);
	if (pid < 0)
		return 1;
	ret = send_pack(fd[0], fd[1], nr_heads, heads);
	close(fd[0]);
	close(fd[1]);
	finish_connect(pid);
	return ret;
}
