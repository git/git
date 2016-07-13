#include "builtin.h"
#include "cache.h"
#include "dir.h"
#include "parse-options.h"
#include "run-command.h"
#include "sigchain.h"
#include "strbuf.h"
#include "string-list.h"
#include "argv-array.h"

static int delta_base_offset = 1;
static int pack_kept_objects = -1;
static int write_bitmaps;
static char *packdir, *packtmp;

static const char *const git_repack_usage[] = {
	N_("git repack [<options>]"),
	NULL
};

static int repack_config(const char *var, const char *value, void *cb)
{
	if (!strcmp(var, "repack.usedeltabaseoffset")) {
		delta_base_offset = git_config_bool(var, value);
		return 0;
	}
	if (!strcmp(var, "repack.packkeptobjects")) {
		pack_kept_objects = git_config_bool(var, value);
		return 0;
	}
	if (!strcmp(var, "repack.writebitmaps") ||
	    !strcmp(var, "pack.writebitmaps")) {
		write_bitmaps = git_config_bool(var, value);
		return 0;
	}
	return git_default_config(var, value, cb);
}

/*
 * Remove temporary $GIT_OBJECT_DIRECTORY/pack/.tmp-$$-pack-* files.
 */
static void remove_temporary_files(void)
{
	struct strbuf buf = STRBUF_INIT;
	size_t dirlen, prefixlen;
	DIR *dir;
	struct dirent *e;

	dir = opendir(packdir);
	if (!dir)
		return;

	/* Point at the slash at the end of ".../objects/pack/" */
	dirlen = strlen(packdir) + 1;
	strbuf_addstr(&buf, packtmp);
	/* Hold the length of  ".tmp-%d-pack-" */
	prefixlen = buf.len - dirlen;

	while ((e = readdir(dir))) {
		if (strncmp(e->d_name, buf.buf + dirlen, prefixlen))
			continue;
		strbuf_setlen(&buf, dirlen);
		strbuf_addstr(&buf, e->d_name);
		unlink(buf.buf);
	}
	closedir(dir);
	strbuf_release(&buf);
}

static void remove_pack_on_signal(int signo)
{
	remove_temporary_files();
	sigchain_pop(signo);
	raise(signo);
}

/*
 * Adds all packs hex strings to the fname list, which do not
 * have a corresponding .keep file.
 */
static void get_non_kept_pack_filenames(struct string_list *fname_list)
{
	DIR *dir;
	struct dirent *e;
	char *fname;

	if (!(dir = opendir(packdir)))
		return;

	while ((e = readdir(dir)) != NULL) {
		size_t len;
		if (!strip_suffix(e->d_name, ".pack", &len))
			continue;

		fname = xmemdupz(e->d_name, len);

		if (!file_exists(mkpath("%s/%s.keep", packdir, fname)))
			string_list_append_nodup(fname_list, fname);
		else
			free(fname);
	}
	closedir(dir);
}

static void remove_redundant_pack(const char *dir_name, const char *base_name)
{
	const char *exts[] = {".pack", ".idx", ".keep", ".bitmap"};
	int i;
	struct strbuf buf = STRBUF_INIT;
	size_t plen;

	strbuf_addf(&buf, "%s/%s", dir_name, base_name);
	plen = buf.len;

	for (i = 0; i < ARRAY_SIZE(exts); i++) {
		strbuf_setlen(&buf, plen);
		strbuf_addstr(&buf, exts[i]);
		unlink(buf.buf);
	}
	strbuf_release(&buf);
}

#define ALL_INTO_ONE 1
#define LOOSEN_UNREACHABLE 2

