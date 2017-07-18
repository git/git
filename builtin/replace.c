/*
 * Builtin "git replace"
 *
 * Copyright (c) 2008 Christian Couder <chriscool@tuxfamily.org>
 *
 * Based on builtin/tag.c by Kristian Høgsberg <krh@redhat.com>
 * and Carlos Rica <jasampler@gmail.com> that was itself based on
 * git-tag.sh and mktag.c by Linus Torvalds.
 */

#include "cache.h"
#include "builtin.h"
#include "refs.h"
#include "parse-options.h"
#include "run-command.h"
#include "tag.h"

static const char * const git_replace_usage[] = {
	N_("git replace [-f] <object> <replacement>"),
	N_("git replace [-f] --edit <object>"),
	N_("git replace [-f] --graft <commit> [<parent>...]"),
	N_("git replace -d <object>..."),
	N_("git replace [--format=<format>] [-l [<pattern>]]"),
	NULL
};

enum replace_format {
	REPLACE_FORMAT_SHORT,
	REPLACE_FORMAT_MEDIUM,
	REPLACE_FORMAT_LONG
};

struct show_data {
	const char *pattern;
	enum replace_format format;
};

static int show_reference(const char *refname, const struct object_id *oid,
			  int flag, void *cb_data)
{
	struct show_data *data = cb_data;

	if (!wildmatch(data->pattern, refname, 0, NULL)) {
		if (data->format == REPLACE_FORMAT_SHORT)
			printf("%s\n", refname);
		else if (data->format == REPLACE_FORMAT_MEDIUM)
			printf("%s -> %s\n", refname, oid_to_hex(oid));
		else { /* data->format == REPLACE_FORMAT_LONG */
			struct object_id object;
			enum object_type obj_type, repl_type;

			if (get_sha1(refname, object.hash))
				return error("Failed to resolve '%s' as a valid ref.", refname);

			obj_type = sha1_object_info(object.hash, NULL);
			repl_type = sha1_object_info(oid->hash, NULL);

			printf("%s (%s) -> %s (%s)\n", refname, typename(obj_type),
			       oid_to_hex(oid), typename(repl_type));
		}
	}

	return 0;
}

static int list_replace_refs(const char *pattern, const char *format)
{
	struct show_data data;

	if (pattern == NULL)
		pattern = "*";
	data.pattern = pattern;

	if (format == NULL || *format == '\0' || !strcmp(format, "short"))
		data.format = REPLACE_FORMAT_SHORT;
	else if (!strcmp(format, "medium"))
		data.format = REPLACE_FORMAT_MEDIUM;
	else if (!strcmp(format, "long"))
		data.format = REPLACE_FORMAT_LONG;
	else
		die("invalid replace format '%s'\n"
		    "valid formats are 'short', 'medium' and 'long'\n",
		    format);

	for_each_replace_ref(show_reference, (void *)&data);

	return 0;
}

typedef int (*each_replace_name_fn)(const char *name, const char *ref,
				    const unsigned char *sha1);

static int for_each_replace_name(const char **argv, each_replace_name_fn fn)
{
	const char **p, *full_hex;
	char ref[PATH_MAX];
	int had_error = 0;
	unsigned char sha1[20];

	for (p = argv; *p; p++) {
		if (get_sha1(*p, sha1)) {
			error("Failed to resolve '%s' as a valid ref.", *p);
			had_error = 1;
			continue;
		}
		full_hex = sha1_to_hex(sha1);
		snprintf(ref, sizeof(ref), "%s%s", git_replace_ref_base, full_hex);
		/* read_ref() may reuse the buffer */
		full_hex = ref + strlen(git_replace_ref_base);
		if (read_ref(ref, sha1)) {
			error("replace ref '%s' not found.", full_hex);
			had_error = 1;
			continue;
		}
		if (fn(full_hex, ref, sha1))
			had_error = 1;
	}
	return had_error;
}

static int delete_replace_ref(const char *name, const char *ref,
			      const unsigned char *sha1)
{
	if (delete_ref(ref, sha1, 0))
		return 1;
	printf("Deleted replace ref '%s'\n", name);
	return 0;
}

