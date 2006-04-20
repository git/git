#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <sys/ioctl.h>
#include "git-compat-util.h"
#include "exec_cmd.h"
#include "common-cmds.h"

#include "cache.h"
#include "commit.h"
#include "diff.h"
#include "revision.h"
#include "log-tree.h"

#ifndef PATH_MAX
# define PATH_MAX 4096
#endif

static const char git_usage[] =
	"Usage: git [--version] [--exec-path[=GIT_EXEC_PATH]] [--help] COMMAND [ ARGS ]";

/* most gui terms set COLUMNS (although some don't export it) */
static int term_columns(void)
{
	char *col_string = getenv("COLUMNS");
	int n_cols = 0;

	if (col_string && (n_cols = atoi(col_string)) > 0)
		return n_cols;

#ifdef TIOCGWINSZ
	{
		struct winsize ws;
		if (!ioctl(1, TIOCGWINSZ, &ws)) {
			if (ws.ws_col)
				return ws.ws_col;
		}
	}
#endif

	return 80;
}

static void oom(void)
{
	fprintf(stderr, "git: out of memory\n");
	exit(1);
}

static inline void mput_char(char c, unsigned int num)
{
	while(num--)
		putchar(c);
}

static struct cmdname {
	size_t len;
	char name[1];
} **cmdname;
static int cmdname_alloc, cmdname_cnt;

static void add_cmdname(const char *name, int len)
{
	struct cmdname *ent;
	if (cmdname_alloc <= cmdname_cnt) {
		cmdname_alloc = cmdname_alloc + 200;
		cmdname = realloc(cmdname, cmdname_alloc * sizeof(*cmdname));
		if (!cmdname)
			oom();
	}
	ent = malloc(sizeof(*ent) + len);
	if (!ent)
		oom();
	ent->len = len;
	memcpy(ent->name, name, len);
	ent->name[len] = 0;
	cmdname[cmdname_cnt++] = ent;
}

static int cmdname_compare(const void *a_, const void *b_)
{
	struct cmdname *a = *(struct cmdname **)a_;
	struct cmdname *b = *(struct cmdname **)b_;
	return strcmp(a->name, b->name);
}

static void pretty_print_string_list(struct cmdname **cmdname, int longest)
{
	int cols = 1, rows;
	int space = longest + 1; /* min 1 SP between words */
	int max_cols = term_columns() - 1; /* don't print *on* the edge */
	int i, j;

	if (space < max_cols)
		cols = max_cols / space;
	rows = (cmdname_cnt + cols - 1) / cols;

	qsort(cmdname, cmdname_cnt, sizeof(*cmdname), cmdname_compare);

	for (i = 0; i < rows; i++) {
		printf("  ");

		for (j = 0; j < cols; j++) {
			int n = j * rows + i;
			int size = space;
			if (n >= cmdname_cnt)
				break;
			if (j == cols-1 || n + rows >= cmdname_cnt)
				size = 1;
			printf("%-*s", size, cmdname[n]->name);
		}
		putchar('\n');
	}
}

static void list_commands(const char *exec_path, const char *pattern)
{
	unsigned int longest = 0;
	char path[PATH_MAX];
	int dirlen;
	DIR *dir = opendir(exec_path);
	struct dirent *de;

	if (!dir) {
		fprintf(stderr, "git: '%s': %s\n", exec_path, strerror(errno));
		exit(1);
	}

	dirlen = strlen(exec_path);
	if (PATH_MAX - 20 < dirlen) {
		fprintf(stderr, "git: insanely long exec-path '%s'\n",
			exec_path);
		exit(1);
	}

	memcpy(path, exec_path, dirlen);
	path[dirlen++] = '/';

	while ((de = readdir(dir)) != NULL) {
		struct stat st;
		int entlen;

		if (strncmp(de->d_name, "git-", 4))
			continue;
		strcpy(path+dirlen, de->d_name);
		if (stat(path, &st) || /* stat, not lstat */
		    !S_ISREG(st.st_mode) ||
		    !(st.st_mode & S_IXUSR))
			continue;

		entlen = strlen(de->d_name);
		if (4 < entlen && !strcmp(de->d_name + entlen - 4, ".exe"))
			entlen -= 4;

		if (longest < entlen)
			longest = entlen;

		add_cmdname(de->d_name + 4, entlen-4);
	}
	closedir(dir);

	printf("git commands available in '%s'\n", exec_path);
	printf("----------------------------");
	mput_char('-', strlen(exec_path));
	putchar('\n');
	pretty_print_string_list(cmdname, longest - 4);
	putchar('\n');
}

