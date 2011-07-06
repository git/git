/*
 * GIT - The information manager from hell
 *
 * Copyright (C) Linus Torvalds, 2005
 * Copyright (C) Junio C Hamano, 2005
 */
#include "cache.h"
#include "blob.h"
#include "quote.h"

static void hash_object(const char *path, enum object_type type, int write_object)
{
	int fd;
	struct stat st;
	unsigned char sha1[20];
	fd = open(path, O_RDONLY);
	if (fd < 0 ||
	    fstat(fd, &st) < 0 ||
	    index_fd(sha1, fd, &st, write_object, type, path))
		die(write_object
		    ? "Unable to add %s to database"
		    : "Unable to hash %s", path);
	printf("%s\n", sha1_to_hex(sha1));
	maybe_flush_or_die(stdout, "hash to stdout");
}

static void hash_stdin(const char *type, int write_object)
{
	unsigned char sha1[20];
	if (index_pipe(sha1, 0, type, write_object))
		die("Unable to add stdin to database");
	printf("%s\n", sha1_to_hex(sha1));
}

static void hash_stdin_paths(const char *type, int write_objects)
{
	struct strbuf buf, nbuf;

	strbuf_init(&buf, 0);
	strbuf_init(&nbuf, 0);
	while (strbuf_getline(&buf, stdin, '\n') != EOF) {
		if (buf.buf[0] == '"') {
			strbuf_reset(&nbuf);
			if (unquote_c_style(&nbuf, buf.buf, NULL))
				die("line is badly quoted");
			strbuf_swap(&buf, &nbuf);
		}
		hash_object(buf.buf, type_from_string(type), write_objects);
	}
	strbuf_release(&buf);
	strbuf_release(&nbuf);
}

static const char hash_object_usage[] =
"git hash-object [ [-t <type>] [-w] [--stdin] <file>... | --stdin-paths < <list-of-paths> ]";

int main(int argc, char **argv)
{
	int i;
	const char *type = blob_type;
	int write_object = 0;
	const char *prefix = NULL;
	int prefix_length = -1;
	int no_more_flags = 0;
	int hashstdin = 0;
	int stdin_paths = 0;

	git_config(git_default_config, NULL);

	for (i = 1 ; i < argc; i++) {
		if (!no_more_flags && argv[i][0] == '-') {
			if (!strcmp(argv[i], "-t")) {
				if (argc <= ++i)
					usage(hash_object_usage);
				type = argv[i];
			}
			else if (!strcmp(argv[i], "-w")) {
				if (prefix_length < 0) {
					prefix = setup_git_directory();
					prefix_length =
						prefix ? strlen(prefix) : 0;
				}
				write_object = 1;
			}
			else if (!strcmp(argv[i], "--")) {
				no_more_flags = 1;
			}
			else if (!strcmp(argv[i], "--help"))
				usage(hash_object_usage);
			else if (!strcmp(argv[i], "--stdin-paths")) {
				if (hashstdin) {
					error("Can't use --stdin-paths with --stdin");
					usage(hash_object_usage);
				}
				stdin_paths = 1;

			}
			else if (!strcmp(argv[i], "--stdin")) {
				if (stdin_paths) {
					error("Can't use %s with --stdin-paths", argv[i]);
					usage(hash_object_usage);
				}
				if (hashstdin)
					die("Multiple --stdin arguments are not supported");
				hashstdin = 1;
			}
			else
				usage(hash_object_usage);
		}
		else {
			const char *arg = argv[i];

			if (stdin_paths) {
				error("Can't specify files (such as \"%s\") with --stdin-paths", arg);
				usage(hash_object_usage);
			}

			if (hashstdin) {
				hash_stdin(type, write_object);
				hashstdin = 0;
			}
			if (0 <= prefix_length)
				arg = prefix_filename(prefix, prefix_length,
						      arg);
			hash_object(arg, type_from_string(type), write_object);
			no_more_flags = 1;
		}
	}

	if (stdin_paths)
		hash_stdin_paths(type, write_object);

	if (hashstdin)
		hash_stdin(type, write_object);
	return 0;
}