static void check_ref_valid(unsigned char object[20],
			    unsigned char prev[20],
			    char *ref,
			    int ref_size,
			    int force)
{
	if (snprintf(ref, ref_size,
		     "%s%s", git_replace_ref_base,
		     sha1_to_hex(object)) > ref_size - 1)
		die("replace ref name too long: %.*s...", 50, ref);
	if (check_refname_format(ref, 0))
		die("'%s' is not a valid ref name.", ref);

	if (read_ref(ref, prev))
		hashclr(prev);
	else if (!force)
		die("replace ref '%s' already exists", ref);
}

static int replace_object_sha1(const char *object_ref,
			       unsigned char object[20],
			       const char *replace_ref,
			       unsigned char repl[20],
			       int force)
{
	unsigned char prev[20];
	enum object_type obj_type, repl_type;
	char ref[PATH_MAX];
	struct ref_transaction *transaction;
	struct strbuf err = STRBUF_INIT;

	obj_type = sha1_object_info(object, NULL);
	repl_type = sha1_object_info(repl, NULL);
	if (!force && obj_type != repl_type)
		die("Objects must be of the same type.\n"
		    "'%s' points to a replaced object of type '%s'\n"
		    "while '%s' points to a replacement object of type '%s'.",
		    object_ref, typename(obj_type),
		    replace_ref, typename(repl_type));

	check_ref_valid(object, prev, ref, sizeof(ref), force);

	transaction = ref_transaction_begin(&err);
	if (!transaction ||
	    ref_transaction_update(transaction, ref, repl, prev,
				   0, NULL, &err) ||
	    ref_transaction_commit(transaction, &err))
		die("%s", err.buf);

	ref_transaction_free(transaction);
	return 0;
}

static int replace_object(const char *object_ref, const char *replace_ref, int force)
{
	unsigned char object[20], repl[20];

	if (get_sha1(object_ref, object))
		die("Failed to resolve '%s' as a valid ref.", object_ref);
	if (get_sha1(replace_ref, repl))
		die("Failed to resolve '%s' as a valid ref.", replace_ref);

	return replace_object_sha1(object_ref, object, replace_ref, repl, force);
}

/*
 * Write the contents of the object named by "sha1" to the file "filename".
 * If "raw" is true, then the object's raw contents are printed according to
 * "type". Otherwise, we pretty-print the contents for human editing.
 */
static void export_object(const unsigned char *sha1, enum object_type type,
			  int raw, const char *filename)
{
	struct child_process cmd = CHILD_PROCESS_INIT;
	int fd;

	fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0666);
	if (fd < 0)
		die_errno("unable to open %s for writing", filename);

	argv_array_push(&cmd.args, "--no-replace-objects");
	argv_array_push(&cmd.args, "cat-file");
	if (raw)
		argv_array_push(&cmd.args, typename(type));
	else
		argv_array_push(&cmd.args, "-p");
	argv_array_push(&cmd.args, sha1_to_hex(sha1));
	cmd.git_cmd = 1;
	cmd.out = fd;

	if (run_command(&cmd))
		die("cat-file reported failure");
}

/*
 * Read a previously-exported (and possibly edited) object back from "filename",
 * interpreting it as "type", and writing the result to the object database.
 * The sha1 of the written object is returned via sha1.
 */
static void import_object(unsigned char *sha1, enum object_type type,
			  int raw, const char *filename)
{
	int fd;

	fd = open(filename, O_RDONLY);
	if (fd < 0)
		die_errno("unable to open %s for reading", filename);

	if (!raw && type == OBJ_TREE) {
		const char *argv[] = { "mktree", NULL };
		struct child_process cmd = CHILD_PROCESS_INIT;
		struct strbuf result = STRBUF_INIT;

		cmd.argv = argv;
		cmd.git_cmd = 1;
		cmd.in = fd;
		cmd.out = -1;

		if (start_command(&cmd))
			die("unable to spawn mktree");

		if (strbuf_read(&result, cmd.out, 41) < 0)
			die_errno("unable to read from mktree");
		close(cmd.out);

		if (finish_command(&cmd))
			die("mktree reported failure");
		if (get_sha1_hex(result.buf, sha1) < 0)
			die("mktree did not return an object name");

		strbuf_release(&result);
	} else {
		struct stat st;
		int flags = HASH_FORMAT_CHECK | HASH_WRITE_OBJECT;

		if (fstat(fd, &st) < 0)
			die_errno("unable to fstat %s", filename);
		if (index_fd(sha1, fd, &st, type, NULL, flags) < 0)
			die("unable to write object to database");
		/* index_fd close()s fd for us */
	}

	/*
	 * No need to close(fd) here; both run-command and index-fd
	 * will have done it for us.
	 */
}

