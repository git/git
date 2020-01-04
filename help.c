#include "cache.h"
#include "config.h"
#include "builtin.h"
#include "exec-cmd.h"
#include "run-command.h"
#include "levenshtein.h"
#include "help.h"
#include "command-list.h"
#include "string-list.h"
#include "column.h"
#include "version.h"
#include "refs.h"
#include "parse-options.h"

struct category_description {
	uint32_t category;
	const char *desc;
};
static uint32_t common_mask =
	CAT_init | CAT_worktree | CAT_info |
	CAT_history | CAT_remote;
static struct category_description common_categories[] = {
	{ CAT_init, N_("start a working area (see also: git help tutorial)") },
	{ CAT_worktree, N_("work on the current change (see also: git help everyday)") },
	{ CAT_info, N_("examine the history and state (see also: git help revisions)") },
	{ CAT_history, N_("grow, mark and tweak your common history") },
	{ CAT_remote, N_("collaborate (see also: git help workflows)") },
	{ 0, NULL }
};
static struct category_description main_categories[] = {
	{ CAT_mainporcelain, N_("Main Porcelain Commands") },
	{ CAT_ancillarymanipulators, N_("Ancillary Commands / Manipulators") },
	{ CAT_ancillaryinterrogators, N_("Ancillary Commands / Interrogators") },
	{ CAT_foreignscminterface, N_("Interacting with Others") },
	{ CAT_plumbingmanipulators, N_("Low-level Commands / Manipulators") },
	{ CAT_plumbinginterrogators, N_("Low-level Commands / Interrogators") },
	{ CAT_synchingrepositories, N_("Low-level Commands / Syncing Repositories") },
	{ CAT_purehelpers, N_("Low-level Commands / Internal Helpers") },
	{ 0, NULL }
};

static const char *drop_prefix(const char *name, uint32_t category)
{
	const char *new_name;

	if (skip_prefix(name, "git-", &new_name))
		return new_name;
	if (category == CAT_guide && skip_prefix(name, "git", &new_name))
		return new_name;
	return name;

}

static void extract_cmds(struct cmdname_help **p_cmds, uint32_t mask)
{
	int i, nr = 0;
	struct cmdname_help *cmds;

	if (ARRAY_SIZE(command_list) == 0)
		BUG("empty command_list[] is a sign of broken generate-cmdlist.sh");

	ALLOC_ARRAY(cmds, ARRAY_SIZE(command_list) + 1);

	for (i = 0; i < ARRAY_SIZE(command_list); i++) {
		const struct cmdname_help *cmd = command_list + i;

		if (!(cmd->category & mask))
			continue;

		cmds[nr] = *cmd;
		cmds[nr].name = drop_prefix(cmd->name, cmd->category);

		nr++;
	}
	cmds[nr].name = NULL;
	*p_cmds = cmds;
}

static void print_command_list(const struct cmdname_help *cmds,
			       uint32_t mask, int longest)
{
	int i;

	for (i = 0; cmds[i].name; i++) {
		if (cmds[i].category & mask) {
			size_t len = strlen(cmds[i].name);
			printf("   %s   ", cmds[i].name);
			if (longest > len)
				mput_char(' ', longest - len);
			puts(_(cmds[i].help));
		}
	}
}

static int cmd_name_cmp(const void *elem1, const void *elem2)
{
	const struct cmdname_help *e1 = elem1;
	const struct cmdname_help *e2 = elem2;

	return strcmp(e1->name, e2->name);
}

static void print_cmd_by_category(const struct category_description *catdesc,
				  int *longest_p)
{
	struct cmdname_help *cmds;
	int longest = 0;
	int i, nr = 0;
	uint32_t mask = 0;

	for (i = 0; catdesc[i].desc; i++)
		mask |= catdesc[i].category;

	extract_cmds(&cmds, mask);

	for (i = 0; cmds[i].name; i++, nr++) {
		if (longest < strlen(cmds[i].name))
			longest = strlen(cmds[i].name);
	}
	QSORT(cmds, nr, cmd_name_cmp);

	for (i = 0; catdesc[i].desc; i++) {
		uint32_t mask = catdesc[i].category;
		const char *desc = catdesc[i].desc;

		printf("\n%s\n", _(desc));
		print_command_list(cmds, mask, longest);
	}
	free(cmds);
	if (longest_p)
		*longest_p = longest;
}

