#include "cache.h"
#include "object.h"
#include "commit.h"
#include "diff.h"
#include "revision.h"
#include "list-objects.h"
#include "exec_cmd.h"

/*
 * Basic handler for bundle files to connect repositories via sneakernet.
 * Invocation must include action.
 * This function can create a bundle or provide information on an existing
 * bundle supporting git-fetch, git-pull, and git-ls-remote
 */

static const char *bundle_usage="git-bundle (create <bundle> <git-rev-list args> | verify <bundle> | list-heads <bundle> [refname]... | unbundle <bundle> [refname]... )";

static const char bundle_signature[] = "# v2 git bundle\n";

struct ref_list {
	unsigned int nr, alloc;
	struct {
		unsigned char sha1[20];
		char *name;
	} *list;
};

static void add_to_ref_list(const unsigned char *sha1, const char *name,
		struct ref_list *list)
{
	if (list->nr + 1 >= list->alloc) {
		list->alloc = alloc_nr(list->nr + 1);
		list->list = xrealloc(list->list,
				list->alloc * sizeof(list->list[0]));
	}
	memcpy(list->list[list->nr].sha1, sha1, 20);
	list->list[list->nr].name = xstrdup(name);
	list->nr++;
}

struct bundle_header {
	struct ref_list prerequisites;
	struct ref_list references;
};

/* this function returns the length of the string */
static int read_string(int fd, char *buffer, int size)
{
	int i;
	for (i = 0; i < size - 1; i++) {
		int count = xread(fd, buffer + i, 1);
		if (count < 0)
			return error("Read error: %s", strerror(errno));
		if (count == 0) {
			i--;
			break;
		}
		if (buffer[i] == '\n')
			break;
	}
	buffer[i + 1] = '\0';
	return i + 1;
}

/* returns an fd */
static int read_header(const char *path, struct bundle_header *header) {
	char buffer[1024];
	int fd = open(path, O_RDONLY);

	if (fd < 0)
		return error("could not open '%s'", path);
	if (read_string(fd, buffer, sizeof(buffer)) < 0 ||
			strcmp(buffer, bundle_signature)) {
		close(fd);
		return error("'%s' does not look like a v2 bundle file", path);
	}
	while (read_string(fd, buffer, sizeof(buffer)) > 0
			&& buffer[0] != '\n') {
		int is_prereq = buffer[0] == '-';
		int offset = is_prereq ? 1 : 0;
		int len = strlen(buffer);
		unsigned char sha1[20];
		struct ref_list *list = is_prereq ? &header->prerequisites
			: &header->references;
		char delim;

		if (buffer[len - 1] == '\n')
			buffer[len - 1] = '\0';
		if (get_sha1_hex(buffer + offset, sha1)) {
			warn("unrecognized header: %s", buffer);
			continue;
		}
		delim = buffer[40 + offset];
		if (!isspace(delim) && (delim != '\0' || !is_prereq))
			die ("invalid header: %s", buffer);
		add_to_ref_list(sha1, isspace(delim) ?
				buffer + 41 + offset : "", list);
	}
	return fd;
}

/* if in && *in >= 0, take that as input file descriptor instead */
static int fork_with_pipe(const char **argv, int *in, int *out)
{
	int needs_in, needs_out;
	int fdin[2], fdout[2], pid;

	needs_in = in && *in < 0;
	if (needs_in) {
		if (pipe(fdin) < 0)
			return error("could not setup pipe");
		*in = fdin[1];
	}

	needs_out = out && *out < 0;
	if (needs_out) {
		if (pipe(fdout) < 0)
			return error("could not setup pipe");
		*out = fdout[0];
	}

	if ((pid = fork()) < 0) {
		if (needs_in) {
			close(fdin[0]);
			close(fdin[1]);
		}
		if (needs_out) {
			close(fdout[0]);
			close(fdout[1]);
		}
		return error("could not fork");
	}
	if (!pid) {
		if (needs_in) {
			dup2(fdin[0], 0);
			close(fdin[0]);
			close(fdin[1]);
		} else if (in) {
			dup2(*in, 0);
			close(*in);
		}
		if (needs_out) {
			dup2(fdout[1], 1);
			close(fdout[0]);
			close(fdout[1]);
		} else if (out) {
			dup2(*out, 1);
			close(*out);
		}
		exit(execv_git_cmd(argv));
	}
	if (needs_in)
		close(fdin[0]);
	else if (in)
		close(*in);
	if (needs_out)
		close(fdout[1]);
	else if (out)
		close(*out);
	return pid;
}