static int edit_and_replace(const char *object_ref, int force, int raw)
{
	char *tmpfile = git_pathdup("REPLACE_EDITOBJ");
	enum object_type type;
	unsigned char old[20], new[20], prev[20];
	char ref[PATH_MAX];

	if (get_sha1(object_ref, old) < 0)
		die("Not a valid object name: '%s'", object_ref);

	type = sha1_object_info(old, NULL);
	if (type < 0)
		die("unable to get object type for %s", sha1_to_hex(old));

	check_ref_valid(old, prev, ref, sizeof(ref), force);

	export_object(old, type, raw, tmpfile);
	if (launch_editor(tmpfile, NULL, NULL) < 0)
		die("editing object file failed");
	import_object(new, type, raw, tmpfile);

	free(tmpfile);

	if (!hashcmp(old, new))
		return error("new object is the same as the old one: '%s'", sha1_to_hex(old));

	return replace_object_sha1(object_ref, old, "replacement", new, force);
}

static void replace_parents(struct strbuf *buf, int argc, const char **argv)
{
	struct strbuf new_parents = STRBUF_INIT;
	const char *parent_start, *parent_end;
	int i;

	/* find existing parents */
	parent_start = buf->buf;
	parent_start += 46; /* "tree " + "hex sha1" + "\n" */
	parent_end = parent_start;

	while (starts_with(parent_end, "parent "))
		parent_end += 48; /* "parent " + "hex sha1" + "\n" */

	/* prepare new parents */
	for (i = 0; i < argc; i++) {
		unsigned char sha1[20];
		if (get_sha1(argv[i], sha1) < 0)
			die(_("Not a valid object name: '%s'"), argv[i]);
		lookup_commit_or_die(sha1, argv[i]);
		strbuf_addf(&new_parents, "parent %s\n", sha1_to_hex(sha1));
	}

	/* replace existing parents with new ones */
	strbuf_splice(buf, parent_start - buf->buf, parent_end - parent_start,
		      new_parents.buf, new_parents.len);

	strbuf_release(&new_parents);
}

struct check_mergetag_data {
	int argc;
	const char **argv;
};

static void check_one_mergetag(struct commit *commit,
			       struct commit_extra_header *extra,
			       void *data)
{
	struct check_mergetag_data *mergetag_data = (struct check_mergetag_data *)data;
	const char *ref = mergetag_data->argv[0];
	unsigned char tag_sha1[20];
	struct tag *tag;
	int i;

	hash_sha1_file(extra->value, extra->len, typename(OBJ_TAG), tag_sha1);
	tag = lookup_tag(tag_sha1);
	if (!tag)
		die(_("bad mergetag in commit '%s'"), ref);
	if (parse_tag_buffer(tag, extra->value, extra->len))
		die(_("malformed mergetag in commit '%s'"), ref);

	/* iterate over new parents */
	for (i = 1; i < mergetag_data->argc; i++) {
		struct object_id oid;
		if (get_sha1(mergetag_data->argv[i], oid.hash) < 0)
			die(_("Not a valid object name: '%s'"), mergetag_data->argv[i]);
		if (!oidcmp(&tag->tagged->oid, &oid))
			return; /* found */
	}

	die(_("original commit '%s' contains mergetag '%s' that is discarded; "
	      "use --edit instead of --graft"), ref, sha1_to_hex(tag_sha1));
}

static void check_mergetags(struct commit *commit, int argc, const char **argv)
{
	struct check_mergetag_data mergetag_data;

	mergetag_data.argc = argc;
	mergetag_data.argv = argv;
	for_each_mergetag(check_one_mergetag, commit, &mergetag_data);
}

