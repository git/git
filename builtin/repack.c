#include "builtin.h"
#include "cache.h"
#include "config.h"
#include "dir.h"
#include "parse-options.h"
#include "run-command.h"
#include "sigchain.h"
#include "strbuf.h"
#include "string-list.h"
#include "argv-array.h"
#include "midx.h"
#include "packfile.h"
#include "object-store.h"

static int delta_base_offset = 1;
static int pack_kept_objects = -1;
static int write_bitmaps;
static int use_delta_islands;
static char *packdir, *packtmp;

static const char *const git_repack_usage[] = {
	N_("git repack [<options>]"),
	NULL
};

static const char incremental_bitmap_conflict_error[] = N_(
"Incremental repacks are incompatible with bitmap indexes.  Use\n"
"--no-write-bitmap-index or disable the pack.writebitmaps configuration."
);


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
	if (!strcmp(var, "repack.usedeltaislands")) {
		use_delta_islands = git_config_bool(var, value);
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
 * have a corresponding .keep file. These packs are not to
 * be kept if we are going to pack everything into one file.
 */
static void get_non_kept_pack_filenames(struct string_list *fname_list,
					const struct string_list *extra_keep)
{
	DIR *dir;
	struct dirent *e;
	char *fname;

	if (!(dir = opendir(packdir)))
		return;

	while ((e = readdir(dir)) != NULL) {
		size_t len;
		int i;

		for (i = 0; i < extra_keep->nr; i++)
			if (!fspathcmp(e->d_name, extra_keep->items[i].string))
				break;
		if (extra_keep->nr > 0 && i < extra_keep->nr)
			continue;

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
	const char *exts[] = {".pack", ".idx", ".keep", ".bitmap", ".promisor"};
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

struct pack_objects_args {
	const char *window;
	const char *window_memory;
	const char *depth;
	const char *threads;
	const char *max_pack_size;
	int no_reuse_delta;
	int no_reuse_object;
	int quiet;
	int local;
};

static void prepare_pack_objects(struct child_process *cmd,
				 const struct pack_objects_args *args)
{
	argv_array_push(&cmd->args, "pack-objects");
	if (args->window)
		argv_array_pushf(&cmd->args, "--window=%s", args->window);
	if (args->window_memory)
		argv_array_pushf(&cmd->args, "--window-memory=%s", args->window_memory);
	if (args->depth)
		argv_array_pushf(&cmd->args, "--depth=%s", args->depth);
	if (args->threads)
		argv_array_pushf(&cmd->args, "--threads=%s", args->threads);
	if (args->max_pack_size)
		argv_array_pushf(&cmd->args, "--max-pack-size=%s", args->max_pack_size);
	if (args->no_reuse_delta)
		argv_array_pushf(&cmd->args, "--no-reuse-delta");
	if (args->no_reuse_object)
		argv_array_pushf(&cmd->args, "--no-reuse-object");
	if (args->local)
		argv_array_push(&cmd->args,  "--local");
	if (args->quiet)
		argv_array_push(&cmd->args,  "--quiet");
	if (delta_base_offset)
		argv_array_push(&cmd->args,  "--delta-base-offset");
	argv_array_push(&cmd->args, packtmp);
	cmd->git_cmd = 1;
	cmd->out = -1;
}

/*
 * Write oid to the given struct child_process's stdin, starting it first if
 * necessary.
 */
static int write_oid(const struct object_id *oid, struct packed_git *pack,
		     uint32_t pos, void *data)
{
	struct child_process *cmd = data;

	if (cmd->in == -1) {
		if (start_command(cmd))
			die(_("could not start pack-objects to repack promisor objects"));
	}

	xwrite(cmd->in, oid_to_hex(oid), GIT_SHA1_HEXSZ);
	xwrite(cmd->in, "\n", 1);
	return 0;
}

static void repack_promisor_objects(const struct pack_objects_args *args,
				    struct string_list *names)
{
	struct child_process cmd = CHILD_PROCESS_INIT;
	FILE *out;
	struct strbuf line = STRBUF_INIT;

	prepare_pack_objects(&cmd, args);
	cmd.in = -1;

	/*
	 * NEEDSWORK: Giving pack-objects only the OIDs without any ordering
	 * hints may result in suboptimal deltas in the resulting pack. See if
	 * the OIDs can be sent with fake paths such that pack-objects can use a
	 * {type -> existing pack order} ordering when computing deltas instead
	 * of a {type -> size} ordering, which may produce better deltas.
	 */
	for_each_packed_object(write_oid, &cmd,
			       FOR_EACH_OBJECT_PROMISOR_ONLY);

	if (cmd.in == -1)
		/* No packed objects; cmd was never started */
		return;

	close(cmd.in);

	out = xfdopen(cmd.out, "r");
	while (strbuf_getline_lf(&line, out) != EOF) {
		char *promisor_name;
		int fd;
		if (line.len != the_hash_algo->hexsz)
			die(_("repack: Expecting full hex object ID lines only from pack-objects."));
		string_list_append(names, line.buf);

		/*
		 * pack-objects creates the .pack and .idx files, but not the
		 * .promisor file. Create the .promisor file, which is empty.
		 */
		promisor_name = mkpathdup("%s-%s.promisor", packtmp,
					  line.buf);
		fd = open(promisor_name, O_CREAT|O_EXCL|O_WRONLY, 0600);
		if (fd < 0)
			die_errno(_("unable to create '%s'"), promisor_name);
		close(fd);
		free(promisor_name);
	}
	fclose(out);
	if (finish_command(&cmd))
		die(_("could not finish pack-objects to repack promisor objects"));
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
		{".promisor", 1},
	};
	struct child_process cmd = CHILD_PROCESS_INIT;
	struct string_list_item *item;
	struct string_list names = STRING_LIST_INIT_DUP;
	struct string_list rollback = STRING_LIST_INIT_NODUP;
	struct string_list existing_packs = STRING_LIST_INIT_DUP;
	struct strbuf line = STRBUF_INIT;
	int i, ext, ret, failed;
	FILE *out;

	/* variables to be filled by option parsing */
	int pack_everything = 0;
	int delete_redundant = 0;
	const char *unpack_unreachable = NULL;
	int keep_unreachable = 0;
	struct string_list keep_pack_list = STRING_LIST_INIT_NODUP;
	int no_update_server_info = 0;
	int midx_cleared = 0;
	struct pack_objects_args po_args = {NULL};

	struct option builtin_repack_options[] = {
		OPT_BIT('a', NULL, &pack_everything,
				N_("pack everything in a single pack"), ALL_INTO_ONE),
		OPT_BIT('A', NULL, &pack_everything,
				N_("same as -a, and turn unreachable objects loose"),
				   LOOSEN_UNREACHABLE | ALL_INTO_ONE),
		OPT_BOOL('d', NULL, &delete_redundant,
				N_("remove redundant packs, and run git-prune-packed")),
		OPT_BOOL('f', NULL, &po_args.no_reuse_delta,
				N_("pass --no-reuse-delta to git-pack-objects")),
		OPT_BOOL('F', NULL, &po_args.no_reuse_object,
				N_("pass --no-reuse-object to git-pack-objects")),
		OPT_BOOL('n', NULL, &no_update_server_info,
				N_("do not run git-update-server-info")),
		OPT__QUIET(&po_args.quiet, N_("be quiet")),
		OPT_BOOL('l', "local", &po_args.local,
				N_("pass --local to git-pack-objects")),
		OPT_BOOL('b', "write-bitmap-index", &write_bitmaps,
				N_("write bitmap index")),
		OPT_BOOL('i', "delta-islands", &use_delta_islands,
				N_("pass --delta-islands to git-pack-objects")),
		OPT_STRING(0, "unpack-unreachable", &unpack_unreachable, N_("approxidate"),
				N_("with -A, do not loosen objects older than this")),
		OPT_BOOL('k', "keep-unreachable", &keep_unreachable,
				N_("with -a, repack unreachable objects")),
		OPT_STRING(0, "window", &po_args.window, N_("n"),
				N_("size of the window used for delta compression")),
		OPT_STRING(0, "window-memory", &po_args.window_memory, N_("bytes"),
				N_("same as the above, but limit memory size instead of entries count")),
		OPT_STRING(0, "depth", &po_args.depth, N_("n"),
				N_("limits the maximum delta depth")),
		OPT_STRING(0, "threads", &po_args.threads, N_("n"),
				N_("limits the maximum number of threads")),
		OPT_STRING(0, "max-pack-size", &po_args.max_pack_size, N_("bytes"),
				N_("maximum size of each packfile")),
		OPT_BOOL(0, "pack-kept-objects", &pack_kept_objects,
				N_("repack objects in packs marked with .keep")),
		OPT_STRING_LIST(0, "keep-pack", &keep_pack_list, N_("name"),
				N_("do not repack this pack")),
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

	if (write_bitmaps && !(pack_everything & ALL_INTO_ONE))
		die(_(incremental_bitmap_conflict_error));

	packdir = mkpathdup("%s/pack", get_object_directory());
	packtmp = mkpathdup("%s/.tmp-%d-pack", packdir, (int)getpid());

	sigchain_push_common(remove_pack_on_signal);

	prepare_pack_objects(&cmd, &po_args);

	argv_array_push(&cmd.args, "--keep-true-parents");
	if (!pack_kept_objects)
		argv_array_push(&cmd.args, "--honor-pack-keep");
	for (i = 0; i < keep_pack_list.nr; i++)
		argv_array_pushf(&cmd.args, "--keep-pack=%s",
				 keep_pack_list.items[i].string);
	argv_array_push(&cmd.args, "--non-empty");
	argv_array_push(&cmd.args, "--all");
	argv_array_push(&cmd.args, "--reflog");
	argv_array_push(&cmd.args, "--indexed-objects");
	if (repository_format_partial_clone)
		argv_array_push(&cmd.args, "--exclude-promisor-objects");
	if (write_bitmaps)
		argv_array_push(&cmd.args, "--write-bitmap-index");
	if (use_delta_islands)
		argv_array_push(&cmd.args, "--delta-islands");

	if (pack_everything & ALL_INTO_ONE) {
		get_non_kept_pack_filenames(&existing_packs, &keep_pack_list);

		repack_promisor_objects(&po_args, &names);

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

	cmd.no_stdin = 1;

	ret = start_command(&cmd);
	if (ret)
		return ret;

	out = xfdopen(cmd.out, "r");
	while (strbuf_getline_lf(&line, out) != EOF) {
		if (line.len != the_hash_algo->hexsz)
			die(_("repack: Expecting full hex object ID lines only from pack-objects."));
		string_list_append(&names, line.buf);
	}
	fclose(out);
	ret = finish_command(&cmd);
	if (ret)
		return ret;

	if (!names.nr && !po_args.quiet)
		printf_ln(_("Nothing new to pack."));

	close_all_packs(the_repository->objects);

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

			if (!midx_cleared) {
				clear_midx_file(the_repository);
				midx_cleared = 1;
			}

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
				_("WARNING: Some packs in use have been renamed by\n"
				  "WARNING: prefixing old- to their name, in order to\n"
				  "WARNING: replace them with the new version of the\n"
				  "WARNING: file.  But the operation failed, and the\n"
				  "WARNING: attempt to rename them back to their\n"
				  "WARNING: original names also failed.\n"
				  "WARNING: Please rename them in %s manually:\n"), packdir);
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

	reprepare_packed_git(the_repository);

	if (delete_redundant) {
		const int hexsz = the_hash_algo->hexsz;
		int opts = 0;
		string_list_sort(&names);
		for_each_string_list_item(item, &existing_packs) {
			char *sha1;
			size_t len = strlen(item->string);
			if (len < hexsz)
				continue;
			sha1 = item->string + len - hexsz;
			if (!string_list_has_string(&names, sha1))
				remove_redundant_pack(packdir, item->string);
		}
		if (!po_args.quiet && isatty(2))
			opts |= PRUNE_PACKED_VERBOSE;
		prune_packed_objects(opts);

		if (!keep_unreachable &&
		    (!(pack_everything & LOOSEN_UNREACHABLE) ||
		     unpack_unreachable) &&
		    is_repository_shallow(the_repository))
			prune_shallow(PRUNE_QUICK);
	}

	if (!no_update_server_info)
		update_server_info(0);
	remove_temporary_files();

	if (git_env_bool(GIT_TEST_MULTI_PACK_INDEX, 0))
		write_midx_file(get_object_directory());

	string_list_clear(&names, 0);
	string_list_clear(&rollback, 0);
	string_list_clear(&existing_packs, 0);
	strbuf_release(&line);

	return 0;
}
