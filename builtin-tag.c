/*
 * Builtin "git tag"
 *
 * Copyright (c) 2007 Kristian Høgsberg <krh@redhat.com>,
 *                    Carlos Rica <jasampler@gmail.com>
 * Based on git-tag.sh and mktag.c by Linus Torvalds.
 */

#include "cache.h"
#include "builtin.h"
#include "refs.h"
#include "tag.h"
#include "run-command.h"

static const char builtin_tag_usage[] =
  "git-tag [-n [<num>]] -l [<pattern>] | [-a | -s | -u <key-id>] [-f | -d | -v] [-m <msg> | -F <file>] <tagname> [<head>]";

static char signingkey[1000];

static void launch_editor(const char *path, char **buffer, unsigned long *len)
{
	const char *editor, *terminal;
	struct child_process child;
	const char *args[3];
	int fd;

	editor = getenv("GIT_EDITOR");
	if (!editor && editor_program)
		editor = editor_program;
	if (!editor)
		editor = getenv("VISUAL");
	if (!editor)
		editor = getenv("EDITOR");

	terminal = getenv("TERM");
	if (!editor && (!terminal || !strcmp(terminal, "dumb"))) {
		fprintf(stderr,
		"Terminal is dumb but no VISUAL nor EDITOR defined.\n"
		"Please supply the message using either -m or -F option.\n");
		exit(1);
	}

	if (!editor)
		editor = "vi";

	memset(&child, 0, sizeof(child));
	child.argv = args;
	args[0] = editor;
	args[1] = path;
	args[2] = NULL;

	if (run_command(&child))
		die("There was a problem with the editor %s.", editor);

	fd = open(path, O_RDONLY);
	if (fd < 0)
		die("could not open '%s': %s", path, strerror(errno));
	if (read_fd(fd, buffer, len)) {
		free(*buffer);
		die("could not read message file '%s': %s",
						path, strerror(errno));
	}
	close(fd);
}

struct tag_filter {
	const char *pattern;
	int lines;
};

#define PGP_SIGNATURE "-----BEGIN PGP SIGNATURE-----"

static int show_reference(const char *refname, const unsigned char *sha1,
			  int flag, void *cb_data)
{
	struct tag_filter *filter = cb_data;

	if (!fnmatch(filter->pattern, refname, 0)) {
		int i;
		unsigned long size;
		enum object_type type;
		char *buf, *sp, *eol;
		size_t len;

		if (!filter->lines) {
			printf("%s\n", refname);
			return 0;
		}
		printf("%-15s ", refname);

		sp = buf = read_sha1_file(sha1, &type, &size);
		if (!buf)
			return 0;
		if (!size) {
			free(buf);
			return 0;
		}
		/* skip header */
		while (sp + 1 < buf + size &&
				!(sp[0] == '\n' && sp[1] == '\n'))
			sp++;
		/* only take up to "lines" lines, and strip the signature */
		for (i = 0, sp += 2;
				i < filter->lines && sp < buf + size &&
				prefixcmp(sp, PGP_SIGNATURE "\n");
				i++) {
			if (i)
				printf("\n    ");
			eol = memchr(sp, '\n', size - (sp - buf));
			len = eol ? eol - sp : size - (sp - buf);
			fwrite(sp, len, 1, stdout);
			if (!eol)
				break;
			sp = eol + 1;
		}
		putchar('\n');
		free(buf);
	}

	return 0;
}

static int list_tags(const char *pattern, int lines)
{
	struct tag_filter filter;

	if (pattern == NULL)
		pattern = "*";

	filter.pattern = pattern;
	filter.lines = lines;

	for_each_tag_ref(show_reference, (void *) &filter);

	return 0;
}

typedef int (*each_tag_name_fn)(const char *name, const char *ref,
				const unsigned char *sha1);

static int for_each_tag_name(const char **argv, each_tag_name_fn fn)
{
	const char **p;
	char ref[PATH_MAX];
	int had_error = 0;
	unsigned char sha1[20];

	for (p = argv; *p; p++) {
		if (snprintf(ref, sizeof(ref), "refs/tags/%s", *p)
					>= sizeof(ref)) {
			error("tag name too long: %.*s...", 50, *p);
			had_error = 1;
			continue;
		}
		if (!resolve_ref(ref, sha1, 1, NULL)) {
			error("tag '%s' not found.", *p);
			had_error = 1;
			continue;
		}
		if (fn(*p, ref, sha1))
			had_error = 1;
	}
	return had_error;
}