void add_cmdname(struct cmdnames *cmds, const char *name, int len)
{
	struct cmdname *ent;
	FLEX_ALLOC_MEM(ent, name, name, len);
	ent->len = len;

	ALLOC_GROW(cmds->names, cmds->cnt + 1, cmds->alloc);
	cmds->names[cmds->cnt++] = ent;
}

static void clean_cmdnames(struct cmdnames *cmds)
{
	int i;
	for (i = 0; i < cmds->cnt; ++i)
		free(cmds->names[i]);
	free(cmds->names);
	cmds->cnt = 0;
	cmds->alloc = 0;
}

static int cmdname_compare(const void *a_, const void *b_)
{
	struct cmdname *a = *(struct cmdname **)a_;
	struct cmdname *b = *(struct cmdname **)b_;
	return strcmp(a->name, b->name);
}

static void uniq(struct cmdnames *cmds)
{
	int i, j;

	if (!cmds->cnt)
		return;

	for (i = j = 1; i < cmds->cnt; i++) {
		if (!strcmp(cmds->names[i]->name, cmds->names[j-1]->name))
			free(cmds->names[i]);
		else
			cmds->names[j++] = cmds->names[i];
	}

	cmds->cnt = j;
}

void exclude_cmds(struct cmdnames *cmds, struct cmdnames *excludes)
{
	int ci, cj, ei;
	int cmp;

	ci = cj = ei = 0;
	while (ci < cmds->cnt && ei < excludes->cnt) {
		cmp = strcmp(cmds->names[ci]->name, excludes->names[ei]->name);
		if (cmp < 0)
			cmds->names[cj++] = cmds->names[ci++];
		else if (cmp == 0) {
			ei++;
			free(cmds->names[ci++]);
		} else if (cmp > 0)
			ei++;
	}

	while (ci < cmds->cnt)
		cmds->names[cj++] = cmds->names[ci++];

	cmds->cnt = cj;
}

static void pretty_print_cmdnames(struct cmdnames *cmds, unsigned int colopts)
{
	struct string_list list = STRING_LIST_INIT_NODUP;
	struct column_options copts;
	int i;

	for (i = 0; i < cmds->cnt; i++)
		string_list_append(&list, cmds->names[i]->name);
	/*
	 * always enable column display, we only consult column.*
	 * about layout strategy and stuff
	 */
	colopts = (colopts & ~COL_ENABLE_MASK) | COL_ENABLED;
	memset(&copts, 0, sizeof(copts));
	copts.indent = "  ";
	copts.padding = 2;
	print_columns(&list, colopts, &copts);
	string_list_clear(&list, 0);
}

static void list_commands_in_dir(struct cmdnames *cmds,
					 const char *path,
					 const char *prefix)
{
	DIR *dir = opendir(path);
	struct dirent *de;
	struct strbuf buf = STRBUF_INIT;
	int len;

	if (!dir)
		return;
	if (!prefix)
		prefix = "git-";

	strbuf_addf(&buf, "%s/", path);
	len = buf.len;

	while ((de = readdir(dir)) != NULL) {
		const char *ent;
		size_t entlen;

		if (!skip_prefix(de->d_name, prefix, &ent))
			continue;

		strbuf_setlen(&buf, len);
		strbuf_addstr(&buf, de->d_name);
		if (!is_executable(buf.buf))
			continue;

		entlen = strlen(ent);
		strip_suffix(ent, ".exe", &entlen);

		add_cmdname(cmds, ent, entlen);
	}
	closedir(dir);
	strbuf_release(&buf);
}

void load_command_list(const char *prefix,
		struct cmdnames *main_cmds,
		struct cmdnames *other_cmds)
{
	const char *env_path = getenv("PATH");
	const char *exec_path = git_exec_path();

	if (exec_path) {
		list_commands_in_dir(main_cmds, exec_path, prefix);
		QSORT(main_cmds->names, main_cmds->cnt, cmdname_compare);
		uniq(main_cmds);
	}

