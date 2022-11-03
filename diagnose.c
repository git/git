#include "cache.h"
#include "diagnose.h"
#include "compat/disk.h"
#include "archive.h"
#include "dir.h"
#include "help.h"
#include "strvec.h"
#include "object-store.h"
#include "packfile.h"

struct archive_dir {
	const char *path;
	int recursive;
};

struct diagnose_option {
	enum diagnose_mode mode;
	const char *option_name;
};

static struct diagnose_option diagnose_options[] = {
	{ DIAGNOSE_STATS, "stats" },
	{ DIAGNOSE_ALL, "all" },
};

int option_parse_diagnose(const struct option *opt, const char *arg, int unset)
{
	int i;
	enum diagnose_mode *diagnose = opt->value;

	if (!arg) {
		*diagnose = unset ? DIAGNOSE_NONE : DIAGNOSE_STATS;
		return 0;
	}

	for (i = 0; i < ARRAY_SIZE(diagnose_options); i++) {
		if (!strcmp(arg, diagnose_options[i].option_name)) {
			*diagnose = diagnose_options[i].mode;
			return 0;
		}
	}

	return error(_("invalid --%s value '%s'"), opt->long_name, arg);
}

static void dir_file_stats_objects(const char *full_path, size_t full_path_len,
				   const char *file_name, void *data)
{
	struct strbuf *buf = data;
	struct stat st;

	if (!stat(full_path, &st))
		strbuf_addf(buf, "%-70s %16" PRIuMAX "\n", file_name,
			    (uintmax_t)st.st_size);
}

static int dir_file_stats(struct object_directory *object_dir, void *data)
{
	struct strbuf *buf = data;

	strbuf_addf(buf, "Contents of %s:\n", object_dir->path);

	for_each_file_in_pack_dir(object_dir->path, dir_file_stats_objects,
				  data);

	return 0;
}

/*
 * Get the d_type of a dirent. If the d_type is unknown, derive it from
 * stat.st_mode.
 *
 * Note that 'path' is assumed to have a trailing slash. It is also modified
 * in-place during the execution of the function, but is then reverted to its
 * original value before returning.
 */
static unsigned char get_dtype(struct dirent *e, struct strbuf *path)
{
	struct stat st;
	unsigned char dtype = DTYPE(e);
	size_t base_path_len;

	if (dtype != DT_UNKNOWN)
		return dtype;

	/* d_type unknown in dirent, try to fall back on lstat results */
	base_path_len = path->len;
	strbuf_addstr(path, e->d_name);
	if (lstat(path->buf, &st))
		goto cleanup;

	/* determine d_type from st_mode */
	if (S_ISREG(st.st_mode))
		dtype = DT_REG;
	else if (S_ISDIR(st.st_mode))
		dtype = DT_DIR;
	else if (S_ISLNK(st.st_mode))
		dtype = DT_LNK;

cleanup:
	strbuf_setlen(path, base_path_len);
	return dtype;
}

static int count_files(struct strbuf *path)
{
	DIR *dir = opendir(path->buf);
	struct dirent *e;
	int count = 0;

	if (!dir)
		return 0;

	while ((e = readdir_skip_dot_and_dotdot(dir)) != NULL)
		if (get_dtype(e, path) == DT_REG)
			count++;

	closedir(dir);
	return count;
}

static void loose_objs_stats(struct strbuf *buf, const char *path)
{
	DIR *dir = opendir(path);
	struct dirent *e;
	int count;
	int total = 0;
	unsigned char c;
	struct strbuf count_path = STRBUF_INIT;
	size_t base_path_len;

	if (!dir)
		return;

	strbuf_addstr(buf, "Object directory stats for ");
	strbuf_add_absolute_path(buf, path);
	strbuf_addstr(buf, ":\n");

	strbuf_add_absolute_path(&count_path, path);
	strbuf_addch(&count_path, '/');
	base_path_len = count_path.len;

	while ((e = readdir_skip_dot_and_dotdot(dir)) != NULL)
		if (get_dtype(e, &count_path) == DT_DIR &&
		    strlen(e->d_name) == 2 &&
		    !hex_to_bytes(&c, e->d_name, 1)) {
			strbuf_setlen(&count_path, base_path_len);
			strbuf_addf(&count_path, "%s/", e->d_name);
			total += (count = count_files(&count_path));
			strbuf_addf(buf, "%s : %7d files\n", e->d_name, count);
		}

	strbuf_addf(buf, "Total: %d loose objects", total);

	strbuf_release(&count_path);
	closedir(dir);
}

