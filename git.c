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

#include "cache.h"
#include "commit.h"
#include "revision.h"

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

#ifdef __GNUC__
static void cmd_usage(const char *exec_path, const char *fmt, ...)
	__attribute__((__format__(__printf__, 2, 3), __noreturn__));
#endif
static void cmd_usage(const char *exec_path, const char *fmt, ...)
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

	putchar('\n');

	if(exec_path)
		list_commands(exec_path, "git-*");

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

static void show_man_page(char *git_cmd)
{
	char *page;

	if (!strncmp(git_cmd, "git", 3))
		page = git_cmd;
	else {
		int page_len = strlen(git_cmd) + 4;

		page = malloc(page_len + 1);
		strcpy(page, "git-");
		strcpy(page + 4, git_cmd);
		page[page_len] = 0;
	}

	execlp("man", "man", page, NULL);
}

static int cmd_version(int argc, char **argv, char **envp)
{
	printf("git version %s\n", GIT_VERSION);
	return 0;
}

static int cmd_help(int argc, char **argv, char **envp)
{
	char *help_cmd = argv[1];
	if (!help_cmd)
		cmd_usage(git_exec_path(), NULL);
	show_man_page(help_cmd);
	return 0;
}

#define LOGSIZE (65536)

static int cmd_log(int argc, char **argv, char **envp)
{
	struct rev_info rev;
	struct commit *commit;
	char *buf = xmalloc(LOGSIZE);
	static enum cmit_fmt commit_format = CMIT_FMT_DEFAULT;
	int abbrev = DEFAULT_ABBREV;
	int show_parents = 0;
	const char *commit_prefix = "commit ";

	argc = setup_revisions(argc, argv, &rev, "HEAD");
	while (1 < argc) {
		char *arg = argv[1];
		/* accept -<digit>, like traditilnal "head" */
		if ((*arg == '-') && isdigit(arg[1])) {
			rev.max_count = atoi(arg + 1);
		}
		else if (!strcmp(arg, "-n")) {
			if (argc < 2)
				die("-n requires an argument");
			rev.max_count = atoi(argv[2]);
			argc--; argv++;
		}
		else if (!strncmp(arg,"-n",2)) {
			rev.max_count = atoi(arg + 2);
		}
		else if (!strncmp(arg, "--pretty", 8)) {
			commit_format = get_commit_format(arg + 8);
			if (commit_format == CMIT_FMT_ONELINE)
				commit_prefix = "";
		}
		else if (!strcmp(arg, "--parents")) {
			show_parents = 1;
		}
		else if (!strcmp(arg, "--no-abbrev")) {
			abbrev = 0;
		}
		else if (!strncmp(arg, "--abbrev=", 9)) {
			abbrev = strtoul(arg + 9, NULL, 10);
			if (abbrev && abbrev < MINIMUM_ABBREV)
				abbrev = MINIMUM_ABBREV;
			else if (40 < abbrev)
				abbrev = 40;
		}
		else
			die("unrecognized argument: %s", arg);
		argc--; argv++;
	}

	prepare_revision_walk(&rev);
	setup_pager();
	while ((commit = get_revision(&rev)) != NULL) {
		printf("%s%s", commit_prefix,
		       sha1_to_hex(commit->object.sha1));
		if (show_parents) {
			struct commit_list *parents = commit->parents;
			while (parents) {
				struct object *o = &(parents->item->object);
				parents = parents->next;
				if (o->flags & TMP_MARK)
					continue;
				printf(" %s", sha1_to_hex(o->sha1));
				o->flags |= TMP_MARK;
			}
			/* TMP_MARK is a general purpose flag that can
			 * be used locally, but the user should clean
			 * things up after it is done with them.
			 */
			for (parents = commit->parents;
			     parents;
			     parents = parents->next)
				parents->item->object.flags &= ~TMP_MARK;
		}
		if (commit_format == CMIT_FMT_ONELINE)
			putchar(' ');
		else
			putchar('\n');
		pretty_print_commit(commit_format, commit, ~0, buf,
				    LOGSIZE, abbrev);
		printf("%s\n", buf);
	}
	free(buf);
	return 0;
}

#define ARRAY_SIZE(x) (sizeof(x)/sizeof(x[0]))

static void handle_internal_command(int argc, char **argv, char **envp)
{
	const char *cmd = argv[0];
	static struct cmd_struct {
		const char *cmd;
		int (*fn)(int, char **, char **);
	} commands[] = {
		{ "version", cmd_version },
		{ "help", cmd_help },
		{ "log", cmd_log },
	};
	int i;

	for (i = 0; i < ARRAY_SIZE(commands); i++) {
		struct cmd_struct *p = commands+i;
		if (strcmp(p->cmd, cmd))
			continue;
		exit(p->fn(argc, argv, envp));
	}
}

int main(int argc, char **argv, char **envp)
{
	char *cmd = argv[0];
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
		cmd_usage(NULL, NULL);
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
		cmd_usage(exec_path, "'%s' is not a git-command", cmd);

	fprintf(stderr, "Failed to run command '%s': %s\n",
		git_command, strerror(errno));

	return 1;
}