	if (env_path) {
		char *paths, *path, *colon;
		path = paths = xstrdup(env_path);
		while (1) {
			if ((colon = strchr(path, PATH_SEP)))
				*colon = 0;
			if (!exec_path || strcmp(path, exec_path))
				list_commands_in_dir(other_cmds, path, prefix);

			if (!colon)
				break;
			path = colon + 1;
		}
		free(paths);

		QSORT(other_cmds->names, other_cmds->cnt, cmdname_compare);
		uniq(other_cmds);
	}
	exclude_cmds(other_cmds, main_cmds);
}

void list_commands(unsigned int colopts,
		   struct cmdnames *main_cmds, struct cmdnames *other_cmds)
{
	if (main_cmds->cnt) {
		const char *exec_path = git_exec_path();
		printf_ln(_("available git commands in '%s'"), exec_path);
		putchar('\n');
		pretty_print_cmdnames(main_cmds, colopts);
		putchar('\n');
	}

	if (other_cmds->cnt) {
		printf_ln(_("git commands available from elsewhere on your $PATH"));
		putchar('\n');
		pretty_print_cmdnames(other_cmds, colopts);
		putchar('\n');
	}
}

void list_common_cmds_help(void)
{
	puts(_("These are common Git commands used in various situations:"));
	print_cmd_by_category(common_categories, NULL);
}

void list_all_main_cmds(struct string_list *list)
{
	struct cmdnames main_cmds, other_cmds;
	int i;

	memset(&main_cmds, 0, sizeof(main_cmds));
	memset(&other_cmds, 0, sizeof(other_cmds));
	load_command_list("git-", &main_cmds, &other_cmds);

	for (i = 0; i < main_cmds.cnt; i++)
		string_list_append(list, main_cmds.names[i]->name);

	clean_cmdnames(&main_cmds);
	clean_cmdnames(&other_cmds);
}

void list_all_other_cmds(struct string_list *list)
{
	struct cmdnames main_cmds, other_cmds;
	int i;

	memset(&main_cmds, 0, sizeof(main_cmds));
	memset(&other_cmds, 0, sizeof(other_cmds));
	load_command_list("git-", &main_cmds, &other_cmds);

	for (i = 0; i < other_cmds.cnt; i++)
		string_list_append(list, other_cmds.names[i]->name);

	clean_cmdnames(&main_cmds);
	clean_cmdnames(&other_cmds);
}

void list_cmds_by_category(struct string_list *list,
			   const char *cat)
{
	int i, n = ARRAY_SIZE(command_list);
	uint32_t cat_id = 0;

	for (i = 0; category_names[i]; i++) {
		if (!strcmp(cat, category_names[i])) {
			cat_id = 1UL << i;
			break;
		}
	}
	if (!cat_id)
		die(_("unsupported command listing type '%s'"), cat);

	for (i = 0; i < n; i++) {
		struct cmdname_help *cmd = command_list + i;

		if (!(cmd->category & cat_id))
			continue;
		string_list_append(list, drop_prefix(cmd->name, cmd->category));
	}
}

void list_cmds_by_config(struct string_list *list)
{
	const char *cmd_list;

	if (git_config_get_string_const("completion.commands", &cmd_list))
		return;

	string_list_sort(list);
	string_list_remove_duplicates(list, 0);

	while (*cmd_list) {
		struct strbuf sb = STRBUF_INIT;
		const char *p = strchrnul(cmd_list, ' ');

		strbuf_add(&sb, cmd_list, p - cmd_list);
		if (sb.buf[0] == '-')
			string_list_remove(list, sb.buf + 1, 0);
		else
			string_list_insert(list, sb.buf);
		strbuf_release(&sb);
		while (*p == ' ')
			p++;
		cmd_list = p;
	}
}

void list_common_guides_help(void)
{
	struct category_description catdesc[] = {
		{ CAT_guide, N_("The common Git guides are:") },
		{ 0, NULL }
	};
	print_cmd_by_category(catdesc, NULL);
	putchar('\n');
}

struct slot_expansion {
	const char *prefix;
	const char *placeholder;
	void (*fn)(struct string_list *list, const char *prefix);
	int found;
};

