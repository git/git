/*
 * Builtin help command
 */
#include "cache.h"
#include "builtin.h"
#include "exec_cmd.h"
#include "parse-options.h"
#include "run-command.h"
#include "column.h"
#include "help.h"

#ifndef DEFAULT_HELP_FORMAT
#define DEFAULT_HELP_FORMAT "man"
#endif

static struct man_viewer_list {
	struct man_viewer_list *next;
	char name[FLEX_ARRAY];
} *man_viewer_list;

static struct man_viewer_info_list {
	struct man_viewer_info_list *next;
	const char *info;
	char name[FLEX_ARRAY];
} *man_viewer_info_list;

enum help_format {
	HELP_FORMAT_NONE,
	HELP_FORMAT_MAN,
	HELP_FORMAT_INFO,
	HELP_FORMAT_WEB
};

static const char *html_path;

static int show_all = 0;
static int show_guides = 0;
static unsigned int colopts;
static enum help_format help_format = HELP_FORMAT_NONE;
static int exclude_guides;
static struct option builtin_help_options[] = {
	OPT_BOOL('a', "all", &show_all, N_("print all available commands")),
	OPT_HIDDEN_BOOL(0, "exclude-guides", &exclude_guides, N_("exclude guides")),
	OPT_BOOL('g', "guides", &show_guides, N_("print list of useful guides")),
	OPT_SET_INT('m', "man", &help_format, N_("show man page"), HELP_FORMAT_MAN),
	OPT_SET_INT('w', "web", &help_format, N_("show manual in web browser"),
			HELP_FORMAT_WEB),
	OPT_SET_INT('i', "info", &help_format, N_("show info page"),
			HELP_FORMAT_INFO),
	OPT_END(),
};

static const char * const builtin_help_usage[] = {
	N_("git help [--all] [--guides] [--man | --web | --info] [<command>]"),
	NULL
};

static enum help_format parse_help_format(const char *format)
{
	if (!strcmp(format, "man"))
		return HELP_FORMAT_MAN;
	if (!strcmp(format, "info"))
		return HELP_FORMAT_INFO;
	if (!strcmp(format, "web") || !strcmp(format, "html"))
		return HELP_FORMAT_WEB;
	die(_("unrecognized help format '%s'"), format);
}

static const char *get_man_viewer_info(const char *name)
{
	struct man_viewer_info_list *viewer;

	for (viewer = man_viewer_info_list; viewer; viewer = viewer->next)
	{
		if (!strcasecmp(name, viewer->name))
			return viewer->info;
	}
	return NULL;
}

static int check_emacsclient_version(void)
{
	struct strbuf buffer = STRBUF_INIT;
	struct child_process ec_process = CHILD_PROCESS_INIT;
	const char *argv_ec[] = { "emacsclient", "--version", NULL };
	int version;

	/* emacsclient prints its version number on stderr */
	ec_process.argv = argv_ec;
	ec_process.err = -1;
	ec_process.stdout_to_stderr = 1;
	if (start_command(&ec_process))
		return error(_("Failed to start emacsclient."));

	strbuf_read(&buffer, ec_process.err, 20);
	close(ec_process.err);

	/*
	 * Don't bother checking return value, because "emacsclient --version"
	 * seems to always exits with code 1.
	 */
	finish_command(&ec_process);

	if (!starts_with(buffer.buf, "emacsclient")) {
		strbuf_release(&buffer);
		return error(_("Failed to parse emacsclient version."));
	}

	strbuf_remove(&buffer, 0, strlen("emacsclient"));
	version = atoi(buffer.buf);

	if (version < 22) {
		strbuf_release(&buffer);
		return error(_("emacsclient version '%d' too old (< 22)."),
			version);
	}

	strbuf_release(&buffer);
	return 0;
}

static void exec_woman_emacs(const char *path, const char *page)
{
	if (!check_emacsclient_version()) {
		/* This works only with emacsclient version >= 22. */
		struct strbuf man_page = STRBUF_INIT;

		if (!path)
			path = "emacsclient";
		strbuf_addf(&man_page, "(woman \"%s\")", page);
		execlp(path, "emacsclient", "-e", man_page.buf, (char *)NULL);
		warning_errno(_("failed to exec '%s'"), path);
	}
}

static void exec_man_konqueror(const char *path, const char *page)
{
	const char *display = getenv("DISPLAY");
	if (display && *display) {
		struct strbuf man_page = STRBUF_INIT;
		const char *filename = "kfmclient";

		/* It's simpler to launch konqueror using kfmclient. */
		if (path) {
			size_t len;
			if (strip_suffix(path, "/konqueror", &len))
				path = xstrfmt("%.*s/kfmclient", (int)len, path);
			filename = basename((char *)path);
		} else
			path = "kfmclient";
		strbuf_addf(&man_page, "man:%s(1)", page);
		execlp(path, filename, "newTab", man_page.buf, (char *)NULL);
		warning_errno(_("failed to exec '%s'"), path);
	}
}

static void exec_man_man(const char *path, const char *page)
{
	if (!path)
		path = "man";
	execlp(path, "man", page, (char *)NULL);
	warning_errno(_("failed to exec '%s'"), path);
}