static int delete_tag(const char *name, const char *ref,
				const unsigned char *sha1)
{
	if (delete_ref(ref, sha1))
		return 1;
	printf("Deleted tag '%s'\n", name);
	return 0;
}

static int verify_tag(const char *name, const char *ref,
				const unsigned char *sha1)
{
	const char *argv_verify_tag[] = {"git-verify-tag",
					"-v", "SHA1_HEX", NULL};
	argv_verify_tag[2] = sha1_to_hex(sha1);

	if (run_command_v_opt(argv_verify_tag, 0))
		return error("could not verify the tag '%s'", name);
	return 0;
}

static ssize_t do_sign(char *buffer, size_t size, size_t max)
{
	struct child_process gpg;
	const char *args[4];
	char *bracket;
	int len;

	if (!*signingkey) {
		if (strlcpy(signingkey, git_committer_info(1),
				sizeof(signingkey)) > sizeof(signingkey) - 1)
			return error("committer info too long.");
		bracket = strchr(signingkey, '>');
		if (bracket)
			bracket[1] = '\0';
	}

	memset(&gpg, 0, sizeof(gpg));
	gpg.argv = args;
	gpg.in = -1;
	gpg.out = -1;
	args[0] = "gpg";
	args[1] = "-bsau";
	args[2] = signingkey;
	args[3] = NULL;

	if (start_command(&gpg))
		return error("could not run gpg.");

	write_or_die(gpg.in, buffer, size);
	close(gpg.in);
	gpg.close_in = 0;
	len = read_in_full(gpg.out, buffer + size, max - size);

	finish_command(&gpg);

	if (len == max - size)
		return error("could not read the entire signature from gpg.");

	return size + len;
}

static const char tag_template[] =
	"\n"
	"#\n"
	"# Write a tag message\n"
	"#\n";

static int git_tag_config(const char *var, const char *value)
{
	if (!strcmp(var, "user.signingkey")) {
		if (!value)
			die("user.signingkey without value");
		if (strlcpy(signingkey, value, sizeof(signingkey))
						>= sizeof(signingkey))
			die("user.signingkey value too long");
		return 0;
	}

	return git_default_config(var, value);
}

#define MAX_SIGNATURE_LENGTH 1024
/* message must be NULL or allocated, it will be reallocated and freed */
static void create_tag(const unsigned char *object, const char *tag,
		       char *message, int sign, unsigned char *result)
{
	enum object_type type;
	char header_buf[1024], *buffer = NULL;
	int header_len, max_size;
	unsigned long size = 0;

	type = sha1_object_info(object, NULL);
	if (type <= OBJ_NONE)
	    die("bad object type.");

	header_len = snprintf(header_buf, sizeof(header_buf),
			  "object %s\n"
			  "type %s\n"
			  "tag %s\n"
			  "tagger %s\n\n",
			  sha1_to_hex(object),
			  typename(type),
			  tag,
			  git_committer_info(1));

	if (header_len > sizeof(header_buf) - 1)
		die("tag header too big.");

	if (!message) {
		char *path;
		int fd;

		/* write the template message before editing: */
		path = xstrdup(git_path("TAG_EDITMSG"));
		fd = open(path, O_CREAT | O_TRUNC | O_WRONLY, 0600);
		if (fd < 0)
			die("could not create file '%s': %s",
						path, strerror(errno));
		write_or_die(fd, tag_template, strlen(tag_template));
		close(fd);

		launch_editor(path, &buffer, &size);

		unlink(path);
		free(path);
	}
	else {
		buffer = message;
		size = strlen(message);
	}

	size = stripspace(buffer, size, 1);

	if (!message && !size)
		die("no tag message?");

	/* insert the header and add the '\n' if needed: */
	max_size = header_len + size + (sign ? MAX_SIGNATURE_LENGTH : 0) + 1;
	buffer = xrealloc(buffer, max_size);
	if (size)
		buffer[size++] = '\n';
	memmove(buffer + header_len, buffer, size);
	memcpy(buffer, header_buf, header_len);
	size += header_len;

	if (sign) {
		size = do_sign(buffer, size, max_size);
		if (size < 0)
			die("unable to sign the tag");
	}

	if (write_sha1_file(buffer, size, tag_type, result) < 0)
		die("unable to write tag file");
	free(buffer);
}

