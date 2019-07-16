#include "cache.h"
#include "add-interactive.h"
#include "color.h"
#include "config.h"
#include "diffcore.h"
#include "revision.h"
#include "refs.h"
#include "prefix-map.h"

struct add_i_state {
	struct repository *r;
	int use_color;
	char header_color[COLOR_MAXLEN];
	char help_color[COLOR_MAXLEN];
	char prompt_color[COLOR_MAXLEN];
	char error_color[COLOR_MAXLEN];
	char reset_color[COLOR_MAXLEN];
};

static void init_color(struct repository *r, struct add_i_state *s,
		       const char *slot_name, char *dst,
		       const char *default_color)
{
	char *key = xstrfmt("color.interactive.%s", slot_name);
	const char *value;

	if (!s->use_color)
		dst[0] = '\0';
	else if (repo_config_get_value(r, key, &value) ||
		 color_parse(value, dst))
		strlcpy(dst, default_color, COLOR_MAXLEN);

	free(key);
}

static int init_add_i_state(struct repository *r, struct add_i_state *s)
{
	const char *value;

	s->r = r;

	if (repo_config_get_value(r, "color.interactive", &value))
		s->use_color = -1;
	else
		s->use_color =
			git_config_colorbool("color.interactive", value);
	s->use_color = want_color(s->use_color);

	init_color(r, s, "header", s->header_color, GIT_COLOR_BOLD);
	init_color(r, s, "help", s->help_color, GIT_COLOR_BOLD_RED);
	init_color(r, s, "prompt", s->prompt_color, GIT_COLOR_BOLD_BLUE);
	init_color(r, s, "error", s->error_color, GIT_COLOR_BOLD_RED);
	init_color(r, s, "reset", s->reset_color, GIT_COLOR_RESET);

	return 0;
}

static ssize_t find_unique(const char *string,
			   struct prefix_item **list, size_t nr)
{
	ssize_t found = -1, i;

	for (i = 0; i < nr; i++) {
		struct prefix_item *item = list[i];
		if (!starts_with(item->name, string))
			continue;
		if (found >= 0)
			return -1;
		found = i;
	}

	return found;
}

struct list_options {
	int columns;
	const char *header;
	void (*print_item)(int i, struct prefix_item *item,
			   void *print_item_data);
	void *print_item_data;
};

static void list(struct prefix_item **list, size_t nr,
		 struct add_i_state *s, struct list_options *opts)
{
	int i, last_lf = 0;

	if (!nr)
		return;

	if (opts->header)
		color_fprintf_ln(stdout, s->header_color,
				 "%s", opts->header);

	for (i = 0; i < nr; i++) {
		opts->print_item(i, list[i], opts->print_item_data);

		if ((opts->columns) && ((i + 1) % (opts->columns))) {
			putchar('\t');
			last_lf = 0;
		}
		else {
			putchar('\n');
			last_lf = 1;
		}
	}

	if (!last_lf)
		putchar('\n');
}
struct list_and_choose_options {
	struct list_options list_opts;

	const char *prompt;
	void (*print_help)(struct add_i_state *s);
};

#define LIST_AND_CHOOSE_ERROR (-1)
#define LIST_AND_CHOOSE_QUIT  (-2)

/*
 * Returns the selected index.
 *
 * If an error occurred, returns `LIST_AND_CHOOSE_ERROR`. Upon EOF,
 * `LIST_AND_CHOOSE_QUIT` is returned.
 */
static ssize_t list_and_choose(struct prefix_item **items, size_t nr,
			       struct add_i_state *s,
			       struct list_and_choose_options *opts)
{
	struct strbuf input = STRBUF_INIT;
	ssize_t res = LIST_AND_CHOOSE_ERROR;

	find_unique_prefixes(items, nr, 1, 4);

