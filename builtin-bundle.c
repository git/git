#include "cache.h"
#include "object.h"
#include "commit.h"
#include "diff.h"
#include "revision.h"
#include "list-objects.h"
#include "run-command.h"

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
	struct ref_list_entry {
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
		ssize_t count = xread(fd, buffer + i, 1);
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
			warning("unrecognized header: %s", buffer);
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

static int list_refs(struct ref_list *r, int argc, const char **argv)
{
	int i;

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

#define PREREQ_MARK (1u<<16)

static int verify_bundle(struct bundle_header *header, int verbose)
{
	/*
	 * Do fast check, then if any prereqs are missing then go line by line
	 * to be verbose about the errors
	 */
	struct ref_list *p = &header->prerequisites;
	struct rev_info revs;
	const char *argv[] = {NULL, "--all"};
	struct object_array refs;
	struct commit *commit;
	int i, ret = 0, req_nr;
	const char *message = "Repository lacks these prerequisite commits:";

	init_revisions(&revs, NULL);
	for (i = 0; i < p->nr; i++) {
		struct ref_list_entry *e = p->list + i;
		struct object *o = parse_object(e->sha1);
		if (o) {
			o->flags |= PREREQ_MARK;
			add_pending_object(&revs, o, e->name);
			continue;
		}
		if (++ret == 1)
			error(message);
		error("%s %s", sha1_to_hex(e->sha1), e->name);
	}
	if (revs.pending.nr != p->nr)
		return ret;
	req_nr = revs.pending.nr;
	setup_revisions(2, argv, &revs, NULL);

	memset(&refs, 0, sizeof(struct object_array));
	for (i = 0; i < revs.pending.nr; i++) {
		struct object_array_entry *e = revs.pending.objects + i;
		add_object_array(e->item, e->name, &refs);
	}

	prepare_revision_walk(&revs);

	i = req_nr;
	while (i && (commit = get_revision(&revs)))
		if (commit->object.flags & PREREQ_MARK)
			i--;

	for (i = 0; i < req_nr; i++)
		if (!(refs.objects[i].item->flags & SHOWN)) {
			if (++ret == 1)
				error(message);
			error("%s %s", sha1_to_hex(refs.objects[i].item->sha1),
				refs.objects[i].name);
		}

	for (i = 0; i < refs.nr; i++)
		clear_commit_marks((struct commit *)refs.objects[i].item, -1);

	if (verbose) {
		struct ref_list *r;

		r = &header->references;
		printf("The bundle contains %d ref%s\n",
		       r->nr, (1 < r->nr) ? "s" : "");
		list_refs(r, 0, NULL);
		r = &header->prerequisites;
		printf("The bundle requires these %d ref%s\n",
		       r->nr, (1 < r->nr) ? "s" : "");
		list_refs(r, 0, NULL);
	}
	return ret;
}

static int list_heads(struct bundle_header *header, int argc, const char **argv)
{
	return list_refs(&header->references, argc, argv);
}

static int create_bundle(struct bundle_header *header, const char *path,
		int argc, const char **argv)
{
	int bundle_fd = -1;
	const char **argv_boundary = xmalloc((argc + 4) * sizeof(const char *));
	const char **argv_pack = xmalloc(5 * sizeof(const char *));
	int i, ref_count = 0;
	char buffer[1024];
	struct rev_info revs;
	struct child_process rls;

	bundle_fd = (!strcmp(path, "-") ? 1 :
			open(path, O_CREAT | O_EXCL | O_WRONLY, 0666));
	if (bundle_fd < 0)
		return error("Could not create '%s': %s", path, strerror(errno));

	/* write signature */
	write_or_die(bundle_fd, bundle_signature, strlen(bundle_signature));

	/* init revs to list objects for pack-objects later */
	save_commit_buffer = 0;
	init_revisions(&revs, NULL);

	/* write prerequisites */
	memcpy(argv_boundary + 3, argv + 1, argc * sizeof(const char *));
	argv_boundary[0] = "rev-list";
	argv_boundary[1] = "--boundary";
	argv_boundary[2] = "--pretty=oneline";
	argv_boundary[argc + 2] = NULL;
	memset(&rls, 0, sizeof(rls));
	rls.argv = argv_boundary;
	rls.out = -1;
	rls.git_cmd = 1;
	if (start_command(&rls))
		return -1;
	while ((i = read_string(rls.out, buffer, sizeof(buffer))) > 0) {
		unsigned char sha1[20];
		if (buffer[0] == '-') {
			write_or_die(bundle_fd, buffer, i);
			if (!get_sha1_hex(buffer + 1, sha1)) {
				struct object *object = parse_object(sha1);
				object->flags |= UNINTERESTING;
				add_pending_object(&revs, object, buffer);
			}
		} else if (!get_sha1_hex(buffer, sha1)) {
			struct object *object = parse_object(sha1);
			object->flags |= SHOWN;
		}
	}
	if (finish_command(&rls))
		return error("rev-list died");

	/* write references */
	argc = setup_revisions(argc, argv, &revs, NULL);
	if (argc > 1)
		return error("unrecognized argument: %s'", argv[1]);

	for (i = 0; i < revs.pending.nr; i++) {
		struct object_array_entry *e = revs.pending.objects + i;
		unsigned char sha1[20];
		char *ref;

		if (e->item->flags & UNINTERESTING)
			continue;
		if (dwim_ref(e->name, strlen(e->name), sha1, &ref) != 1)
			continue;
		/*
		 * Make sure the refs we wrote out is correct; --max-count and
		 * other limiting options could have prevented all the tips
		 * from getting output.
		 */
		if (!(e->item->flags & SHOWN)) {
			warning("ref '%s' is excluded by the rev-list options",
				e->name);
			continue;
		}
		ref_count++;
		write_or_die(bundle_fd, sha1_to_hex(e->item->sha1), 40);
		write_or_die(bundle_fd, " ", 1);
		write_or_die(bundle_fd, ref, strlen(ref));
		write_or_die(bundle_fd, "\n", 1);
		free(ref);
	}
	if (!ref_count)
		die ("Refusing to create empty bundle.");

	/* end header */
	write_or_die(bundle_fd, "\n", 1);

	/* write pack */
	argv_pack[0] = "pack-objects";
	argv_pack[1] = "--all-progress";
	argv_pack[2] = "--stdout";
	argv_pack[3] = "--thin";
	argv_pack[4] = NULL;
	memset(&rls, 0, sizeof(rls));
	rls.argv = argv_pack;
	rls.in = -1;
	rls.out = bundle_fd;
	rls.git_cmd = 1;
	if (start_command(&rls))
		return error("Could not spawn pack-objects");
	for (i = 0; i < revs.pending.nr; i++) {
		struct object *object = revs.pending.objects[i].item;
		if (object->flags & UNINTERESTING)
			write(rls.in, "^", 1);
		write(rls.in, sha1_to_hex(object->sha1), 40);
		write(rls.in, "\n", 1);
	}
	if (finish_command(&rls))
		return error ("pack-objects died");
	return 0;
}

static int unbundle(struct bundle_header *header, int bundle_fd,
		int argc, const char **argv)
{
	const char *argv_index_pack[] = {"index-pack",
		"--fix-thin", "--stdin", NULL};
	struct child_process ip;

	if (verify_bundle(header, 0))
		return -1;
	memset(&ip, 0, sizeof(ip));
	ip.argv = argv_index_pack;
	ip.in = bundle_fd;
	ip.no_stdout = 1;
	ip.git_cmd = 1;
	if (run_command(&ip))
		return error("index-pack died");
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
			(bundle_fd = read_header(bundle_file, &header)) < 0)
		return 1;

	if (!strcmp(cmd, "verify")) {
		close(bundle_fd);
		if (verify_bundle(&header, 1))
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