static int verify_bundle(struct bundle_header *header)
{
	/*
	 * Do fast check, then if any prereqs are missing then go line by line
	 * to be verbose about the errors
	 */
	struct ref_list *p = &header->prerequisites;
	char **argv;
	int pid, out, i, ret = 0;
	char buffer[1024];

	argv = xmalloc((p->nr + 4) * sizeof(const char *));
	argv[0] = "rev-list";
	argv[1] = "--not";
	argv[2] = "--all";
	for (i = 0; i < p->nr; i++)
		argv[i + 3] = xstrdup(sha1_to_hex(p->list[i].sha1));
	argv[p->nr + 3] = NULL;
	out = -1;
	pid = fork_with_pipe((const char **)argv, NULL, &out);
	if (pid < 0)
		return error("Could not fork rev-list");
	while (read_string(out, buffer, sizeof(buffer)) > 0)
		; /* do nothing */
	close(out);
	for (i = 0; i < p->nr; i++)
		free(argv[i + 3]);
	free(argv);

	while (waitpid(pid, &i, 0) < 0)
		if (errno != EINTR)
			return -1;
	if (!ret && (!WIFEXITED(i) || WEXITSTATUS(i)))
		return error("At least one prerequisite is lacking.");

	return ret;
}

static int list_heads(struct bundle_header *header, int argc, const char **argv)
{
	int i;
	struct ref_list *r = &header->references;

	for (i = 0; i < r->nr; i++) {
		if (argc > 1) {
			int j;
			for (j = 1; j < argc; j++)
				if (!strcmp(r->list[i].name, argv[j]))
					break;
			if (j == argc)
				continue;
		}
		printf("%s %s\n", sha1_to_hex(r->list[i].sha1),
				r->list[i].name);
	}
	return 0;
}

static void show_commit(struct commit *commit)
{
	write_or_die(1, sha1_to_hex(commit->object.sha1), 40);
	write_or_die(1, "\n", 1);
	if (commit->parents) {
		free_commit_list(commit->parents);
		commit->parents = NULL;
	}
}

static void show_object(struct object_array_entry *p)
{
	/* An object with name "foo\n0000000..." can be used to
	 * confuse downstream git-pack-objects very badly.
	 */
	const char *ep = strchr(p->name, '\n');
	int len = ep ? ep - p->name : strlen(p->name);
	write_or_die(1, sha1_to_hex(p->item->sha1), 40);
	write_or_die(1, " ", 1);
	if (len)
		write_or_die(1, p->name, len);
	write_or_die(1, "\n", 1);
}