static int add_directory_to_archiver(struct strvec *archiver_args,
				     const char *path, int recurse)
{
	int at_root = !*path;
	DIR *dir;
	struct dirent *e;
	struct strbuf buf = STRBUF_INIT;
	size_t len;
	int res = 0;

	dir = opendir(at_root ? "." : path);
	if (!dir) {
		if (errno == ENOENT) {
			warning(_("could not archive missing directory '%s'"), path);
			return 0;
		}
		return error_errno(_("could not open directory '%s'"), path);
	}

	if (!at_root)
		strbuf_addf(&buf, "%s/", path);
	len = buf.len;
	strvec_pushf(archiver_args, "--prefix=%s", buf.buf);

	while (!res && (e = readdir_skip_dot_and_dotdot(dir))) {
		struct strbuf abspath = STRBUF_INIT;
		unsigned char dtype;

		strbuf_add_absolute_path(&abspath, at_root ? "." : path);
		strbuf_addch(&abspath, '/');
		dtype = get_dtype(e, &abspath);

		strbuf_setlen(&buf, len);
		strbuf_addstr(&buf, e->d_name);

		if (dtype == DT_REG)
			strvec_pushf(archiver_args, "--add-file=%s", buf.buf);
		else if (dtype != DT_DIR)
			warning(_("skipping '%s', which is neither file nor "
				  "directory"), buf.buf);
		else if (recurse &&
			 add_directory_to_archiver(archiver_args,
						   buf.buf, recurse) < 0)
			res = -1;

		strbuf_release(&abspath);
	}

	closedir(dir);
	strbuf_release(&buf);
	return res;
}

int create_diagnostics_archive(struct strbuf *zip_path, enum diagnose_mode mode)
{
	struct strvec archiver_args = STRVEC_INIT;
	char **argv_copy = NULL;
	int stdout_fd = -1, archiver_fd = -1;
	struct strbuf buf = STRBUF_INIT;
	int res, i;
	struct archive_dir archive_dirs[] = {
		{ ".git", 0 },
		{ ".git/hooks", 0 },
		{ ".git/info", 0 },
		{ ".git/logs", 1 },
		{ ".git/objects/info", 0 }
	};

	if (mode == DIAGNOSE_NONE) {
		res = 0;
		goto diagnose_cleanup;
	}

	stdout_fd = dup(STDOUT_FILENO);
	if (stdout_fd < 0) {
		res = error_errno(_("could not duplicate stdout"));
		goto diagnose_cleanup;
	}

	archiver_fd = xopen(zip_path->buf, O_CREAT | O_WRONLY | O_TRUNC, 0666);
	if (dup2(archiver_fd, STDOUT_FILENO) < 0) {
		res = error_errno(_("could not redirect output"));
		goto diagnose_cleanup;
	}

	init_zip_archiver();
	strvec_pushl(&archiver_args, "git-diagnose", "--format=zip", NULL);

	strbuf_reset(&buf);
	strbuf_addstr(&buf, "Collecting diagnostic info\n\n");
	get_version_info(&buf, 1);

	strbuf_addf(&buf, "Repository root: %s\n", the_repository->worktree);
	get_disk_info(&buf);
	write_or_die(stdout_fd, buf.buf, buf.len);
	strvec_pushf(&archiver_args,
		     "--add-virtual-file=diagnostics.log:%.*s",
		     (int)buf.len, buf.buf);

	strbuf_reset(&buf);
	strbuf_addstr(&buf, "--add-virtual-file=packs-local.txt:");
	dir_file_stats(the_repository->objects->odb, &buf);
	foreach_alt_odb(dir_file_stats, &buf);
	strvec_push(&archiver_args, buf.buf);

	strbuf_reset(&buf);
	strbuf_addstr(&buf, "--add-virtual-file=objects-local.txt:");
	loose_objs_stats(&buf, ".git/objects");
	strvec_push(&archiver_args, buf.buf);

	/* Only include this if explicitly requested */
	if (mode == DIAGNOSE_ALL) {
		for (i = 0; i < ARRAY_SIZE(archive_dirs); i++) {
			if (add_directory_to_archiver(&archiver_args,
						      archive_dirs[i].path,
						      archive_dirs[i].recursive)) {
				res = error_errno(_("could not add directory '%s' to archiver"),
						  archive_dirs[i].path);
				goto diagnose_cleanup;
			}
		}
	}

	strvec_pushl(&archiver_args, "--prefix=",
		     oid_to_hex(the_hash_algo->empty_tree), "--", NULL);

	/* `write_archive()` modifies the `argv` passed to it. Let it. */
	argv_copy = xmemdupz(archiver_args.v,
			     sizeof(char *) * archiver_args.nr);
	res = write_archive(archiver_args.nr, (const char **)argv_copy, NULL,
			    the_repository, NULL, 0);
	if (res) {
		error(_("failed to write archive"));
		goto diagnose_cleanup;
	}

	fprintf(stderr, "\n"
		"Diagnostics complete.\n"
		"All of the gathered info is captured in '%s'\n",
		zip_path->buf);

diagnose_cleanup:
	if (archiver_fd >= 0) {
		dup2(stdout_fd, STDOUT_FILENO);
		close(stdout_fd);
		close(archiver_fd);
	}
	free(argv_copy);
	strvec_clear(&archiver_args);
	strbuf_release(&buf);

	return res;
}
