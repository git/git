#include "cache.h"
#include "refs.h"
#include "pkt-line.h"

static const char clone_pack_usage[] =
"git-clone-pack [--exec=<git-upload-pack>] [<host>:]<directory> [<heads>]*";
static const char *exec = "git-upload-pack";

static void clone_handshake(int fd[2], struct ref *ref)
{
	unsigned char sha1[20];

	while (ref) {
		packet_write(fd[1], "want %s\n", sha1_to_hex(ref->old_sha1));
		ref = ref->next;
	}
	packet_flush(fd[1]);

	/* We don't have nuttin' */
	packet_write(fd[1], "done\n");
	if (get_ack(fd[0], sha1))
		error("Huh! git-clone-pack got positive ack for %s", sha1_to_hex(sha1));
}

static int is_master(struct ref *ref)
{
	return !strcmp(ref->name, "refs/heads/master");
}

static void write_one_ref(struct ref *ref)
{
	char *path = git_path("%s", ref->name);
	int fd;
	char *hex;

	if (!strncmp(ref->name, "refs/", 5) &&
	    check_ref_format(ref->name + 5)) {
		error("refusing to create funny ref '%s' locally", ref->name);
		return;
	}

	if (safe_create_leading_directories(path))
		die("unable to create leading directory for %s", ref->name);
	fd = open(path, O_CREAT | O_EXCL | O_WRONLY, 0666);
	if (fd < 0)
		die("unable to create ref %s", ref->name);
	hex = sha1_to_hex(ref->old_sha1);
	hex[40] = '\n';
	if (write(fd, hex, 41) != 41)
		die("unable to write ref %s", ref->name);
	close(fd);
}

static void write_refs(struct ref *ref)
{
	struct ref *head = NULL, *head_ptr, *master_ref;
	char *head_path;

	/* Upload-pack must report HEAD first */
	if (!strcmp(ref->name, "HEAD")) {
		head = ref;
		ref = ref->next;
	}
	head_ptr = NULL;
	master_ref = NULL;
	while (ref) {
		if (is_master(ref))
			master_ref = ref;
		if (head &&
		    !memcmp(ref->old_sha1, head->old_sha1, 20) &&
		    !strncmp(ref->name, "refs/heads/",11) &&
		    (!head_ptr || ref == master_ref))
			head_ptr = ref;

		write_one_ref(ref);
		ref = ref->next;
	}
	if (!head) {
		fprintf(stderr, "No HEAD in remote.\n");
		return;
	}

	head_path = strdup(git_path("HEAD"));
	if (!head_ptr) {
		/*
		 * If we had a master ref, and it wasn't HEAD, we need to undo the
		 * symlink, and write a standalone HEAD. Give a warning, because that's
		 * really really wrong.
		 */
		if (master_ref) {
			error("HEAD doesn't point to any refs! Making standalone HEAD");
			unlink(head_path);
		}
		write_one_ref(head);
		free(head_path);
		return;
	}

	/* We reset to the master branch if it's available */
	if (master_ref)
		return;

	fprintf(stderr, "Setting HEAD to %s\n", head_ptr->name);

	/*
	 * Uhhuh. Other end didn't have master. We start HEAD off with
	 * the first branch with the same value.
	 */
	if (create_symref(head_path, head_ptr->name) < 0)
		die("unable to link HEAD to %s", head_ptr->name);
	free(head_path);
}

static int clone_pack(int fd[2], int nr_match, char **match)
{
	struct ref *refs;
	int status;

	get_remote_heads(fd[0], &refs, nr_match, match, 1);
	if (!refs) {
		packet_flush(fd[1]);
		die("no matching remote head");
	}
	clone_handshake(fd, refs);

	status = receive_keep_pack(fd, "git-clone-pack");

	if (!status) {
		if (nr_match == 0)
			write_refs(refs);
		else
			while (refs) {
				printf("%s %s\n",
				       sha1_to_hex(refs->old_sha1),
				       refs->name);
				refs = refs->next;
			}
	}
	return status;
}

int main(int argc, char **argv)
{
	int i, ret, nr_heads;
	char *dest = NULL, **heads;
	int fd[2];
	pid_t pid;

	setup_git_directory();

	nr_heads = 0;
	heads = NULL;
	for (i = 1; i < argc; i++) {
		char *arg = argv[i];

		if (*arg == '-') {
			if (!strcmp("-q", arg))
				continue;
			if (!strncmp("--exec=", arg, 7)) {
				exec = arg + 7;
				continue;
			}
			usage(clone_pack_usage);
		}
		dest = arg;
		heads = argv + i + 1;
		nr_heads = argc - i - 1;
		break;
	}
	if (!dest)
		usage(clone_pack_usage);
	pid = git_connect(fd, dest, exec);
	if (pid < 0)
		return 1;
	ret = clone_pack(fd, nr_heads, heads);
	close(fd[0]);
	close(fd[1]);
	finish_connect(pid);
	return ret;
}
