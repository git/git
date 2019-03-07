#include "cache.h"
#include "add-interactive.h"
#include "color.h"
#include "config.h"
#include "diffcore.h"
#include "revision.h"
#include "refs.h"
#include "string-list.h"

struct add_i_state {
	struct repository *r;
	int use_color;
	char header_color[COLOR_MAXLEN];
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

static void init_add_i_state(struct add_i_state *s, struct repository *r)
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
}

struct list_options {
	int columns;
	const char *header;
	void (*print_item)(int i, struct string_list_item *item, void *print_item_data);
	void *print_item_data;
};

static void list(struct add_i_state *s, struct string_list *list,
		 struct list_options *opts)
{
	int i, last_lf = 0;

	if (!list->nr)
		return;

	if (opts->header)
		color_fprintf_ln(stdout, s->header_color,
				 "%s", opts->header);

	for (i = 0; i < list->nr; i++) {
		opts->print_item(i, list->items + i, opts->print_item_data);

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
};

#define LIST_AND_CHOOSE_ERROR (-1)
#define LIST_AND_CHOOSE_QUIT  (-2)

/*
 * Returns the selected index.
 *
 * If an error occurred, returns `LIST_AND_CHOOSE_ERROR`. Upon EOF,
 * `LIST_AND_CHOOSE_QUIT` is returned.
 */
static ssize_t list_and_choose(struct add_i_state *s, struct string_list *items,
			       struct list_and_choose_options *opts)
{
	struct strbuf input = STRBUF_INIT;
	ssize_t res = LIST_AND_CHOOSE_ERROR;

	for (;;) {
		char *p;

		strbuf_reset(&input);

		list(s, items, &opts->list_opts);

		printf("%s%s", opts->prompt, "> ");
		fflush(stdout);

		if (strbuf_getline(&input, stdin) == EOF) {
			putchar('\n');
			res = LIST_AND_CHOOSE_QUIT;
			break;
		}
		strbuf_trim(&input);

		if (!input.len)
			break;

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
				char *endp;
				index = strtoul(p, &endp, 10) - 1;
				if (endp != p + sep)
					index = -1;
			}

			if (p[sep])
				p[sep++] = '\0';
			if (index < 0 || index >= items->nr)
				printf(_("Huh (%s)?\n"), p);
			else {
				res = index;
				break;
			}

			p += sep;
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

struct file_item {
	struct adddel index, worktree;
};

static void add_file_item(struct string_list *files, const char *name)
{
	struct file_item *item = xcalloc(sizeof(*item), 1);

	string_list_append(files, name)->util = item;
}

struct pathname_entry {
	struct hashmap_entry ent;
	const char *name;
	struct file_item *item;
};

static int pathname_entry_cmp(const void *unused_cmp_data,
			      const struct hashmap_entry *he1,
			      const struct hashmap_entry *he2,
			      const void *name)
{
	const struct pathname_entry *e1 =
		container_of(he1, const struct pathname_entry, ent);
	const struct pathname_entry *e2 =
		container_of(he2, const struct pathname_entry, ent);

	return strcmp(e1->name, name ? (const char *)name : e2->name);
}

struct collection_status {
	enum { FROM_WORKTREE = 0, FROM_INDEX = 1 } phase;

	const char *reference;

	struct string_list *files;
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
		struct file_item *file_item;
		struct adddel *adddel;

		entry = hashmap_get_entry_from_hash(&s->file_map, hash, name,
						    struct pathname_entry, ent);
		if (!entry) {
			add_file_item(s->files, name);

			entry = xcalloc(sizeof(*entry), 1);
			hashmap_entry_init(&entry->ent, hash);
			entry->name = s->files->items[s->files->nr - 1].string;
			entry->item = s->files->items[s->files->nr - 1].util;
			hashmap_add(&s->file_map, &entry->ent);
		}