int cmd_tag(int argc, const char **argv, const char *prefix)
{
	unsigned char object[20], prev[20];
	int annotate = 0, sign = 0, force = 0, lines = 0;
	char *message = NULL;
	char ref[PATH_MAX];
	const char *object_ref, *tag;
	int i;
	struct ref_lock *lock;

	git_config(git_tag_config);

	for (i = 1; i < argc; i++) {
		const char *arg = argv[i];

		if (arg[0] != '-')
			break;
		if (!strcmp(arg, "-a")) {
			annotate = 1;
			continue;
		}
		if (!strcmp(arg, "-s")) {
			annotate = 1;
			sign = 1;
			continue;
		}
		if (!strcmp(arg, "-f")) {
			force = 1;
			continue;
		}
		if (!strcmp(arg, "-n")) {
			if (i + 1 == argc || *argv[i + 1] == '-')
				/* no argument */
				lines = 1;
			else
				lines = isdigit(*argv[++i]) ?
					atoi(argv[i]) : 1;
			continue;
		}
		if (!strcmp(arg, "-m")) {
			annotate = 1;
			i++;
			if (i == argc)
				die("option -m needs an argument.");
			if (message)
				die("only one -F or -m option is allowed.");
			message = xstrdup(argv[i]);
			continue;
		}
		if (!strcmp(arg, "-F")) {
			unsigned long len;
			int fd;

			annotate = 1;
			i++;
			if (i == argc)
				die("option -F needs an argument.");
			if (message)
				die("only one -F or -m option is allowed.");

			if (!strcmp(argv[i], "-"))
				fd = 0;
			else {
				fd = open(argv[i], O_RDONLY);
				if (fd < 0)
					die("could not open '%s': %s",
						argv[i], strerror(errno));
			}
			len = 1024;
			message = xmalloc(len);
			if (read_fd(fd, &message, &len)) {
				free(message);
				die("cannot read %s", argv[i]);
			}
			continue;
		}
		if (!strcmp(arg, "-u")) {
			annotate = 1;
			sign = 1;
			i++;
			if (i == argc)
				die("option -u needs an argument.");
			if (strlcpy(signingkey, argv[i], sizeof(signingkey))
							>= sizeof(signingkey))
				die("argument to option -u too long");
			continue;
		}
		if (!strcmp(arg, "-l"))
			return list_tags(argv[i + 1], lines);
		if (!strcmp(arg, "-d"))
			return for_each_tag_name(argv + i + 1, delete_tag);
		if (!strcmp(arg, "-v"))
			return for_each_tag_name(argv + i + 1, verify_tag);
		usage(builtin_tag_usage);
	}

	if (i == argc) {
		if (annotate)
			usage(builtin_tag_usage);
		return list_tags(NULL, lines);
	}
	tag = argv[i++];

	object_ref = i < argc ? argv[i] : "HEAD";
	if (i + 1 < argc)
		die("too many params");

	if (get_sha1(object_ref, object))
		die("Failed to resolve '%s' as a valid ref.", object_ref);

	if (snprintf(ref, sizeof(ref), "refs/tags/%s", tag) > sizeof(ref) - 1)
		die("tag name too long: %.*s...", 50, tag);
	if (check_ref_format(ref))
		die("'%s' is not a valid tag name.", tag);

	if (!resolve_ref(ref, prev, 1, NULL))
		hashclr(prev);
	else if (!force)
		die("tag '%s' already exists", tag);

	if (annotate)
		create_tag(object, tag, message, sign, object);

	lock = lock_any_ref_for_update(ref, prev, 0);
	if (!lock)
		die("%s: cannot lock the ref", ref);
	if (write_ref_sha1(lock, object, NULL) < 0)
		die("%s: cannot update the ref", ref);

	return 0;
}