	for (;;) {
		char *p, *endp;

		strbuf_reset(&input);

		list(items, nr, s, &opts->list_opts);

		color_fprintf(stdout, s->prompt_color, "%s", opts->prompt);
		fputs("> ", stdout);
		fflush(stdout);

		if (strbuf_getline(&input, stdin) == EOF) {
			putchar('\n');
			res = LIST_AND_CHOOSE_QUIT;
			break;
		}
		strbuf_trim(&input);

		if (!input.len)
			break;

		if (!strcmp(input.buf, "?")) {
			opts->print_help(s);
			continue;
		}

		p = input.buf;
		for (;;) {
			size_t sep = strcspn(p, " \t\r\n,");
			ssize_t index = -1;

			if (!sep) {
				if (!*p)
					break;
				p++;
				continue;
			}

			if (isdigit(*p)) {
				index = strtoul(p, &endp, 10) - 1;
				if (endp != p + sep)
					index = -1;
			}

			p[sep] = '\0';
			if (index < 0)
				index = find_unique(p, items, nr);

			if (index < 0 || index >= nr)
				color_fprintf_ln(stdout, s->error_color,
						 _("Huh (%s)?"), p);
			else {
				res = index;
				break;
			}

			p += sep + 1;
		}

		if (res != LIST_AND_CHOOSE_ERROR)
			break;
	}

	strbuf_release(&input);
	return res;
}

struct adddel {
	uintmax_t add, del;
	unsigned seen:1, binary:1;
};

struct file_list {
	struct file_item {
		struct prefix_item item;
		struct adddel index, worktree;
	} **file;
	size_t nr, alloc;
};

static void add_file_item(struct file_list *list, const char *name)
{
	struct file_item *item;

	FLEXPTR_ALLOC_STR(item, item.name, name);

	ALLOC_GROW(list->file, list->nr + 1, list->alloc);
	list->file[list->nr++] = item;
}

static void reset_file_list(struct file_list *list)
{
	size_t i;

	for (i = 0; i < list->nr; i++)
		free(list->file[i]);
	list->nr = 0;
}

static void release_file_list(struct file_list *list)
{
	reset_file_list(list);
	FREE_AND_NULL(list->file);
	list->alloc = 0;
}

static int file_item_cmp(const void *a, const void *b)
{
	const struct file_item * const *f1 = a;
	const struct file_item * const *f2 = b;

	return strcmp((*f1)->item.name, (*f2)->item.name);
}

struct pathname_entry {
	struct hashmap_entry ent;
	size_t index;
	char pathname[FLEX_ARRAY];
};

static int pathname_entry_cmp(const void *unused_cmp_data,
			      const void *entry, const void *entry_or_key,
			      const void *pathname)
{
	const struct pathname_entry *e1 = entry, *e2 = entry_or_key;

	return strcmp(e1->pathname,
		      pathname ? (const char *)pathname : e2->pathname);
}

struct collection_status {
	enum { FROM_WORKTREE = 0, FROM_INDEX = 1 } phase;

	const char *reference;

	struct file_list *list;
	struct hashmap file_map;
};

static void collect_changes_cb(struct diff_queue_struct *q,
			       struct diff_options *options,
			       void *data)
{
	struct collection_status *s = data;
	struct diffstat_t stat = { 0 };
	int i;

	if (!q->nr)
		return;

	compute_diffstat(options, &stat, q);

	for (i = 0; i < stat.nr; i++) {
		const char *name = stat.files[i]->name;
		int hash = strhash(name);
		struct pathname_entry *entry;
		size_t file_index;
		struct file_item *file;
		struct adddel *adddel;

		entry = hashmap_get_from_hash(&s->file_map, hash, name);
		if (entry)
			file_index = entry->index;
		else {
			FLEX_ALLOC_STR(entry, pathname, name);
			hashmap_entry_init(entry, hash);
			entry->index = file_index = s->list->nr;
			hashmap_add(&s->file_map, entry);

			add_file_item(s->list, name);
		}
		file = s->list->file[file_index];

		adddel = s->phase == FROM_INDEX ? &file->index : &file->worktree;
		adddel->seen = 1;
		adddel->add = stat.files[i]->added;
		adddel->del = stat.files[i]->deleted;
		if (stat.files[i]->is_binary)
			adddel->binary = 1;
	}
}