void list_config_help(int for_human)
{
	struct slot_expansion slot_expansions[] = {
		{ "advice", "*", list_config_advices },
		{ "color.branch", "<slot>", list_config_color_branch_slots },
		{ "color.decorate", "<slot>", list_config_color_decorate_slots },
		{ "color.diff", "<slot>", list_config_color_diff_slots },
		{ "color.grep", "<slot>", list_config_color_grep_slots },
		{ "color.interactive", "<slot>", list_config_color_interactive_slots },
		{ "color.remote", "<slot>", list_config_color_sideband_slots },
		{ "color.status", "<slot>", list_config_color_status_slots },
		{ "fsck", "<msg-id>", list_config_fsck_msg_ids },
		{ "receive.fsck", "<msg-id>", list_config_fsck_msg_ids },
		{ NULL, NULL, NULL }
	};
	const char **p;
	struct slot_expansion *e;
	struct string_list keys = STRING_LIST_INIT_DUP;
	int i;

	for (p = config_name_list; *p; p++) {
		const char *var = *p;
		struct strbuf sb = STRBUF_INIT;

		for (e = slot_expansions; e->prefix; e++) {

			strbuf_reset(&sb);
			strbuf_addf(&sb, "%s.%s", e->prefix, e->placeholder);
			if (!strcasecmp(var, sb.buf)) {
				e->fn(&keys, e->prefix);
				e->found++;
				break;
			}
		}
		strbuf_release(&sb);
		if (!e->prefix)
			string_list_append(&keys, var);
	}

	for (e = slot_expansions; e->prefix; e++)
		if (!e->found)
			BUG("slot_expansion %s.%s is not used",
			    e->prefix, e->placeholder);

	string_list_sort(&keys);
	for (i = 0; i < keys.nr; i++) {
		const char *var = keys.items[i].string;
		const char *wildcard, *tag, *cut;

		if (for_human) {
			puts(var);
			continue;
		}

		wildcard = strchr(var, '*');
		tag = strchr(var, '<');

		if (!wildcard && !tag) {
			puts(var);
			continue;
		}

		if (wildcard && !tag)
			cut = wildcard;
		else if (!wildcard && tag)
			cut = tag;
		else
			cut = wildcard < tag ? wildcard : tag;

		/*
		 * We may produce duplicates, but that's up to
		 * git-completion.bash to handle
		 */
		printf("%.*s\n", (int)(cut - var), var);
	}
	string_list_clear(&keys, 0);
}

static int get_alias(const char *var, const char *value, void *data)
{
	struct string_list *list = data;

	if (skip_prefix(var, "alias.", &var))
		string_list_append(list, var)->util = xstrdup(value);

	return 0;
}

void list_all_cmds_help(void)
{
	struct string_list others = STRING_LIST_INIT_DUP;
	struct string_list alias_list = STRING_LIST_INIT_DUP;
	struct cmdname_help *aliases;
	int i, longest;

	printf_ln(_("See 'git help <command>' to read about a specific subcommand"));
	print_cmd_by_category(main_categories, &longest);

	list_all_other_cmds(&others);
	if (others.nr)
		printf("\n%s\n", _("External commands"));
	for (i = 0; i < others.nr; i++)
		printf("   %s\n", others.items[i].string);
	string_list_clear(&others, 0);

	git_config(get_alias, &alias_list);
	string_list_sort(&alias_list);

	for (i = 0; i < alias_list.nr; i++) {
		size_t len = strlen(alias_list.items[i].string);
		if (longest < len)
			longest = len;
	}

	if (alias_list.nr) {
		printf("\n%s\n", _("Command aliases"));
		ALLOC_ARRAY(aliases, alias_list.nr + 1);
		for (i = 0; i < alias_list.nr; i++) {
			aliases[i].name = alias_list.items[i].string;
			aliases[i].help = alias_list.items[i].util;
			aliases[i].category = 1;
		}
		aliases[alias_list.nr].name = NULL;
		print_command_list(aliases, 1, longest);
		free(aliases);
	}
	string_list_clear(&alias_list, 1);
}

int is_in_cmdlist(struct cmdnames *c, const char *s)
{
	int i;
	for (i = 0; i < c->cnt; i++)
		if (!strcmp(s, c->names[i]->name))
			return 1;
	return 0;
}