		file_item = entry->item;
		adddel = s->phase == FROM_INDEX ?
			&file_item->index : &file_item->worktree;
		adddel->seen = 1;
		adddel->add = stat.files[i]->added;
		adddel->del = stat.files[i]->deleted;
		if (stat.files[i]->is_binary)
			adddel->binary = 1;
	}
	free_diffstat_info(&stat);
}

static int get_modified_files(struct repository *r, struct string_list *files,
			      const struct pathspec *ps)
{
	struct object_id head_oid;
	int is_initial = !resolve_ref_unsafe("HEAD", RESOLVE_REF_READING,
					     &head_oid, NULL);
	struct collection_status s = { FROM_WORKTREE };

	if (discard_index(r->index) < 0 ||
	    repo_read_index_preload(r, ps, 0) < 0)
		return error(_("could not read index"));

	string_list_clear(files, 1);
	s.files = files;
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
	hashmap_free_entries(&s.file_map, struct pathname_entry, ent);

	/* While the diffs are ordered already, we ran *two* diffs... */
	string_list_sort(files);

	return 0;
}

static void render_adddel(struct strbuf *buf,
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

struct print_file_item_data {
	const char *modified_fmt;
	struct strbuf buf, index, worktree;
};

static void print_file_item(int i, struct string_list_item *item,
			    void *print_file_item_data)
{
	struct file_item *c = item->util;
	struct print_file_item_data *d = print_file_item_data;

	strbuf_reset(&d->index);
	strbuf_reset(&d->worktree);
	strbuf_reset(&d->buf);

	render_adddel(&d->worktree, &c->worktree, _("nothing"));
	render_adddel(&d->index, &c->index, _("unchanged"));
	strbuf_addf(&d->buf, d->modified_fmt,
		    d->index.buf, d->worktree.buf, item->string);

	printf(" %2d: %s", i + 1, d->buf.buf);
}

static int run_status(struct add_i_state *s, const struct pathspec *ps,
		      struct string_list *files, struct list_options *opts)
{
	if (get_modified_files(s->r, files, ps) < 0)
		return -1;

	list(s, files, opts);
	putchar('\n');

	return 0;
}

typedef int (*command_t)(struct add_i_state *s, const struct pathspec *ps,
			 struct string_list *files,
			 struct list_options *opts);

static void print_command_item(int i, struct string_list_item *item,
			       void *print_command_item_data)
{
	printf(" %2d: %s", i + 1, item->string);
}

int run_add_i(struct repository *r, const struct pathspec *ps)
{
	struct add_i_state s = { NULL };
	struct list_and_choose_options main_loop_opts = {
		{ 4, N_("*** Commands ***"), print_command_item, NULL },
		N_("What now")
	};
	struct {
		const char *string;
		command_t command;
	} command_list[] = {
		{ "status", run_status },
	};
	struct string_list commands = STRING_LIST_INIT_NODUP;

	struct print_file_item_data print_file_item_data = {
		"%12s %12s %s", STRBUF_INIT, STRBUF_INIT, STRBUF_INIT
	};
	struct list_options opts = {
		0, NULL, print_file_item, &print_file_item_data
	};
	struct strbuf header = STRBUF_INIT;
	struct string_list files = STRING_LIST_INIT_DUP;
	ssize_t i;
	int res = 0;

	for (i = 0; i < ARRAY_SIZE(command_list); i++)
		string_list_append(&commands, command_list[i].string)
			->util = command_list[i].command;

	init_add_i_state(&s, r);

	strbuf_addstr(&header, "      ");
	strbuf_addf(&header, print_file_item_data.modified_fmt,
		    _("staged"), _("unstaged"), _("path"));
	opts.header = header.buf;

	if (discard_index(r->index) < 0 ||
	    repo_read_index(r) < 0 ||
	    repo_refresh_and_write_index(r, REFRESH_QUIET, 0, 1,
					 NULL, NULL, NULL) < 0)
		warning(_("could not refresh index"));

	res = run_status(&s, ps, &files, &opts);

	for (;;) {
		i = list_and_choose(&s, &commands, &main_loop_opts);
		if (i == LIST_AND_CHOOSE_QUIT) {
			printf(_("Bye.\n"));
			res = 0;
			break;
		}
		if (i != LIST_AND_CHOOSE_ERROR) {
			command_t command = commands.items[i].util;
			res = command(&s, ps, &files, &opts);
		}
	}

	string_list_clear(&files, 1);
	strbuf_release(&print_file_item_data.buf);
	strbuf_release(&print_file_item_data.index);
	strbuf_release(&print_file_item_data.worktree);
	strbuf_release(&header);
	string_list_clear(&commands, 0);

	return res;
}
