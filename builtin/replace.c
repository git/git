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
#include "config.h"
#include "builtin.h"
#include "refs.h"
#include "parse-options.h"
#include "run-command.h"
#include "object-store.h"
#include "repository.h"
#include "tag.h"

static const char * const git_replace_usage[] = {
	N_("git replace [-f] <object> <replacement>"),
	N_("git replace [-f] --edit <object>"),
	N_("git replace [-f] --graft <commit> [<parent>...]"),
	N_("git replace [-f] --convert-graft-file"),
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

static int show_reference(struct repository *r, const char *refname,
			  const struct object_id *oid,
			  int flag, void *cb_data)
{
	struct show_data *data = cb_data;

	if (!wildmatch(data->pattern, refname, 0)) {
		if (data->format == REPLACE_FORMAT_SHORT)
			printf("%s\n", refname);
		else if (data->format == REPLACE_FORMAT_MEDIUM)
			printf("%s -> %s\n", refname, oid_to_hex(oid));
		else { /* data->format == REPLACE_FORMAT_LONG */
			struct object_id object;
			enum object_type obj_type, repl_type;

			if (get_oid(refname, &object))
				return error(_("failed to resolve '%s' as a valid ref"), refname);

			obj_type = oid_object_info(r, &object, NULL);
			repl_type = oid_object_info(r, oid, NULL);

			printf("%s (%s) -> %s (%s)\n", refname, type_name(obj_type),
			       oid_to_hex(oid), type_name(repl_type));
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
		return error(_("invalid replace format '%s'\n"
			       "valid formats are 'short', 'medium' and 'long'"),
			     format);

	for_each_replace_ref(the_repository, show_reference, (void *)&data);

	return 0;
}

typedef int (*each_replace_name_fn)(const char *name, const char *ref,
				    const struct object_id *oid);

static int for_each_replace_name(const char **argv, each_replace_name_fn fn)
{
	const char **p, *full_hex;
	struct strbuf ref = STRBUF_INIT;
	size_t base_len;
	int had_error = 0;
	struct object_id oid;

	strbuf_addstr(&ref, git_replace_ref_base);
	base_len = ref.len;

	for (p = argv; *p; p++) {
		if (get_oid(*p, &oid)) {
			error("failed to resolve '%s' as a valid ref", *p);
			had_error = 1;
			continue;
		}

		strbuf_setlen(&ref, base_len);
		strbuf_addstr(&ref, oid_to_hex(&oid));
		full_hex = ref.buf + base_len;

		if (read_ref(ref.buf, &oid)) {
			error(_("replace ref '%s' not found"), full_hex);
			had_error = 1;
			continue;
		}
		if (fn(full_hex, ref.buf, &oid))
			had_error = 1;
	}
	strbuf_release(&ref);
	return had_error;
}

static int delete_replace_ref(const char *name, const char *ref,
			      const struct object_id *oid)
{
	if (delete_ref(NULL, ref, oid, 0))
		return 1;
	printf_ln(_("Deleted replace ref '%s'"), name);
	return 0;
}

static int check_ref_valid(struct object_id *object,
			    struct object_id *prev,
			    struct strbuf *ref,
			    int force)
{
	strbuf_reset(ref);
	strbuf_addf(ref, "%s%s", git_replace_ref_base, oid_to_hex(object));
	if (check_refname_format(ref->buf, 0))
		return error(_("'%s' is not a valid ref name"), ref->buf);

	if (read_ref(ref->buf, prev))
		oidclr(prev);
	else if (!force)
		return error(_("replace ref '%s' already exists"), ref->buf);
	return 0;
}

static int replace_object_oid(const char *object_ref,
			       struct object_id *object,
			       const char *replace_ref,
			       struct object_id *repl,
			       int force)
{
	struct object_id prev;
	enum object_type obj_type, repl_type;
	struct strbuf ref = STRBUF_INIT;
	struct ref_transaction *transaction;
	struct strbuf err = STRBUF_INIT;
	int res = 0;

	obj_type = oid_object_info(the_repository, object, NULL);
	repl_type = oid_object_info(the_repository, repl, NULL);
	if (!force && obj_type != repl_type)
		return error(_("Objects must be of the same type.\n"
			       "'%s' points to a replaced object of type '%s'\n"
			       "while '%s' points to a replacement object of "
			       "type '%s'."),
			     object_ref, type_name(obj_type),
			     replace_ref, type_name(repl_type));

	if (check_ref_valid(object, &prev, &ref, force)) {
		strbuf_release(&ref);
		return -1;
	}

	transaction = ref_transaction_begin(&err);
	if (!transaction ||
	    ref_transaction_update(transaction, ref.buf, repl, &prev,
				   0, NULL, &err) ||
	    ref_transaction_commit(transaction, &err))
		res = error("%s", err.buf);

	ref_transaction_free(transaction);
	strbuf_release(&ref);
	return res;
}

static int replace_object(const char *object_ref, const char *replace_ref, int force)
{
	struct object_id object, repl;

	if (get_oid(object_ref, &object))
		return error(_("failed to resolve '%s' as a valid ref"),
			     object_ref);
	if (get_oid(replace_ref, &repl))
		return error(_("failed to resolve '%s' as a valid ref"),
			     replace_ref);

	return replace_object_oid(object_ref, &object, replace_ref, &repl, force);
}

/*
 * Write the contents of the object named by "sha1" to the file "filename".
 * If "raw" is true, then the object's raw contents are printed according to
 * "type". Otherwise, we pretty-print the contents for human editing.
 */
static int export_object(const struct object_id *oid, enum object_type type,
			  int raw, const char *filename)
{
	struct child_process cmd = CHILD_PROCESS_INIT;
	int fd;

	fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0666);
	if (fd < 0)
		return error_errno(_("unable to open %s for writing"), filename);

	argv_array_push(&cmd.args, "--no-replace-objects");
	argv_array_push(&cmd.args, "cat-file");
	if (raw)
		argv_array_push(&cmd.args, type_name(type));
	else
		argv_array_push(&cmd.args, "-p");
	argv_array_push(&cmd.args, oid_to_hex(oid));
	cmd.git_cmd = 1;
	cmd.out = fd;

	if (run_command(&cmd))
		return error(_("cat-file reported failure"));
	return 0;
}

/*
 * Read a previously-exported (and possibly edited) object back from "filename",
 * interpreting it as "type", and writing the result to the object database.
 * The sha1 of the written object is returned via sha1.
 */
static int import_object(struct object_id *oid, enum object_type type,
			  int raw, const char *filename)
{
	int fd;

	fd = open(filename, O_RDONLY);
	if (fd < 0)
		return error_errno(_("unable to open %s for reading"), filename);

	if (!raw && type == OBJ_TREE) {
		const char *argv[] = { "mktree", NULL };
		struct child_process cmd = CHILD_PROCESS_INIT;
		struct strbuf result = STRBUF_INIT;

		cmd.argv = argv;
		cmd.git_cmd = 1;
		cmd.in = fd;
		cmd.out = -1;

		if (start_command(&cmd)) {
			close(fd);
			return error(_("unable to spawn mktree"));
		}

		if (strbuf_read(&result, cmd.out, 41) < 0) {
			error_errno(_("unable to read from mktree"));
			close(fd);
			close(cmd.out);
			return -1;
		}
		close(cmd.out);

		if (finish_command(&cmd)) {
			strbuf_release(&result);
			return error(_("mktree reported failure"));
		}
		if (get_oid_hex(result.buf, oid) < 0) {
			strbuf_release(&result);
			return error(_("mktree did not return an object name"));
		}

		strbuf_release(&result);
	} else {
		struct stat st;
		int flags = HASH_FORMAT_CHECK | HASH_WRITE_OBJECT;

		if (fstat(fd, &st) < 0) {
			error_errno(_("unable to fstat %s"), filename);
			close(fd);
			return -1;
		}
		if (index_fd(oid, fd, &st, type, NULL, flags) < 0)
			return error(_("unable to write object to database"));
		/* index_fd close()s fd for us */
	}

	/*
	 * No need to close(fd) here; both run-command and index-fd
	 * will have done it for us.
	 */
	return 0;
}

static int edit_and_replace(const char *object_ref, int force, int raw)
{
	char *tmpfile;
	enum object_type type;
	struct object_id old_oid, new_oid, prev;
	struct strbuf ref = STRBUF_INIT;

	if (get_oid(object_ref, &old_oid) < 0)
		return error(_("not a valid object name: '%s'"), object_ref);

	type = oid_object_info(the_repository, &old_oid, NULL);
	if (type < 0)
		return error(_("unable to get object type for %s"),
			     oid_to_hex(&old_oid));

	if (check_ref_valid(&old_oid, &prev, &ref, force)) {
		strbuf_release(&ref);
		return -1;
	}
	strbuf_release(&ref);

	tmpfile = git_pathdup("REPLACE_EDITOBJ");
	if (export_object(&old_oid, type, raw, tmpfile)) {
		free(tmpfile);
		return -1;
	}
	if (launch_editor(tmpfile, NULL, NULL) < 0) {
		free(tmpfile);
		return error(_("editing object file failed"));
	}
	if (import_object(&new_oid, type, raw, tmpfile)) {
		free(tmpfile);
		return -1;
	}
	free(tmpfile);

	if (oideq(&old_oid, &new_oid))
		return error(_("new object is the same as the old one: '%s'"), oid_to_hex(&old_oid));

	return replace_object_oid(object_ref, &old_oid, "replacement", &new_oid, force);
}

static int replace_parents(struct strbuf *buf, int argc, const char **argv)
{
	struct strbuf new_parents = STRBUF_INIT;
	const char *parent_start, *parent_end;
	int i;

	/* find existing parents */
	parent_start = buf->buf;
	parent_start += GIT_SHA1_HEXSZ + 6; /* "tree " + "hex sha1" + "\n" */
	parent_end = parent_start;

	while (starts_with(parent_end, "parent "))
		parent_end += 48; /* "parent " + "hex sha1" + "\n" */

	/* prepare new parents */
	for (i = 0; i < argc; i++) {
		struct object_id oid;
		if (get_oid(argv[i], &oid) < 0) {
			strbuf_release(&new_parents);
			return error(_("not a valid object name: '%s'"),
				     argv[i]);
		}
		if (!lookup_commit_reference(the_repository, &oid)) {
			strbuf_release(&new_parents);
			return error(_("could not parse %s"), argv[i]);
		}
		strbuf_addf(&new_parents, "parent %s\n", oid_to_hex(&oid));
	}

	/* replace existing parents with new ones */
	strbuf_splice(buf, parent_start - buf->buf, parent_end - parent_start,
		      new_parents.buf, new_parents.len);

	strbuf_release(&new_parents);
	return 0;
}

struct check_mergetag_data {
	int argc;
	const char **argv;
};

static int check_one_mergetag(struct commit *commit,
			       struct commit_extra_header *extra,
			       void *data)
{
	struct check_mergetag_data *mergetag_data = (struct check_mergetag_data *)data;
	const char *ref = mergetag_data->argv[0];
	struct object_id tag_oid;
	struct tag *tag;
	int i;

	hash_object_file(extra->value, extra->len, type_name(OBJ_TAG), &tag_oid);
	tag = lookup_tag(the_repository, &tag_oid);
	if (!tag)
		return error(_("bad mergetag in commit '%s'"), ref);
	if (parse_tag_buffer(the_repository, tag, extra->value, extra->len))
		return error(_("malformed mergetag in commit '%s'"), ref);

	/* iterate over new parents */
	for (i = 1; i < mergetag_data->argc; i++) {
		struct object_id oid;
		if (get_oid(mergetag_data->argv[i], &oid) < 0)
			return error(_("not a valid object name: '%s'"),
				     mergetag_data->argv[i]);
		if (oideq(&tag->tagged->oid, &oid))
			return 0; /* found */
	}

	return error(_("original commit '%s' contains mergetag '%s' that is "
		       "discarded; use --edit instead of --graft"), ref,
		     oid_to_hex(&tag_oid));
}

static int check_mergetags(struct commit *commit, int argc, const char **argv)
{
	struct check_mergetag_data mergetag_data;

	mergetag_data.argc = argc;
	mergetag_data.argv = argv;
	return for_each_mergetag(check_one_mergetag, commit, &mergetag_data);
}

static int create_graft(int argc, const char **argv, int force, int gentle)
{
	struct object_id old_oid, new_oid;
	const char *old_ref = argv[0];
	struct commit *commit;
	struct strbuf buf = STRBUF_INIT;
	const char *buffer;
	unsigned long size;

	if (get_oid(old_ref, &old_oid) < 0)
		return error(_("not a valid object name: '%s'"), old_ref);
	commit = lookup_commit_reference(the_repository, &old_oid);
	if (!commit)
		return error(_("could not parse %s"), old_ref);

	buffer = get_commit_buffer(commit, &size);
	strbuf_add(&buf, buffer, size);
	unuse_commit_buffer(commit, buffer);

	if (replace_parents(&buf, argc - 1, &argv[1]) < 0) {
		strbuf_release(&buf);
		return -1;
	}

	if (remove_signature(&buf)) {
		warning(_("the original commit '%s' has a gpg signature"), old_ref);
		warning(_("the signature will be removed in the replacement commit!"));
	}

	if (check_mergetags(commit, argc, argv)) {
		strbuf_release(&buf);
		return -1;
	}

	if (write_object_file(buf.buf, buf.len, commit_type, &new_oid)) {
		strbuf_release(&buf);
		return error(_("could not write replacement commit for: '%s'"),
			     old_ref);
	}

	strbuf_release(&buf);

	if (oideq(&old_oid, &new_oid)) {
		if (gentle) {
			warning(_("graft for '%s' unnecessary"), oid_to_hex(&old_oid));
			return 0;
		}
		return error(_("new commit is the same as the old one: '%s'"), oid_to_hex(&old_oid));
	}

	return replace_object_oid(old_ref, &old_oid, "replacement", &new_oid, force);
}

static int convert_graft_file(int force)
{
	const char *graft_file = get_graft_file(the_repository);
	FILE *fp = fopen_or_warn(graft_file, "r");
	struct strbuf buf = STRBUF_INIT, err = STRBUF_INIT;
	struct argv_array args = ARGV_ARRAY_INIT;

	if (!fp)
		return -1;

	while (strbuf_getline(&buf, fp) != EOF) {
		if (*buf.buf == '#')
			continue;

		argv_array_split(&args, buf.buf);
		if (args.argc && create_graft(args.argc, args.argv, force, 1))
			strbuf_addf(&err, "\n\t%s", buf.buf);
		argv_array_clear(&args);
	}
	fclose(fp);

	strbuf_release(&buf);

	if (!err.len)
		return unlink_or_warn(graft_file);

	warning(_("could not convert the following graft(s):\n%s"), err.buf);
	strbuf_release(&err);

	return -1;
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
		MODE_CONVERT_GRAFT_FILE,
		MODE_REPLACE
	} cmdmode = MODE_UNSPECIFIED;
	struct option options[] = {
		OPT_CMDMODE('l', "list", &cmdmode, N_("list replace refs"), MODE_LIST),
		OPT_CMDMODE('d', "delete", &cmdmode, N_("delete replace refs"), MODE_DELETE),
		OPT_CMDMODE('e', "edit", &cmdmode, N_("edit existing object"), MODE_EDIT),
		OPT_CMDMODE('g', "graft", &cmdmode, N_("change a commit's parents"), MODE_GRAFT),
		OPT_CMDMODE(0, "convert-graft-file", &cmdmode, N_("convert existing graft file"), MODE_CONVERT_GRAFT_FILE),
		OPT_BOOL_F('f', "force", &force, N_("replace the ref if it exists"),
			   PARSE_OPT_NOCOMPLETE),
		OPT_BOOL(0, "raw", &raw, N_("do not pretty-print contents for --edit")),
		OPT_STRING(0, "format", &format, N_("format"), N_("use this format")),
		OPT_END()
	};

	read_replace_refs = 0;
	git_config(git_default_config, NULL);

	argc = parse_options(argc, argv, prefix, options, git_replace_usage, 0);

	if (!cmdmode)
		cmdmode = argc ? MODE_REPLACE : MODE_LIST;

	if (format && cmdmode != MODE_LIST)
		usage_msg_opt(_("--format cannot be used when not listing"),
			      git_replace_usage, options);

	if (force &&
	    cmdmode != MODE_REPLACE &&
	    cmdmode != MODE_EDIT &&
	    cmdmode != MODE_GRAFT &&
	    cmdmode != MODE_CONVERT_GRAFT_FILE)
		usage_msg_opt(_("-f only makes sense when writing a replacement"),
			      git_replace_usage, options);

	if (raw && cmdmode != MODE_EDIT)
		usage_msg_opt(_("--raw only makes sense with --edit"),
			      git_replace_usage, options);

	switch (cmdmode) {
	case MODE_DELETE:
		if (argc < 1)
			usage_msg_opt(_("-d needs at least one argument"),
				      git_replace_usage, options);
		return for_each_replace_name(argv, delete_replace_ref);

	case MODE_REPLACE:
		if (argc != 2)
			usage_msg_opt(_("bad number of arguments"),
				      git_replace_usage, options);
		return replace_object(argv[0], argv[1], force);

	case MODE_EDIT:
		if (argc != 1)
			usage_msg_opt(_("-e needs exactly one argument"),
				      git_replace_usage, options);
		return edit_and_replace(argv[0], force, raw);

	case MODE_GRAFT:
		if (argc < 1)
			usage_msg_opt(_("-g needs at least one argument"),
				      git_replace_usage, options);
		return create_graft(argc, argv, force, 0);

	case MODE_CONVERT_GRAFT_FILE:
		if (argc != 0)
			usage_msg_opt(_("--convert-graft-file takes no argument"),
				      git_replace_usage, options);
		return !!convert_graft_file(force);

	case MODE_LIST:
		if (argc > 1)
			usage_msg_opt(_("only one pattern can be given with -l"),
				      git_replace_usage, options);
		return list_replace_refs(argv[0], format);

	default:
		BUG("invalid cmdmode %d", (int)cmdmode);
	}
}