static int create_graft(int argc, const char **argv, int force)
{
	unsigned char old[20], new[20];
	const char *old_ref = argv[0];
	struct commit *commit;
	struct strbuf buf = STRBUF_INIT;
	const char *buffer;
	unsigned long size;

	if (get_sha1(old_ref, old) < 0)
		die(_("Not a valid object name: '%s'"), old_ref);
	commit = lookup_commit_or_die(old, old_ref);

	buffer = get_commit_buffer(commit, &size);
	strbuf_add(&buf, buffer, size);
	unuse_commit_buffer(commit, buffer);

	replace_parents(&buf, argc - 1, &argv[1]);

	if (remove_signature(&buf)) {
		warning(_("the original commit '%s' has a gpg signature."), old_ref);
		warning(_("the signature will be removed in the replacement commit!"));
	}

	check_mergetags(commit, argc, argv);

	if (write_sha1_file(buf.buf, buf.len, commit_type, new))
		die(_("could not write replacement commit for: '%s'"), old_ref);

	strbuf_release(&buf);

	if (!hashcmp(old, new))
		return error("new commit is the same as the old one: '%s'", sha1_to_hex(old));

	return replace_object_sha1(old_ref, old, "replacement", new, force);
}

int cmd_replace(int argc, const char **argv, const char *prefix)
{
	int force = 0;
	int raw = 0;
	const char *format = NULL;
	enum {
		MODE_UNSPECIFIED = 0,
		MODE_LIST,
		MODE_DELETE,
		MODE_EDIT,
		MODE_GRAFT,
		MODE_REPLACE
	} cmdmode = MODE_UNSPECIFIED;
	struct option options[] = {
		OPT_CMDMODE('l', "list", &cmdmode, N_("list replace refs"), MODE_LIST),
		OPT_CMDMODE('d', "delete", &cmdmode, N_("delete replace refs"), MODE_DELETE),
		OPT_CMDMODE('e', "edit", &cmdmode, N_("edit existing object"), MODE_EDIT),
		OPT_CMDMODE('g', "graft", &cmdmode, N_("change a commit's parents"), MODE_GRAFT),
		OPT_BOOL('f', "force", &force, N_("replace the ref if it exists")),
		OPT_BOOL(0, "raw", &raw, N_("do not pretty-print contents for --edit")),
		OPT_STRING(0, "format", &format, N_("format"), N_("use this format")),
		OPT_END()
	};

	check_replace_refs = 0;
	git_config(git_default_config, NULL);

	argc = parse_options(argc, argv, prefix, options, git_replace_usage, 0);

	if (!cmdmode)
		cmdmode = argc ? MODE_REPLACE : MODE_LIST;

	if (format && cmdmode != MODE_LIST)
		usage_msg_opt("--format cannot be used when not listing",
			      git_replace_usage, options);

	if (force &&
	    cmdmode != MODE_REPLACE &&
	    cmdmode != MODE_EDIT &&
	    cmdmode != MODE_GRAFT)
		usage_msg_opt("-f only makes sense when writing a replacement",
			      git_replace_usage, options);

	if (raw && cmdmode != MODE_EDIT)
		usage_msg_opt("--raw only makes sense with --edit",
			      git_replace_usage, options);

	switch (cmdmode) {
	case MODE_DELETE:
		if (argc < 1)
			usage_msg_opt("-d needs at least one argument",
				      git_replace_usage, options);
		return for_each_replace_name(argv, delete_replace_ref);

	case MODE_REPLACE:
		if (argc != 2)
			usage_msg_opt("bad number of arguments",
				      git_replace_usage, options);
		return replace_object(argv[0], argv[1], force);

	case MODE_EDIT:
		if (argc != 1)
			usage_msg_opt("-e needs exactly one argument",
				      git_replace_usage, options);
		return edit_and_replace(argv[0], force, raw);

	case MODE_GRAFT:
		if (argc < 1)
			usage_msg_opt("-g needs at least one argument",
				      git_replace_usage, options);
		return create_graft(argc, argv, force);

	case MODE_LIST:
		if (argc > 1)
			usage_msg_opt("only one pattern can be given with -l",
				      git_replace_usage, options);
		return list_replace_refs(argv[0], format);

	default:
		die("BUG: invalid cmdmode %d", (int)cmdmode);
	}
}