static void list_common_cmds_help(void)
{
	int i, longest = 0;

	for (i = 0; i < ARRAY_SIZE(common_cmds); i++) {
		if (longest < strlen(common_cmds[i].name))
			longest = strlen(common_cmds[i].name);
	}

	puts("The most commonly used git commands are:");
	for (i = 0; i < ARRAY_SIZE(common_cmds); i++) {
		printf("    %s", common_cmds[i].name);
		mput_char(' ', longest - strlen(common_cmds[i].name) + 4);
		puts(common_cmds[i].help);
	}
	puts("(use 'git help -a' to get a list of all installed git commands)");
}

#ifdef __GNUC__
static void cmd_usage(int show_all, const char *exec_path, const char *fmt, ...)
	__attribute__((__format__(__printf__, 3, 4), __noreturn__));
#endif
static void cmd_usage(int show_all, const char *exec_path, const char *fmt, ...)
{
	if (fmt) {
		va_list ap;

		va_start(ap, fmt);
		printf("git: ");
		vprintf(fmt, ap);
		va_end(ap);
		putchar('\n');
	}
	else
		puts(git_usage);

	if (exec_path) {
		putchar('\n');
		if (show_all)
			list_commands(exec_path, "git-*");
		else
			list_common_cmds_help();
        }

	exit(1);
}

static void prepend_to_path(const char *dir, int len)
{
	char *path, *old_path = getenv("PATH");
	int path_len = len;

	if (!old_path)
		old_path = "/usr/local/bin:/usr/bin:/bin";

	path_len = len + strlen(old_path) + 1;

	path = malloc(path_len + 1);

	memcpy(path, dir, len);
	path[len] = ':';
	memcpy(path + len + 1, old_path, path_len - len);

	setenv("PATH", path, 1);
}

static void show_man_page(const char *git_cmd)
{
	const char *page;

	if (!strncmp(git_cmd, "git", 3))
		page = git_cmd;
	else {
		int page_len = strlen(git_cmd) + 4;
		char *p = malloc(page_len + 1);
		strcpy(p, "git-");
		strcpy(p + 4, git_cmd);
		p[page_len] = 0;
		page = p;
	}

	execlp("man", "man", page, NULL);
}

static int cmd_version(int argc, const char **argv, char **envp)
{
	printf("git version %s\n", GIT_VERSION);
	return 0;
}

static int cmd_help(int argc, const char **argv, char **envp)
{
	const char *help_cmd = argv[1];
	if (!help_cmd)
		cmd_usage(0, git_exec_path(), NULL);
	else if (!strcmp(help_cmd, "--all") || !strcmp(help_cmd, "-a"))
		cmd_usage(1, git_exec_path(), NULL);
	else
		show_man_page(help_cmd);
	return 0;
}

static int cmd_format_patch(int argc, const char **argv, char **envp)
{
	struct commit *commit;
	struct commit **list = NULL;
	struct rev_info rev;
	int nr = 0;

	init_revisions(&rev);
	rev.commit_format = CMIT_FMT_EMAIL;
	rev.verbose_header = 1;
	rev.diff = 1;
	rev.diffopt.with_raw = 0;
	rev.diffopt.with_stat = 1;
	rev.combine_merges = 0;
	rev.ignore_merges = 1;
	rev.diffopt.output_format = DIFF_FORMAT_PATCH;
	argc = setup_revisions(argc, argv, &rev, "HEAD");

	prepare_revision_walk(&rev);
	while ((commit = get_revision(&rev)) != NULL) {
		nr++;
		list = realloc(list, nr * sizeof(list[0]));
		list[nr - 1] = commit;
	}
	while (0 <= --nr) {
		int shown;
		commit = list[nr];
		shown = log_tree_commit(&rev, commit);
		free(commit->buffer);
		commit->buffer = NULL;
		if (shown)
			printf("-- \n%s\n\n", GIT_VERSION);
	}
	free(list);
	return 0;
}

static int cmd_log_wc(int argc, const char **argv, char **envp,
		      struct rev_info *rev)
{
	struct commit *commit;

	rev->abbrev = DEFAULT_ABBREV;
	rev->commit_format = CMIT_FMT_DEFAULT;
	rev->verbose_header = 1;
	argc = setup_revisions(argc, argv, rev, "HEAD");

	if (argc > 1)
		die("unrecognized argument: %s", argv[1]);

	prepare_revision_walk(rev);
	setup_pager();
	while ((commit = get_revision(rev)) != NULL) {
		log_tree_commit(rev, commit);
		free(commit->buffer);
		commit->buffer = NULL;
	}
	return 0;
}