static int autocorrect;
static struct cmdnames aliases;

static int git_unknown_cmd_config(const char *var, const char *value, void *cb)
{
	const char *p;

	if (!strcmp(var, "help.autocorrect"))
		autocorrect = git_config_int(var,value);
	/* Also use aliases for command lookup */
	if (skip_prefix(var, "alias.", &p))
		add_cmdname(&aliases, p, strlen(p));

	return git_default_config(var, value, cb);
}

static int levenshtein_compare(const void *p1, const void *p2)
{
	const struct cmdname *const *c1 = p1, *const *c2 = p2;
	const char *s1 = (*c1)->name, *s2 = (*c2)->name;
	int l1 = (*c1)->len;
	int l2 = (*c2)->len;
	return l1 != l2 ? l1 - l2 : strcmp(s1, s2);
}

static void add_cmd_list(struct cmdnames *cmds, struct cmdnames *old)
{
	int i;
	ALLOC_GROW(cmds->names, cmds->cnt + old->cnt, cmds->alloc);

	for (i = 0; i < old->cnt; i++)
		cmds->names[cmds->cnt++] = old->names[i];
	FREE_AND_NULL(old->names);
	old->cnt = 0;
}

/* An empirically derived magic number */
#define SIMILARITY_FLOOR 7
#define SIMILAR_ENOUGH(x) ((x) < SIMILARITY_FLOOR)

static const char bad_interpreter_advice[] =
	N_("'%s' appears to be a git command, but we were not\n"
	"able to execute it. Maybe git-%s is broken?");

const char *help_unknown_cmd(const char *cmd)
{
	int i, n, best_similarity = 0;
	struct cmdnames main_cmds, other_cmds;
	struct cmdname_help *common_cmds;

	memset(&main_cmds, 0, sizeof(main_cmds));
	memset(&other_cmds, 0, sizeof(other_cmds));
	memset(&aliases, 0, sizeof(aliases));

	read_early_config(git_unknown_cmd_config, NULL);

	load_command_list("git-", &main_cmds, &other_cmds);

	add_cmd_list(&main_cmds, &aliases);
	add_cmd_list(&main_cmds, &other_cmds);
	QSORT(main_cmds.names, main_cmds.cnt, cmdname_compare);
	uniq(&main_cmds);

	extract_cmds(&common_cmds, common_mask);

	/* This abuses cmdname->len for levenshtein distance */
	for (i = 0, n = 0; i < main_cmds.cnt; i++) {
		int cmp = 0; /* avoid compiler stupidity */
		const char *candidate = main_cmds.names[i]->name;

		/*
		 * An exact match means we have the command, but
		 * for some reason exec'ing it gave us ENOENT; probably
		 * it's a bad interpreter in the #! line.
		 */
		if (!strcmp(candidate, cmd))
			die(_(bad_interpreter_advice), cmd, cmd);

		/* Does the candidate appear in common_cmds list? */
		while (common_cmds[n].name &&
		       (cmp = strcmp(common_cmds[n].name, candidate)) < 0)
			n++;
		if (common_cmds[n].name && !cmp) {
			/* Yes, this is one of the common commands */
			n++; /* use the entry from common_cmds[] */
			if (starts_with(candidate, cmd)) {
				/* Give prefix match a very good score */
				main_cmds.names[i]->len = 0;
				continue;
			}
		}

		main_cmds.names[i]->len =
			levenshtein(cmd, candidate, 0, 2, 1, 3) + 1;
	}
	FREE_AND_NULL(common_cmds);

	QSORT(main_cmds.names, main_cmds.cnt, levenshtein_compare);

	if (!main_cmds.cnt)
		die(_("Uh oh. Your system reports no Git commands at all."));

	/* skip and count prefix matches */
	for (n = 0; n < main_cmds.cnt && !main_cmds.names[n]->len; n++)
		; /* still counting */

	if (main_cmds.cnt <= n) {
		/* prefix matches with everything? that is too ambiguous */
		best_similarity = SIMILARITY_FLOOR + 1;
	} else {
		/* count all the most similar ones */
		for (best_similarity = main_cmds.names[n++]->len;
		     (n < main_cmds.cnt &&
		      best_similarity == main_cmds.names[n]->len);
		     n++)
			; /* still counting */
	}
	if (autocorrect && n == 1 && SIMILAR_ENOUGH(best_similarity)) {
		const char *assumed = main_cmds.names[0]->name;
		main_cmds.names[0] = NULL;
		clean_cmdnames(&main_cmds);
		fprintf_ln(stderr,
			   _("WARNING: You called a Git command named '%s', "
			     "which does not exist."),
			   cmd);
		if (autocorrect < 0)
			fprintf_ln(stderr,
				   _("Continuing under the assumption that "
				     "you meant '%s'."),
				   assumed);
		else {
			fprintf_ln(stderr,
				   _("Continuing in %0.1f seconds, "
				     "assuming that you meant '%s'."),
				   (float)autocorrect/10.0, assumed);
			sleep_millisec(autocorrect * 100);
		}
		return assumed;
	}

	fprintf_ln(stderr, _("git: '%s' is not a git command. See 'git --help'."), cmd);

	if (SIMILAR_ENOUGH(best_similarity)) {
		fprintf_ln(stderr,
			   Q_("\nThe most similar command is",
			      "\nThe most similar commands are",
			   n));

		for (i = 0; i < n; i++)
			fprintf(stderr, "\t%s\n", main_cmds.names[i]->name);
	}

	exit(1);
}