static void exec_man_cmd(const char *cmd, const char *page)
{
	struct strbuf shell_cmd = STRBUF_INIT;
	strbuf_addf(&shell_cmd, "%s %s", cmd, page);
	execl(SHELL_PATH, SHELL_PATH, "-c", shell_cmd.buf, (char *)NULL);
	warning(_("failed to exec '%s'"), cmd);
}

static void add_man_viewer(const char *name)
{
	struct man_viewer_list **p = &man_viewer_list;

	while (*p)
		p = &((*p)->next);
	FLEX_ALLOC_STR(*p, name, name);
}

static int supported_man_viewer(const char *name, size_t len)
{
	return (!strncasecmp("man", name, len) ||
		!strncasecmp("woman", name, len) ||
		!strncasecmp("konqueror", name, len));
}

static void do_add_man_viewer_info(const char *name,
				   size_t len,
				   const char *value)
{
	struct man_viewer_info_list *new;
	FLEX_ALLOC_MEM(new, name, name, len);
	new->info = xstrdup(value);
	new->next = man_viewer_info_list;
	man_viewer_info_list = new;
}

static int add_man_viewer_path(const char *name,
			       size_t len,
			       const char *value)
{
	if (supported_man_viewer(name, len))
		do_add_man_viewer_info(name, len, value);
	else
		warning(_("'%s': path for unsupported man viewer.\n"
			  "Please consider using 'man.<tool>.cmd' instead."),
			name);

	return 0;
}

static int add_man_viewer_cmd(const char *name,
			      size_t len,
			      const char *value)
{
	if (supported_man_viewer(name, len))
		warning(_("'%s': cmd for supported man viewer.\n"
			  "Please consider using 'man.<tool>.path' instead."),
			name);
	else
		do_add_man_viewer_info(name, len, value);

	return 0;
}

static int add_man_viewer_info(const char *var, const char *value)
{
	const char *name, *subkey;
	int namelen;

	if (parse_config_key(var, "man", &name, &namelen, &subkey) < 0 || !name)
		return 0;

	if (!strcmp(subkey, "path")) {
		if (!value)
			return config_error_nonbool(var);
		return add_man_viewer_path(name, namelen, value);
	}
	if (!strcmp(subkey, "cmd")) {
		if (!value)
			return config_error_nonbool(var);
		return add_man_viewer_cmd(name, namelen, value);
	}

	return 0;
}

static int git_help_config(const char *var, const char *value, void *cb)
{
	if (starts_with(var, "column."))
		return git_column_config(var, value, "help", &colopts);
	if (!strcmp(var, "help.format")) {
		if (!value)
			return config_error_nonbool(var);
		help_format = parse_help_format(value);
		return 0;
	}
	if (!strcmp(var, "help.htmlpath")) {
		if (!value)
			return config_error_nonbool(var);
		html_path = xstrdup(value);
		return 0;
	}
	if (!strcmp(var, "man.viewer")) {
		if (!value)
			return config_error_nonbool(var);
		add_man_viewer(value);
		return 0;
	}
	if (starts_with(var, "man."))
		return add_man_viewer_info(var, value);

	return git_default_config(var, value, cb);
}

static struct cmdnames main_cmds, other_cmds;

static int is_git_command(const char *s)
{
	if (is_builtin(s))
		return 1;

	load_command_list("git-", &main_cmds, &other_cmds);
	return is_in_cmdlist(&main_cmds, s) ||
		is_in_cmdlist(&other_cmds, s);
}

static const char *cmd_to_page(const char *git_cmd)
{
	if (!git_cmd)
		return "git";
	else if (starts_with(git_cmd, "git"))
		return git_cmd;
	else if (is_git_command(git_cmd))
		return xstrfmt("git-%s", git_cmd);
	else
		return xstrfmt("git%s", git_cmd);
}

static void setup_man_path(void)
{
	struct strbuf new_path = STRBUF_INIT;
	const char *old_path = getenv("MANPATH");
	char *git_man_path = system_path(GIT_MAN_PATH);

	/* We should always put ':' after our path. If there is no
	 * old_path, the ':' at the end will let 'man' to try
	 * system-wide paths after ours to find the manual page. If
	 * there is old_path, we need ':' as delimiter. */
	strbuf_addstr(&new_path, git_man_path);
	strbuf_addch(&new_path, ':');
	if (old_path)
		strbuf_addstr(&new_path, old_path);

	free(git_man_path);
	setenv("MANPATH", new_path.buf, 1);

	strbuf_release(&new_path);
}

static void exec_viewer(const char *name, const char *page)
{
	const char *info = get_man_viewer_info(name);

	if (!strcasecmp(name, "man"))
		exec_man_man(info, page);
	else if (!strcasecmp(name, "woman"))
		exec_woman_emacs(info, page);
	else if (!strcasecmp(name, "konqueror"))
		exec_man_konqueror(info, page);
	else if (info)
		exec_man_cmd(info, page);
	else
		warning(_("'%s': unknown man viewer."), name);
}