static int create_bundle(struct bundle_header *header, const char *path,
		int argc, const char **argv)
{
	int bundle_fd = -1;
	const char **argv_boundary = xmalloc((argc + 3) * sizeof(const char *));
	const char **argv_pack = xmalloc(4 * sizeof(const char *));
	int pid, in, out, i, status;
	char buffer[1024];
	struct rev_info revs;

	bundle_fd = (!strcmp(path, "-") ? 1 :
			open(path, O_CREAT | O_WRONLY, 0666));
	if (bundle_fd < 0)
		return error("Could not write to '%s'", path);

	/* write signature */
	write_or_die(bundle_fd, bundle_signature, strlen(bundle_signature));

	/* write prerequisites */
	memcpy(argv_boundary + 2, argv + 1, argc * sizeof(const char *));
	argv_boundary[0] = "rev-list";
	argv_boundary[1] = "--boundary";
	argv_boundary[argc + 1] = NULL;
	out = -1;
	pid = fork_with_pipe(argv_boundary, NULL, &out);
	if (pid < 0)
		return -1;
	while ((i = read_string(out, buffer, sizeof(buffer))) > 0)
		if (buffer[0] == '-')
			write_or_die(bundle_fd, buffer, i);
	while ((i = waitpid(pid, &status, 0)) < 0)
		if (errno != EINTR)
			return error("rev-list died");
	if (!WIFEXITED(status) || WEXITSTATUS(status))
		return error("rev-list died %d", WEXITSTATUS(status));

	/* write references */
	save_commit_buffer = 0;
	init_revisions(&revs, NULL);
	revs.tag_objects = 1;
	revs.tree_objects = 1;
	revs.blob_objects = 1;
	argc = setup_revisions(argc, argv, &revs, NULL);
	if (argc > 1)
		return error("unrecognized argument: %s'", argv[1]);
	for (i = 0; i < revs.pending.nr; i++) {
		struct object_array_entry *e = revs.pending.objects + i;
		if (!(e->item->flags & UNINTERESTING)) {
			unsigned char sha1[20];
			char *ref;
			if (dwim_ref(e->name, strlen(e->name), sha1, &ref) != 1)
				continue;
			write_or_die(bundle_fd, sha1_to_hex(e->item->sha1), 40);
			write_or_die(bundle_fd, " ", 1);
			write_or_die(bundle_fd, ref, strlen(ref));
			write_or_die(bundle_fd, "\n", 1);
			free(ref);
		}
	}

	/* end header */
	write_or_die(bundle_fd, "\n", 1);

	/* write pack */
	argv_pack[0] = "pack-objects";
	argv_pack[1] = "--all-progress";
	argv_pack[2] = "--stdout";
	argv_pack[3] = NULL;
	in = -1;
	out = bundle_fd;
	pid = fork_with_pipe(argv_pack, &in, &out);
	if (pid < 0)
		return error("Could not spawn pack-objects");
	close(1);
	dup2(in, 1);
	close(in);
	prepare_revision_walk(&revs);
	traverse_commit_list(&revs, show_commit, show_object);
	close(1);
	while (waitpid(pid, &status, 0) < 0)
		if (errno != EINTR)
			return -1;
	if (!WIFEXITED(status) || WEXITSTATUS(status))
		return error ("pack-objects died");
	return 0;
}

static int unbundle(struct bundle_header *header, int bundle_fd,
		int argc, const char **argv)
{
	const char *argv_index_pack[] = {"index-pack", "--stdin", NULL};
	int pid, status, dev_null;

	if (verify_bundle(header))
		return -1;
	dev_null = open("/dev/null", O_WRONLY);
	pid = fork_with_pipe(argv_index_pack, &bundle_fd, &dev_null);
	if (pid < 0)
		return error("Could not spawn index-pack");
	while (waitpid(pid, &status, 0) < 0)
		if (errno != EINTR)
			return error("index-pack died");
	if (!WIFEXITED(status) || WEXITSTATUS(status))
		return error("index-pack exited with status %d",
				WEXITSTATUS(status));
	return list_heads(header, argc, argv);
}

int cmd_bundle(int argc, const char **argv, const char *prefix)
{
	struct bundle_header header;
	int nongit = 0;
	const char *cmd, *bundle_file;
	int bundle_fd = -1;
	char buffer[PATH_MAX];

	if (argc < 3)
		usage(bundle_usage);

	cmd = argv[1];
	bundle_file = argv[2];
	argc -= 2;
	argv += 2;

	prefix = setup_git_directory_gently(&nongit);
	if (prefix && bundle_file[0] != '/') {
		snprintf(buffer, sizeof(buffer), "%s/%s", prefix, bundle_file);
		bundle_file = buffer;
	}

	memset(&header, 0, sizeof(header));
	if (strcmp(cmd, "create") &&
			!(bundle_fd = read_header(bundle_file, &header)))
		return 1;

	if (!strcmp(cmd, "verify")) {
		close(bundle_fd);
		if (verify_bundle(&header))
			return 1;
		fprintf(stderr, "%s is okay\n", bundle_file);
		return 0;
	}
	if (!strcmp(cmd, "list-heads")) {
		close(bundle_fd);
		return !!list_heads(&header, argc, argv);
	}
	if (!strcmp(cmd, "create")) {
		if (nongit)
			die("Need a repository to create a bundle.");
		return !!create_bundle(&header, bundle_file, argc, argv);
	} else if (!strcmp(cmd, "unbundle")) {
		if (nongit)
			die("Need a repository to unbundle.");
		return !!unbundle(&header, bundle_fd, argc, argv);
	} else
		usage(bundle_usage);
}

