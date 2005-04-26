#include "cache.h"
#include "diff.h"

static char *diff_cmd = "diff -L 'k/%s' -L 'l/%s' ";
static char *diff_opts = "-p -u";
static char *diff_arg_forward  = " - '%s'";
static char *diff_arg_reverse  = " '%s' -";

void prepare_diff_cmd(void)
{
	/*
	 * Default values above are meant to match the
	 * Linux kernel development style.  Examples of
	 * alternative styles you can specify via environment
	 * variables are:
	 *
	 * GIT_DIFF_CMD="diff -L '%s' -L '%s'"
	 * GIT_DIFF_OPTS="-c";
	 */
	diff_cmd = getenv("GIT_DIFF_CMD") ? : diff_cmd;
	diff_opts = getenv("GIT_DIFF_OPTS") ? : diff_opts;
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

	if (! (buf = malloc(cnt)))
	    return buf;
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

void show_differences(const char *name, /* filename on the filesystem */
		      const char *label, /* diff label to use */
		      void *old_contents, /* contents in core */
		      unsigned long long old_size, /* size in core */
		      int reverse /* 0: diff core file
				     1: diff file core */)
{
	FILE *f;
	char *name_sq = sq_expand(name);
	const char *label_sq = (name != label) ? sq_expand(label) : name_sq;
	char *diff_arg = reverse ? diff_arg_reverse : diff_arg_forward;
	int cmd_size = strlen(name_sq) + strlen(label_sq) * 2 +
		strlen(diff_cmd) + strlen(diff_opts) + strlen(diff_arg);
	char *cmd = malloc(cmd_size);
	int next_at;

	fflush(stdout);
	next_at = snprintf(cmd, cmd_size, diff_cmd, label_sq, label_sq);
	next_at += snprintf(cmd+next_at, cmd_size-next_at, "%s", diff_opts);
	next_at += snprintf(cmd+next_at, cmd_size-next_at, diff_arg, name_sq);
	f = popen(cmd, "w");
	if (old_size)
		fwrite(old_contents, old_size, 1, f);
	pclose(f);
	if (label_sq != name_sq)
		free((void*)label_sq); /* constness */
	free(name_sq);
	free(cmd);
}

void show_diff_empty(const unsigned char *sha1,
		     const char *name,
		     int reverse)
{
	char *old;
	unsigned long int size;
	unsigned char type[20];

	old = read_sha1_file(sha1, type, &size);
	if (! old) {
		error("unable to read blob object for %s (%s)", name,
		      sha1_to_hex(sha1));
		return;
	}
	show_differences("/dev/null", name, old, size, reverse);
}
