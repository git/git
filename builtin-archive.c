/*
 * Copyright (c) 2006 Franck Bui-Huu
 * Copyright (c) 2006 Rene Scharfe
 */
#include <time.h>
#include "cache.h"
#include "builtin.h"
#include "archive.h"
#include "commit.h"
#include "tree-walk.h"
#include "exec_cmd.h"
#include "pkt-line.h"
#include "sideband.h"

static const char archive_usage[] = \
"git-archive --format=<fmt> [--prefix=<prefix>/] [--verbose] [<extra>] <tree-ish> [path...]";

struct archiver archivers[] = {
	{
		.name		= "tar",
		.write_archive	= write_tar_archive,
	},
	{
		.name		= "zip",
		.write_archive	= write_zip_archive,
		.parse_extra	= parse_extra_zip_args,
	},
};

static int run_remote_archiver(const char *remote, int argc,
			       const char **argv)
{
	char *url, buf[LARGE_PACKET_MAX];
	int fd[2], i, len, rv;
	pid_t pid;
	const char *exec = "git-upload-archive";
	int exec_at = 0;

	for (i = 1; i < argc; i++) {
		const char *arg = argv[i];
		if (!strncmp("--exec=", arg, 7)) {
			if (exec_at)
				die("multiple --exec specified");
			exec = arg + 7;
			exec_at = i;
			break;
		}
	}

	url = xstrdup(remote);
	pid = git_connect(fd, url, exec);
	if (pid < 0)
		return pid;

	for (i = 1; i < argc; i++) {
		if (i == exec_at)
			continue;
		packet_write(fd[1], "argument %s\n", argv[i]);
	}
	packet_flush(fd[1]);

	len = packet_read_line(fd[0], buf, sizeof(buf));
	if (!len)
		die("git-archive: expected ACK/NAK, got EOF");
	if (buf[len-1] == '\n')
		buf[--len] = 0;
	if (strcmp(buf, "ACK")) {
		if (len > 5 && !strncmp(buf, "NACK ", 5))
			die("git-archive: NACK %s", buf + 5);
		die("git-archive: protocol error");
	}

	len = packet_read_line(fd[0], buf, sizeof(buf));
	if (len)
		die("git-archive: expected a flush");

	/* Now, start reading from fd[0] and spit it out to stdout */
	rv = recv_sideband("archive", fd[0], 1, 2);
	close(fd[0]);
	rv |= finish_connect(pid);

	return !!rv;
}

static int init_archiver(const char *name, struct archiver *ar)
{
	int rv = -1, i;

	for (i = 0; i < ARRAY_SIZE(archivers); i++) {
		if (!strcmp(name, archivers[i].name)) {
			memcpy(ar, &archivers[i], sizeof(struct archiver));
			rv = 0;
			break;
		}
	}
	return rv;
}

void parse_pathspec_arg(const char **pathspec, struct archiver_args *ar_args)
{
	ar_args->pathspec = get_pathspec(ar_args->base, pathspec);
}

void parse_treeish_arg(const char **argv, struct archiver_args *ar_args,
		       const char *prefix)
{
	const char *name = argv[0];
	const unsigned char *commit_sha1;
	time_t archive_time;
	struct tree *tree;
	struct commit *commit;
	unsigned char sha1[20];

	if (get_sha1(name, sha1))
		die("Not a valid object name");

	commit = lookup_commit_reference_gently(sha1, 1);
	if (commit) {
		commit_sha1 = commit->object.sha1;
		archive_time = commit->date;
	} else {
		commit_sha1 = NULL;
		archive_time = time(NULL);
	}

	tree = parse_tree_indirect(sha1);
	if (tree == NULL)
		die("not a tree object");

	if (prefix) {
		unsigned char tree_sha1[20];
		unsigned int mode;
		int err;

		err = get_tree_entry(tree->object.sha1, prefix,
				     tree_sha1, &mode);
		if (err || !S_ISDIR(mode))
			die("current working directory is untracked");

		free(tree);
		tree = parse_tree_indirect(tree_sha1);
	}
	ar_args->tree = tree;
	ar_args->commit_sha1 = commit_sha1;
	ar_args->time = archive_time;
}

int parse_archive_args(int argc, const char **argv, struct archiver *ar)
{
	const char *extra_argv[MAX_EXTRA_ARGS];
	int extra_argc = 0;
	const char *format = NULL; /* might want to default to "tar" */
	const char *base = "";
	int verbose = 0;
	int i;

	for (i = 1; i < argc; i++) {
		const char *arg = argv[i];

		if (!strcmp(arg, "--list") || !strcmp(arg, "-l")) {
			for (i = 0; i < ARRAY_SIZE(archivers); i++)
				printf("%s\n", archivers[i].name);
			exit(0);
		}
		if (!strcmp(arg, "--verbose") || !strcmp(arg, "-v")) {
			verbose = 1;
			continue;
		}
		if (!strncmp(arg, "--format=", 9)) {
			format = arg + 9;
			continue;
		}
		if (!strncmp(arg, "--prefix=", 9)) {
			base = arg + 9;
			continue;
		}
		if (!strcmp(arg, "--")) {
			i++;
			break;
		}
		if (arg[0] == '-') {
			if (extra_argc > MAX_EXTRA_ARGS - 1)
				die("Too many extra options");
			extra_argv[extra_argc++] = arg;
			continue;
		}
		break;
	}

	/* We need at least one parameter -- tree-ish */
	if (argc - 1 < i)
		usage(archive_usage);
	if (!format)
		die("You must specify an archive format");
	if (init_archiver(format, ar) < 0)
		die("Unknown archive format '%s'", format);

	if (extra_argc) {
		if (!ar->parse_extra)
			die("'%s' format does not handle %s",
			    ar->name, extra_argv[0]);
		ar->args.extra = ar->parse_extra(extra_argc, extra_argv);
	}
	ar->args.verbose = verbose;
	ar->args.base = base;

	return i;
}

static const char *extract_remote_arg(int *ac, const char **av)
{
	int ix, iy, cnt = *ac;
	int no_more_options = 0;
	const char *remote = NULL;

	for (ix = iy = 1; ix < cnt; ix++) {
		const char *arg = av[ix];
		if (!strcmp(arg, "--"))
			no_more_options = 1;
		if (!no_more_options) {
			if (!strncmp(arg, "--remote=", 9)) {
				if (remote)
					die("Multiple --remote specified");
				remote = arg + 9;
				continue;
			}
			if (arg[0] != '-')
				no_more_options = 1;
		}
		if (ix != iy)
			av[iy] = arg;
		iy++;
	}
	if (remote) {
		av[--cnt] = NULL;
		*ac = cnt;
	}
	return remote;
}

int cmd_archive(int argc, const char **argv, const char *prefix)
{
	struct archiver ar;
	int tree_idx;
	const char *remote = NULL;

	remote = extract_remote_arg(&argc, argv);
	if (remote)
		return run_remote_archiver(remote, argc, argv);

	setlinebuf(stderr);

	memset(&ar, 0, sizeof(ar));
	tree_idx = parse_archive_args(argc, argv, &ar);
	if (prefix == NULL)
		prefix = setup_git_directory();

	argv += tree_idx;
	parse_treeish_arg(argv, &ar.args, prefix);
	parse_pathspec_arg(argv + 1, &ar.args);

	return ar.write_archive(&ar.args);
}