int cmd_repack(int argc, const char **argv, const char *prefix)
{
	struct {
		const char *name;
		unsigned optional:1;
	} exts[] = {
		{".pack"},
		{".idx"},
		{".bitmap", 1},
	};
	struct child_process cmd = CHILD_PROCESS_INIT;
	struct string_list_item *item;
	struct string_list names = STRING_LIST_INIT_DUP;
	struct string_list rollback = STRING_LIST_INIT_NODUP;
	struct string_list existing_packs = STRING_LIST_INIT_DUP;
	struct strbuf line = STRBUF_INIT;
	int ext, ret, failed;
	FILE *out;

	/* variables to be filled by option parsing */
	int pack_everything = 0;
	int delete_redundant = 0;
	const char *unpack_unreachable = NULL;
	int keep_unreachable = 0;
	const char *window = NULL, *window_memory = NULL;
	const char *depth = NULL;
	const char *max_pack_size = NULL;
	int no_reuse_delta = 0, no_reuse_object = 0;
	int no_update_server_info = 0;
	int quiet = 0;
	int local = 0;

	struct option builtin_repack_options[] = {
		OPT_BIT('a', NULL, &pack_everything,
				N_("pack everything in a single pack"), ALL_INTO_ONE),
		OPT_BIT('A', NULL, &pack_everything,
				N_("same as -a, and turn unreachable objects loose"),
				   LOOSEN_UNREACHABLE | ALL_INTO_ONE),
		OPT_BOOL('d', NULL, &delete_redundant,
				N_("remove redundant packs, and run git-prune-packed")),
		OPT_BOOL('f', NULL, &no_reuse_delta,
				N_("pass --no-reuse-delta to git-pack-objects")),
		OPT_BOOL('F', NULL, &no_reuse_object,
				N_("pass --no-reuse-object to git-pack-objects")),
		OPT_BOOL('n', NULL, &no_update_server_info,
				N_("do not run git-update-server-info")),
		OPT__QUIET(&quiet, N_("be quiet")),
		OPT_BOOL('l', "local", &local,
				N_("pass --local to git-pack-objects")),
		OPT_BOOL('b', "write-bitmap-index", &write_bitmaps,
				N_("write bitmap index")),
		OPT_STRING(0, "unpack-unreachable", &unpack_unreachable, N_("approxidate"),
				N_("with -A, do not loosen objects older than this")),
		OPT_BOOL('k', "keep-unreachable", &keep_unreachable,
				N_("with -a, repack unreachable objects")),
		OPT_STRING(0, "window", &window, N_("n"),
				N_("size of the window used for delta compression")),
		OPT_STRING(0, "window-memory", &window_memory, N_("bytes"),
				N_("same as the above, but limit memory size instead of entries count")),
		OPT_STRING(0, "depth", &depth, N_("n"),
				N_("limits the maximum delta depth")),
		OPT_STRING(0, "max-pack-size", &max_pack_size, N_("bytes"),
				N_("maximum size of each packfile")),
		OPT_BOOL(0, "pack-kept-objects", &pack_kept_objects,
				N_("repack objects in packs marked with .keep")),
		OPT_END()
	};

	git_config(repack_config, NULL);

	argc = parse_options(argc, argv, prefix, builtin_repack_options,
				git_repack_usage, 0);

	if (delete_redundant && repository_format_precious_objects)
		die(_("cannot delete packs in a precious-objects repo"));

	if (keep_unreachable &&
	    (unpack_unreachable || (pack_everything & LOOSEN_UNREACHABLE)))
		die(_("--keep-unreachable and -A are incompatible"));

	if (pack_kept_objects < 0)
		pack_kept_objects = write_bitmaps;

	packdir = mkpathdup("%s/pack", get_object_directory());
	packtmp = mkpathdup("%s/.tmp-%d-pack", packdir, (int)getpid());

	sigchain_push_common(remove_pack_on_signal);

	argv_array_push(&cmd.args, "pack-objects");
	argv_array_push(&cmd.args, "--keep-true-parents");
	if (!pack_kept_objects)
		argv_array_push(&cmd.args, "--honor-pack-keep");
	argv_array_push(&cmd.args, "--non-empty");
	argv_array_push(&cmd.args, "--all");
	argv_array_push(&cmd.args, "--reflog");
	argv_array_push(&cmd.args, "--indexed-objects");
	if (window)
		argv_array_pushf(&cmd.args, "--window=%s", window);
	if (window_memory)
		argv_array_pushf(&cmd.args, "--window-memory=%s", window_memory);
	if (depth)
		argv_array_pushf(&cmd.args, "--depth=%s", depth);
	if (max_pack_size)
		argv_array_pushf(&cmd.args, "--max-pack-size=%s", max_pack_size);
	if (no_reuse_delta)
		argv_array_pushf(&cmd.args, "--no-reuse-delta");
	if (no_reuse_object)
		argv_array_pushf(&cmd.args, "--no-reuse-object");
	if (write_bitmaps)
		argv_array_push(&cmd.args, "--write-bitmap-index");

	if (pack_everything & ALL_INTO_ONE) {
		get_non_kept_pack_filenames(&existing_packs);

		if (existing_packs.nr && delete_redundant) {
			if (unpack_unreachable) {
				argv_array_pushf(&cmd.args,
						"--unpack-unreachable=%s",
						unpack_unreachable);
				argv_array_push(&cmd.env_array, "GIT_REF_PARANOIA=1");
			} else if (pack_everything & LOOSEN_UNREACHABLE) {
				argv_array_push(&cmd.args,
						"--unpack-unreachable");
			} else if (keep_unreachable) {
				argv_array_push(&cmd.args, "--keep-unreachable");
				argv_array_push(&cmd.args, "--pack-loose-unreachable");
			} else {
				argv_array_push(&cmd.env_array, "GIT_REF_PARANOIA=1");
			}
		}
	} else {
		argv_array_push(&cmd.args, "--unpacked");
		argv_array_push(&cmd.args, "--incremental");
	}

	if (local)
		argv_array_push(&cmd.args,  "--local");
	if (quiet)
		argv_array_push(&cmd.args,  "--quiet");
	if (delta_base_offset)
		argv_array_push(&cmd.args,  "--delta-base-offset");

	argv_array_push(&cmd.args, packtmp);

	cmd.git_cmd = 1;
	cmd.out = -1;
	cmd.no_stdin = 1;

	ret = start_command(&cmd);
	if (ret)
		return ret;

	out = xfdopen(cmd.out, "r");
	while (strbuf_getline_lf(&line, out) != EOF) {
		if (line.len != 40)
			die("repack: Expecting 40 character sha1 lines only from pack-objects.");
		string_list_append(&names, line.buf);
	}
	fclose(out);
	ret = finish_command(&cmd);
	if (ret)
		return ret;

	if (!names.nr && !quiet)
		printf("Nothing new to pack.\n");

	/*
	 * Ok we have prepared all new packfiles.
	 * First see if there are packs of the same name and if so
	 * if we can move them out of the way (this can happen if we
	 * repacked immediately after packing fully.
	 */
	failed = 0;
	for_each_string_list_item(item, &names) {
		for (ext = 0; ext < ARRAY_SIZE(exts); ext++) {
			char *fname, *fname_old;
			fname = mkpathdup("%s/pack-%s%s", packdir,
						item->string, exts[ext].name);
			if (!file_exists(fname)) {
				free(fname);
				continue;
			}

			fname_old = mkpathdup("%s/old-%s%s", packdir,
						item->string, exts[ext].name);
			if (file_exists(fname_old))
				if (unlink(fname_old))
					failed = 1;

			if (!failed && rename(fname, fname_old)) {
				free(fname);
				free(fname_old);
				failed = 1;
				break;
			} else {
				string_list_append(&rollback, fname);
				free(fname_old);
			}
		}
		if (failed)
			break;
	}
	if (failed) {
		struct string_list rollback_failure = STRING_LIST_INIT_DUP;
		for_each_string_list_item(item, &rollback) {
			char *fname, *fname_old;
			fname = mkpathdup("%s/%s", packdir, item->string);
			fname_old = mkpathdup("%s/old-%s", packdir, item->string);
			if (rename(fname_old, fname))
				string_list_append(&rollback_failure, fname);
			free(fname);
			free(fname_old);
		}

		if (rollback_failure.nr) {
			int i;
			fprintf(stderr,
				"WARNING: Some packs in use have been renamed by\n"
				"WARNING: prefixing old- to their name, in order to\n"
				"WARNING: replace them with the new version of the\n"
				"WARNING: file.  But the operation failed, and the\n"
				"WARNING: attempt to rename them back to their\n"
				"WARNING: original names also failed.\n"
				"WARNING: Please rename them in %s manually:\n", packdir);
			for (i = 0; i < rollback_failure.nr; i++)
				fprintf(stderr, "WARNING:   old-%s -> %s\n",
					rollback_failure.items[i].string,
					rollback_failure.items[i].string);
		}
		exit(1);
	}

	/* Now the ones with the same name are out of the way... */
	for_each_string_list_item(item, &names) {
		for (ext = 0; ext < ARRAY_SIZE(exts); ext++) {
			char *fname, *fname_old;
			struct stat statbuffer;
			int exists = 0;
			fname = mkpathdup("%s/pack-%s%s",
					packdir, item->string, exts[ext].name);
			fname_old = mkpathdup("%s-%s%s",
					packtmp, item->string, exts[ext].name);
			if (!stat(fname_old, &statbuffer)) {
				statbuffer.st_mode &= ~(S_IWUSR | S_IWGRP | S_IWOTH);
				chmod(fname_old, statbuffer.st_mode);
				exists = 1;
			}
			if (exists || !exts[ext].optional) {
				if (rename(fname_old, fname))
					die_errno(_("renaming '%s' failed"), fname_old);
			}
			free(fname);
			free(fname_old);
		}
	}

	/* Remove the "old-" files */
	for_each_string_list_item(item, &names) {
		for (ext = 0; ext < ARRAY_SIZE(exts); ext++) {
			char *fname;
			fname = mkpathdup("%s/old-%s%s",
					  packdir,
					  item->string,
					  exts[ext].name);
			if (remove_path(fname))
				warning(_("failed to remove '%s'"), fname);
			free(fname);
		}
	}

	/* End of pack replacement. */

	if (delete_redundant) {
		int opts = 0;
		string_list_sort(&names);
		for_each_string_list_item(item, &existing_packs) {
			char *sha1;
			size_t len = strlen(item->string);
			if (len < 40)
				continue;
			sha1 = item->string + len - 40;
			if (!string_list_has_string(&names, sha1))
				remove_redundant_pack(packdir, item->string);
		}
		if (!quiet && isatty(2))
			opts |= PRUNE_PACKED_VERBOSE;
		prune_packed_objects(opts);
	}

	if (!no_update_server_info)
		update_server_info(0);
	remove_temporary_files();
	string_list_clear(&names, 0);
	string_list_clear(&rollback, 0);
	string_list_clear(&existing_packs, 0);
	strbuf_release(&line);

	return 0;
}