static int cmd_wc(int argc, const char **argv, char **envp)
{
	struct rev_info rev;

	init_revisions(&rev);
	rev.diff = 1;
	rev.diffopt.recursive = 1;
	return cmd_log_wc(argc, argv, envp, &rev);
}

static int cmd_show(int argc, const char **argv, char **envp)
{
	struct rev_info rev;

	init_revisions(&rev);
	rev.diff = 1;
	rev.diffopt.recursive = 1;
	rev.combine_merges = 1;
	rev.dense_combined_merges = 1;
	rev.always_show_header = 1;
	rev.ignore_merges = 0;
	rev.no_walk = 1;
	return cmd_log_wc(argc, argv, envp, &rev);
}

static int cmd_log(int argc, const char **argv, char **envp)
{
	struct rev_info rev;

	init_revisions(&rev);
	rev.always_show_header = 1;
	rev.diffopt.recursive = 1;
	rev.combine_merges = 1;
	rev.ignore_merges = 0;
	return cmd_log_wc(argc, argv, envp, &rev);
}

static void handle_internal_command(int argc, const char **argv, char **envp)
{
	const char *cmd = argv[0];
	static struct cmd_struct {
		const char *cmd;
		int (*fn)(int, const char **, char **);
	} commands[] = {
		{ "version", cmd_version },
		{ "help", cmd_help },
		{ "log", cmd_log },
		{ "whatchanged", cmd_wc },
		{ "show", cmd_show },
		{ "fmt-patch", cmd_format_patch },
	};
	int i;

	/* Turn "git cmd --help" into "git help cmd" */
	if (argc > 1 && !strcmp(argv[1], "--help")) {
		argv[1] = argv[0];
		argv[0] = cmd = "help";
	}

	for (i = 0; i < ARRAY_SIZE(commands); i++) {
		struct cmd_struct *p = commands+i;
		if (strcmp(p->cmd, cmd))
			continue;
		exit(p->fn(argc, argv, envp));
	}
}

int main(int argc, const char **argv, char **envp)
{
	const char *cmd = argv[0];
	char *slash = strrchr(cmd, '/');
	char git_command[PATH_MAX + 1];
	const char *exec_path = NULL;

	/*
	 * Take the basename of argv[0] as the command
	 * name, and the dirname as the default exec_path
	 * if it's an absolute path and we don't have
	 * anything better.
	 */
	if (slash) {
		*slash++ = 0;
		if (*cmd == '/')
			exec_path = cmd;
		cmd = slash;
	}

	/*
	 * "git-xxxx" is the same as "git xxxx", but we obviously:
	 *
	 *  - cannot take flags in between the "git" and the "xxxx".
	 *  - cannot execute it externally (since it would just do
	 *    the same thing over again)
	 *
	 * So we just directly call the internal command handler, and
	 * die if that one cannot handle it.
	 */
	if (!strncmp(cmd, "git-", 4)) {
		cmd += 4;
		argv[0] = cmd;
		handle_internal_command(argc, argv, envp);
		die("cannot handle %s internally", cmd);
	}

	/* Default command: "help" */
	cmd = "help";

	/* Look for flags.. */
	while (argc > 1) {
		cmd = *++argv;
		argc--;

		if (strncmp(cmd, "--", 2))
			break;

		cmd += 2;

		/*
		 * For legacy reasons, the "version" and "help"
		 * commands can be written with "--" prepended
		 * to make them look like flags.
		 */
		if (!strcmp(cmd, "help"))
			break;
		if (!strcmp(cmd, "version"))
			break;

		/*
		 * Check remaining flags (which by now must be
		 * "--exec-path", but maybe we will accept
		 * other arguments some day)
		 */
		if (!strncmp(cmd, "exec-path", 9)) {
			cmd += 9;
			if (*cmd == '=') {
				git_set_exec_path(cmd + 1);
				continue;
			}
			puts(git_exec_path());
			exit(0);
		}
		cmd_usage(0, NULL, NULL);
	}
	argv[0] = cmd;

	/*
	 * We search for git commands in the following order:
	 *  - git_exec_path()
	 *  - the path of the "git" command if we could find it
	 *    in $0
	 *  - the regular PATH.
	 */
	if (exec_path)
		prepend_to_path(exec_path, strlen(exec_path));
	exec_path = git_exec_path();
	prepend_to_path(exec_path, strlen(exec_path));

	/* See if it's an internal command */
	handle_internal_command(argc, argv, envp);

	/* .. then try the external ones */
	execv_git_cmd(argv);

	if (errno == ENOENT)
		cmd_usage(0, exec_path, "'%s' is not a git-command", cmd);

	fprintf(stderr, "Failed to run command '%s': %s\n",
		git_command, strerror(errno));

	return 1;
}
