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

int main(int argc, char **argv, char **envp)
{
	char git_command[PATH_MAX + 1];
	char wd[PATH_MAX + 1];
	int i, len, show_help = 0;
	char *exec_path = getenv("GIT_EXEC_PATH");

	getcwd(wd, PATH_MAX);

	if (!exec_path)
		exec_path = GIT_EXEC_PATH;

	for (i = 1; i < argc; i++) {
		char *arg = argv[i];

		if (!strcmp(arg, "help")) {
			show_help = 1;
			continue;
		}

		if (strncmp(arg, "--", 2))
			break;

		arg += 2;

		if (!strncmp(arg, "exec-path", 9)) {
			arg += 9;
			if (*arg == '=')
				exec_path = arg + 1;
			else {
				puts(exec_path);
				exit(0);
			}
		}
		else if (!strcmp(arg, "version")) {
			printf("git version %s\n", GIT_VERSION);
			exit(0);
		}
		else if (!strcmp(arg, "help"))
			show_help = 1;
		else if (!show_help)
			cmd_usage(NULL, NULL);
	}

	if (i >= argc || show_help) {
		if (i >= argc)
			cmd_usage(exec_path, NULL);

		show_man_page(argv[i]);
	}

	if (*exec_path != '/') {
		if (!getcwd(git_command, sizeof(git_command))) {
			fprintf(stderr,
				"git: cannot determine current directory\n");
			exit(1);
		}
		len = strlen(git_command);

		/* Trivial cleanup */
		while (!strncmp(exec_path, "./", 2)) {
			exec_path += 2;
			while (*exec_path == '/')
				exec_path++;
		}
		snprintf(git_command + len, sizeof(git_command) - len,
			 "/%s", exec_path);
	}
	else
		strcpy(git_command, exec_path);
	len = strlen(git_command);
	prepend_to_path(git_command, len);

	len += snprintf(git_command + len, sizeof(git_command) - len,
			"/git-%s", argv[i]);
	if (sizeof(git_command) <= len) {
		fprintf(stderr, "git: command name given is too long.\n");
		exit(1);
	}

	/* execve() can only ever return if it fails */
	execve(git_command, &argv[i], envp);

	if (errno == ENOENT)
		cmd_usage(exec_path, "'%s' is not a git-command", argv[i]);

	fprintf(stderr, "Failed to run command '%s': %s\n",
		git_command, strerror(errno));

	return 1;
}
