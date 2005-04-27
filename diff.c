/*
 * Copyright (C) 2005 Junio C Hamano
 */
#include <sys/types.h>
#include <sys/wait.h>
#include "cache.h"
#include "diff.h"

static char *diff_cmd = "diff -L'k/%s' -L'l/%s'";
static char *diff_opts = "-pu";

static const char *external_diff(void)
{
	static char *external_diff_cmd = NULL;
	static int done_preparing = 0;

	if (done_preparing)
		return external_diff_cmd;

	/*
	 * Default values above are meant to match the
	 * Linux kernel development style.  Examples of
	 * alternative styles you can specify via environment
	 * variables are:
	 *
	 * GIT_DIFF_CMD="diff -L '%s' -L '%s'"
	 * GIT_DIFF_OPTS="-c";
	 */
	if (getenv("GIT_EXTERNAL_DIFF"))
		external_diff_cmd = getenv("GIT_EXTERNAL_DIFF");

	/* In case external diff fails... */
	diff_cmd = getenv("GIT_DIFF_CMD") ? : diff_cmd;
	diff_opts = getenv("GIT_DIFF_OPTS") ? : diff_opts;

	done_preparing = 1;
	return external_diff_cmd;
}

/* Help to copy the thing properly quoted for the shell safety.
 * any single quote is replaced with '\'', and the caller is
 * expected to enclose the result within a single quote pair.
 *
 * E.g.
 *  original     sq_expand     result
 *  name     ==> name      ==> 'name'
 *  a b      ==> a b       ==> 'a b'
 *  a'b      ==> a'\''b    ==> 'a'\''b'
 */
static char *sq_expand(const char *src)
{
	static char *buf = NULL;
	int cnt, c;
	const char *cp;
	char *bp;

	/* count bytes needed to store the quoted string. */ 
	for (cnt = 1, cp = src; *cp; cnt++, cp++)
		if (*cp == '\'')
			cnt += 3;

	buf = xmalloc(cnt);
	bp = buf;
	while ((c = *src++)) {
		if (c != '\'')
			*bp++ = c;
		else {
			bp = strcpy(bp, "'\\''");
			bp += 4;
		}
	}
	*bp = 0;
	return buf;
}

static struct diff_tempfile {
	const char *name;
	char hex[41];
	char mode[10];
	char tmp_path[50];
} diff_temp[2];

static void builtin_diff(const char *name,
			 struct diff_tempfile *temp)
{
	static char *diff_arg  = "'%s' '%s'";
	const char *name_1_sq = sq_expand(temp[0].name);
	const char *name_2_sq = sq_expand(temp[1].name);
	const char *name_sq = sq_expand(name);

	/* diff_cmd and diff_arg have 4 %s in total which makes
	 * the sum of these strings 8 bytes larger than required.
	 * we use 2 spaces around diff-opts, and we need to count
	 * terminating NUL, so we subtract 5 here.
	 */
	int cmd_size = (strlen(diff_cmd) + 
			strlen(name_sq) * 2 +
			strlen(diff_opts) +
			strlen(diff_arg) +
			strlen(name_1_sq) + strlen(name_2_sq)
			- 5);
	char *cmd = xmalloc(cmd_size);
	int next_at = 0;

	next_at += snprintf(cmd+next_at, cmd_size-next_at,
			    diff_cmd, name_sq, name_sq);
	next_at += snprintf(cmd+next_at, cmd_size-next_at,
			    " %s ", diff_opts);
	next_at += snprintf(cmd+next_at, cmd_size-next_at,
			    diff_arg, name_1_sq, name_2_sq);
	execlp("/bin/sh","sh", "-c", cmd, NULL);
}

static void prepare_temp_file(const char *name,
			      struct diff_tempfile *temp,
			      struct diff_spec *one)
{
	static unsigned char null_sha1[20] = { 0, };

	if (!one->file_valid) {
	not_a_valid_file:
		temp->name = "/dev/null";
		strcpy(temp->hex, ".");
		strcpy(temp->mode, ".");
		return;
	}

	if (one->sha1_valid &&
	    !memcmp(one->u.sha1, null_sha1, sizeof(null_sha1))) {
		one->sha1_valid = 0;
		one->u.name = name;
	}