static int get_modified_files(struct repository *r, struct file_list *list,
			      const struct pathspec *ps)
{
	struct object_id head_oid;
	int is_initial = !resolve_ref_unsafe("HEAD", RESOLVE_REF_READING,
					     &head_oid, NULL);
	struct collection_status s = { FROM_WORKTREE };

	if (repo_read_index_preload(r, ps, 0) < 0)
		return error(_("could not read index"));

	s.list = list;
	hashmap_init(&s.file_map, pathname_entry_cmp, NULL, 0);

	for (s.phase = FROM_WORKTREE; s.phase <= FROM_INDEX; s.phase++) {
		struct rev_info rev;
		struct setup_revision_opt opt = { 0 };

		opt.def = is_initial ?
			empty_tree_oid_hex() : oid_to_hex(&head_oid);

		init_revisions(&rev, NULL);
		setup_revisions(0, NULL, &rev, &opt);

		rev.diffopt.output_format = DIFF_FORMAT_CALLBACK;
		rev.diffopt.format_callback = collect_changes_cb;
		rev.diffopt.format_callback_data = &s;

		if (ps)
			copy_pathspec(&rev.prune_data, ps);

		if (s.phase == FROM_INDEX)
			run_diff_index(&rev, 1);
		else {
			rev.diffopt.flags.ignore_dirty_submodules = 1;
			run_diff_files(&rev, 0);
		}
	}
	hashmap_free(&s.file_map, 1);

	/* While the diffs are ordered already, we ran *two* diffs... */
	QSORT(list->file, list->nr, file_item_cmp);

	return 0;
}

static void populate_wi_changes(struct strbuf *buf,
				struct adddel *ad, const char *no_changes)
{
	if (ad->binary)
		strbuf_addstr(buf, _("binary"));
	else if (ad->seen)
		strbuf_addf(buf, "+%"PRIuMAX"/-%"PRIuMAX,
			    (uintmax_t)ad->add, (uintmax_t)ad->del);
	else
		strbuf_addstr(buf, no_changes);
}

/* filters out prefixes which have special meaning to list_and_choose() */
static int is_valid_prefix(const char *prefix, size_t prefix_len)
{
	return prefix_len && prefix &&
		/*
		 * We expect `prefix` to be NUL terminated, therefore this
		 * `strcspn()` call is okay, even if it might do much more
		 * work than strictly necessary.
		 */
		strcspn(prefix, " \t\r\n,") >= prefix_len &&	/* separators */
		*prefix != '-' &&				/* deselection */
		!isdigit(*prefix) &&				/* selection */
		(prefix_len != 1 ||
		 (*prefix != '*' &&				/* "all" wildcard */
		  *prefix != '?'));				/* prompt help */
}

struct print_file_item_data {
	const char *modified_fmt;
	struct strbuf buf, index, worktree;
};

static void print_file_item(int i, struct prefix_item *item,
			    void *print_file_item_data)
{
	struct file_item *c = (struct file_item *)item;
	struct print_file_item_data *d = print_file_item_data;

	strbuf_reset(&d->index);
	strbuf_reset(&d->worktree);
	strbuf_reset(&d->buf);

	populate_wi_changes(&d->worktree, &c->worktree, _("nothing"));
	populate_wi_changes(&d->index, &c->index, _("unchanged"));
	strbuf_addf(&d->buf, d->modified_fmt,
		    d->index.buf, d->worktree.buf, item->name);

	printf(" %2d: %s", i + 1, d->buf.buf);
}

static int run_status(struct add_i_state *s, const struct pathspec *ps,
		      struct file_list *files, struct list_options *opts)
{
	reset_file_list(files);

	if (get_modified_files(s->r, files, ps) < 0)
		return -1;

	if (files->nr)
		list((struct prefix_item **)files->file, files->nr, s, opts);
	putchar('\n');

	return 0;
}

static int run_help(struct add_i_state *s, const struct pathspec *ps,
		    struct file_list *files, struct list_options *opts)
{
	const char *help_color = s->help_color;