int cmd_version(int argc, const char **argv, const char *prefix)
{
	int build_options = 0;
	const char * const usage[] = {
		N_("git version [<options>]"),
		NULL
	};
	struct option options[] = {
		OPT_BOOL(0, "build-options", &build_options,
			 "also print build options"),
		OPT_END()
	};

	argc = parse_options(argc, argv, prefix, options, usage, 0);

	/*
	 * The format of this string should be kept stable for compatibility
	 * with external projects that rely on the output of "git version".
	 *
	 * Always show the version, even if other options are given.
	 */
	printf("git version %s\n", git_version_string);

	if (build_options) {
		printf("cpu: %s\n", GIT_HOST_CPU);
		if (git_built_from_commit_string[0])
			printf("built from commit: %s\n",
			       git_built_from_commit_string);
		else
			printf("no commit associated with this build\n");
		printf("sizeof-long: %d\n", (int)sizeof(long));
		printf("sizeof-size_t: %d\n", (int)sizeof(size_t));
		/* NEEDSWORK: also save and output GIT-BUILD_OPTIONS? */
	}
	return 0;
}

struct similar_ref_cb {
	const char *base_ref;
	struct string_list *similar_refs;
};

static int append_similar_ref(const char *refname, const struct object_id *oid,
			      int flags, void *cb_data)
{
	struct similar_ref_cb *cb = (struct similar_ref_cb *)(cb_data);
	char *branch = strrchr(refname, '/') + 1;

	/* A remote branch of the same name is deemed similar */
	if (starts_with(refname, "refs/remotes/") &&
	    !strcmp(branch, cb->base_ref))
		string_list_append_nodup(cb->similar_refs,
					 shorten_unambiguous_ref(refname, 1));
	return 0;
}

static struct string_list guess_refs(const char *ref)
{
	struct similar_ref_cb ref_cb;
	struct string_list similar_refs = STRING_LIST_INIT_DUP;

	ref_cb.base_ref = ref;
	ref_cb.similar_refs = &similar_refs;
	for_each_ref(append_similar_ref, &ref_cb);
	return similar_refs;
}

NORETURN void help_unknown_ref(const char *ref, const char *cmd,
			       const char *error)
{
	int i;
	struct string_list suggested_refs = guess_refs(ref);

	fprintf_ln(stderr, _("%s: %s - %s"), cmd, ref, error);

	if (suggested_refs.nr > 0) {
		fprintf_ln(stderr,
			   Q_("\nDid you mean this?",
			      "\nDid you mean one of these?",
			      suggested_refs.nr));
		for (i = 0; i < suggested_refs.nr; i++)
			fprintf(stderr, "\t%s\n", suggested_refs.items[i].string);
	}

	string_list_clear(&suggested_refs, 0);
	exit(1);
}