static void show_man_page(const char *git_cmd)
{
	struct man_viewer_list *viewer;
	const char *page = cmd_to_page(git_cmd);
	const char *fallback = getenv("GIT_MAN_VIEWER");

	setup_man_path();
	for (viewer = man_viewer_list; viewer; viewer = viewer->next)
	{
		exec_viewer(viewer->name, page); /* will return when unable */
	}
	if (fallback)
		exec_viewer(fallback, page);
	exec_viewer("man", page);
	die(_("no man viewer handled the request"));
}

static void show_info_page(const char *git_cmd)
{
	const char *page = cmd_to_page(git_cmd);
	setenv("INFOPATH", system_path(GIT_INFO_PATH), 1);
	execlp("info", "info", "gitman", page, (char *)NULL);
	die(_("no info viewer handled the request"));
}

static void get_html_page_path(struct strbuf *page_path, const char *page)
{
	struct stat st;
	char *to_free = NULL;

	if (!html_path)
		html_path = to_free = system_path(GIT_HTML_PATH);

	/* Check that we have a git documentation directory. */
	if (!strstr(html_path, "://")) {
		if (stat(mkpath("%s/git.html", html_path), &st)
		    || !S_ISREG(st.st_mode))
			die("'%s': not a documentation directory.", html_path);
	}

	strbuf_init(page_path, 0);
	strbuf_addf(page_path, "%s/%s.html", html_path, page);
	free(to_free);
}

static void open_html(const char *path)
{
	execl_git_cmd("web--browse", "-c", "help.browser", path, (char *)NULL);
}

static void show_html_page(const char *git_cmd)
{
	const char *page = cmd_to_page(git_cmd);
	struct strbuf page_path; /* it leaks but we exec bellow */

	get_html_page_path(&page_path, page);

	open_html(page_path.buf);
}

static struct {
	const char *name;
	const char *help;
} common_guides[] = {
	{ "attributes", N_("Defining attributes per path") },
	{ "everyday", N_("Everyday Git With 20 Commands Or So") },
	{ "glossary", N_("A Git glossary") },
	{ "ignore", N_("Specifies intentionally untracked files to ignore") },
	{ "modules", N_("Defining submodule properties") },
	{ "revisions", N_("Specifying revisions and ranges for Git") },
	{ "tutorial", N_("A tutorial introduction to Git (for version 1.5.1 or newer)") },
	{ "workflows", N_("An overview of recommended workflows with Git") },
};

static void list_common_guides_help(void)
{
	int i, longest = 0;

	for (i = 0; i < ARRAY_SIZE(common_guides); i++) {
		if (longest < strlen(common_guides[i].name))
			longest = strlen(common_guides[i].name);
	}

	puts(_("The common Git guides are:\n"));
	for (i = 0; i < ARRAY_SIZE(common_guides); i++) {
		printf("   %s   ", common_guides[i].name);
		mput_char(' ', longest - strlen(common_guides[i].name));
		puts(_(common_guides[i].help));
	}
	putchar('\n');
}

static const char *check_git_cmd(const char* cmd)
{
	char *alias;

	if (is_git_command(cmd))
		return cmd;

	alias = alias_lookup(cmd);
	if (alias) {
		printf_ln(_("`git %s' is aliased to `%s'"), cmd, alias);
		free(alias);
		exit(0);
	}

	if (exclude_guides)
		return help_unknown_cmd(cmd);

	return cmd;
}

int cmd_help(int argc, const char **argv, const char *prefix)
{
	int nongit;
	enum help_format parsed_help_format;

	argc = parse_options(argc, argv, prefix, builtin_help_options,
			builtin_help_usage, 0);
	parsed_help_format = help_format;

	if (show_all) {
		git_config(git_help_config, NULL);
		printf(_("usage: %s%s"), _(git_usage_string), "\n\n");
		load_command_list("git-", &main_cmds, &other_cmds);
		list_commands(colopts, &main_cmds, &other_cmds);
	}

	if (show_guides)
		list_common_guides_help();

	if (show_all || show_guides) {
		printf("%s\n", _(git_more_info_string));
		/*
		* We're done. Ignore any remaining args
		*/
		return 0;
	}

	if (!argv[0]) {
		printf(_("usage: %s%s"), _(git_usage_string), "\n\n");
		list_common_cmds_help();
		printf("\n%s\n", _(git_more_info_string));
		return 0;
	}

	setup_git_directory_gently(&nongit);
	git_config(git_help_config, NULL);

	if (parsed_help_format != HELP_FORMAT_NONE)
		help_format = parsed_help_format;
	if (help_format == HELP_FORMAT_NONE)
		help_format = parse_help_format(DEFAULT_HELP_FORMAT);

	argv[0] = check_git_cmd(argv[0]);

	switch (help_format) {
	case HELP_FORMAT_NONE:
	case HELP_FORMAT_MAN:
		show_man_page(argv[0]);
		break;
	case HELP_FORMAT_INFO:
		show_info_page(argv[0]);
		break;
	case HELP_FORMAT_WEB:
		show_html_page(argv[0]);
		break;
	}

	return 0;
}