	color_fprintf_ln(stdout, help_color, "status        - %s",
			 _("show paths with changes"));
	color_fprintf_ln(stdout, help_color, "update        - %s",
			 _("add working tree state to the staged set of changes"));
	color_fprintf_ln(stdout, help_color, "revert        - %s",
			 _("revert staged set of changes back to the HEAD version"));
	color_fprintf_ln(stdout, help_color, "patch         - %s",
			 _("pick hunks and update selectively"));
	color_fprintf_ln(stdout, help_color, "diff          - %s",
			 _("view diff between HEAD and index"));
	color_fprintf_ln(stdout, help_color, "add untracked - %s",
			 _("add contents of untracked files to the staged set of changes"));

	return 0;
}

struct print_command_item_data {
	const char *color, *reset;
};

static void print_command_item(int i, struct prefix_item *item,
			       void *print_command_item_data)
{
	struct print_command_item_data *d = print_command_item_data;

	if (!item->prefix_length ||
	    !is_valid_prefix(item->name, item->prefix_length))
		printf(" %2d: %s", i + 1, item->name);
	else
		printf(" %2d: %s%.*s%s%s", i + 1,
		       d->color, (int)item->prefix_length, item->name, d->reset,
		       item->name + item->prefix_length);
}

struct command_item {
	struct prefix_item item;
	int (*command)(struct add_i_state *s, const struct pathspec *ps,
		       struct file_list *files, struct list_options *opts);
};

static void command_prompt_help(struct add_i_state *s)
{
	const char *help_color = s->help_color;
	color_fprintf_ln(stdout, help_color, "%s", _("Prompt help:"));
	color_fprintf_ln(stdout, help_color, "1          - %s",
			 _("select a numbered item"));
	color_fprintf_ln(stdout, help_color, "foo        - %s",
			 _("select item based on unique prefix"));
	color_fprintf_ln(stdout, help_color, "           - %s",
			 _("(empty) select nothing"));
}

int run_add_i(struct repository *r, const struct pathspec *ps)
{
	struct add_i_state s = { NULL };
	struct print_command_item_data data;
	struct list_and_choose_options main_loop_opts = {
		{ 4, N_("*** Commands ***"), print_command_item, &data },
		N_("What now"), command_prompt_help
	};
	struct command_item
		status = { { "status" }, run_status },
		help = { { "help" }, run_help };
	struct command_item *commands[] = {
		&status,
		&help
	};

	struct print_file_item_data print_file_item_data = {
		"%12s %12s %s", STRBUF_INIT, STRBUF_INIT, STRBUF_INIT
	};
	struct list_options opts = {
		0, NULL, print_file_item, &print_file_item_data
	};
	struct strbuf header = STRBUF_INIT;
	struct file_list files = { NULL };
	ssize_t i;
	int res = 0;

	if (init_add_i_state(r, &s))
		return error("could not parse `add -i` config");

	/*
	 * When color was asked for, use the prompt color for
	 * highlighting, otherwise use square brackets.
	 */
	if (s.use_color) {
		data.color = s.prompt_color;
		data.reset = s.reset_color;
	} else {
		data.color = "[";
		data.reset = "]";
	}

	strbuf_addstr(&header, "      ");
	strbuf_addf(&header, print_file_item_data.modified_fmt,
		    _("staged"), _("unstaged"), _("path"));
	opts.header = header.buf;

	repo_refresh_and_write_index(r, REFRESH_QUIET, 1);
	if (run_status(&s, ps, &files, &opts) < 0)
		res = -1;

	for (;;) {
		i = list_and_choose((struct prefix_item **)commands,
				    ARRAY_SIZE(commands), &s, &main_loop_opts);
		if (i == LIST_AND_CHOOSE_QUIT) {
			printf(_("Bye.\n"));
			res = 0;
			break;
		}
		if (i != LIST_AND_CHOOSE_ERROR)
			res = commands[i]->command(&s, ps, &files, &opts);
	}

	release_file_list(&files);
	strbuf_release(&print_file_item_data.buf);
	strbuf_release(&print_file_item_data.index);
	strbuf_release(&print_file_item_data.worktree);
	strbuf_release(&header);

	return res;
}