	if (!one->sha1_valid) {
		struct stat st;
		temp->name = one->u.name;
		if (stat(temp->name, &st) < 0) {
			if (errno == ENOENT)
				goto not_a_valid_file;
			die("stat(%s): %s", temp->name, strerror(errno));
		}
		strcpy(temp->hex, ".");
		sprintf(temp->mode, "%06o",
			S_IFREG |ce_permissions(st.st_mode));
	}
	else {
		int fd;
		void *blob;
		char type[20];
		unsigned long size;

		blob = read_sha1_file(one->u.sha1, type, &size);
		if (!blob || strcmp(type, "blob"))
			die("unable to read blob object for %s (%s)",
			    name, sha1_to_hex(one->u.sha1));

		strcpy(temp->tmp_path, ".diff_XXXXXX");
		fd = mkstemp(temp->tmp_path);
		if (fd < 0)
			die("unable to create temp-file");
		if (write(fd, blob, size) != size)
			die("unable to write temp-file");
		close(fd);
		free(blob);
		temp->name = temp->tmp_path;
		strcpy(temp->hex, sha1_to_hex(one->u.sha1));
		temp->hex[40] = 0;
		sprintf(temp->mode, "%06o", one->mode);
	}
}

static void remove_tempfile(void)
{
	int i;

	for (i = 0; i < 2; i++)
		if (diff_temp[i].name == diff_temp[i].tmp_path) {
			unlink(diff_temp[i].name);
			diff_temp[i].name = NULL;
		}
}

/* An external diff command takes:
 *
 * diff-cmd name infile1 infile1-sha1 infile1-mode \
 *               infile2 infile2-sha1 infile2-mode.
 *
 */
void run_external_diff(const char *name,
		       struct diff_spec *one,
		       struct diff_spec *two)
{
	struct diff_tempfile *temp = diff_temp;
	int pid, status;
	static int atexit_asked = 0;

	if (one && two) {
		prepare_temp_file(name, &temp[0], one);
		prepare_temp_file(name, &temp[1], two);
		if (! atexit_asked &&
		    (temp[0].name == temp[0].tmp_path ||
		     temp[1].name == temp[1].tmp_path)) {
			atexit_asked = 1;
			atexit(remove_tempfile);
		}
	}

	fflush(NULL);
	pid = fork();
	if (pid < 0)
		die("unable to fork");
	if (!pid) {
		const char *pgm = external_diff();
		if (pgm) {
			if (one && two)
				execlp(pgm, pgm,
				       name,
				       temp[0].name, temp[0].hex, temp[0].mode,
				       temp[1].name, temp[1].hex, temp[1].mode,
				       NULL);
			else
				execlp(pgm, pgm, name, NULL);
		}
		/*
		 * otherwise we use the built-in one.
		 */
		if (one && two)
			builtin_diff(name, temp);
		else
			printf("* Unmerged path %s\n", name);
		exit(0);
	}
	if (waitpid(pid, &status, 0) < 0 || !WIFEXITED(status))
		die("diff program failed");

	remove_tempfile();
}

void diff_addremove(int addremove, unsigned mode,
		    const unsigned char *sha1,
		    const char *base, const char *path)
{
	char concatpath[PATH_MAX];
	struct diff_spec spec[2], *one, *two;

	memcpy(spec[0].u.sha1, sha1, 20);
	spec[0].mode = mode;
	spec[0].sha1_valid = spec[0].file_valid = 1;
	spec[1].file_valid = 0;

	if (addremove == '+') {
		one = spec + 1; two = spec;
	} else {
		one = spec; two = one + 1;
	}
	
	if (path) {
		strcpy(concatpath, base);
		strcat(concatpath, "/");
		strcat(concatpath, path);
	}
	run_external_diff(path ? concatpath : base, one, two);
}

void diff_change(unsigned old_mode, unsigned new_mode,
		 const unsigned char *old_sha1,
		 const unsigned char *new_sha1,
		 const char *base, const char *path) {
	char concatpath[PATH_MAX];
	struct diff_spec spec[2];

	memcpy(spec[0].u.sha1, old_sha1, 20);
	spec[0].mode = old_mode;
	memcpy(spec[1].u.sha1, new_sha1, 20);
	spec[1].mode = new_mode;
	spec[0].sha1_valid = spec[0].file_valid = 1;
	spec[1].sha1_valid = spec[1].file_valid = 1;

	if (path) {
		strcpy(concatpath, base);
		strcat(concatpath, "/");
		strcat(concatpath, path);
	}
	run_external_diff(path ? concatpath : base, &spec[0], &spec[1]);
}

void diff_unmerge(const char *path)
{
	run_external_diff(path, NULL, NULL);
}
