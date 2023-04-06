#include "cache.h"
#include "add-interactive.h"
#include "color.h"
#include "config.h"
#include "diffcore.h"
#include "gettext.h"
#include "hex.h"
#include "revision.h"
#include "refs.h"
#include "string-list.h"
#include "lockfile.h"
#include "dir.h"
#include "run-command.h"
#include "prompt.h"

static void init_color(struct repository *r, struct add_i_state *s,
		       const char *section_and_slot, char *dst,
		       const char *default_color)
{
	char *key = xstrfmt("color.%s", section_and_slot);
	const char *value;

	if (!s->use_color)
		dst[0] = '\0';
	else if (repo_config_get_value(r, key, &value) ||
		 color_parse(value, dst))
		strlcpy(dst, default_color, COLOR_MAXLEN);

	free(key);
}

void init_add_i_state(struct add_i_state *s, struct repository *r)
{
	const char *value;

	s->r = r;

	if (repo_config_get_value(r, "color.interactive", &value))
		s->use_color = -1;
	else
		s->use_color =
			git_config_colorbool("color.interactive", value);
	s->use_color = want_color(s->use_color);

	init_color(r, s, "interactive.header", s->header_color, GIT_COLOR_BOLD);
	init_color(r, s, "interactive.help", s->help_color, GIT_COLOR_BOLD_RED);
	init_color(r, s, "interactive.prompt", s->prompt_color,
		   GIT_COLOR_BOLD_BLUE);
	init_color(r, s, "interactive.error", s->error_color,
		   GIT_COLOR_BOLD_RED);

	init_color(r, s, "diff.frag", s->fraginfo_color,
		   diff_get_color(s->use_color, DIFF_FRAGINFO));
	init_color(r, s, "diff.context", s->context_color, "fall back");
	if (!strcmp(s->context_color, "fall back"))
		init_color(r, s, "diff.plain", s->context_color,
			   diff_get_color(s->use_color, DIFF_CONTEXT));
	init_color(r, s, "diff.old", s->file_old_color,
		diff_get_color(s->use_color, DIFF_FILE_OLD));
	init_color(r, s, "diff.new", s->file_new_color,
		diff_get_color(s->use_color, DIFF_FILE_NEW));

	strlcpy(s->reset_color,
		s->use_color ? GIT_COLOR_RESET : "", COLOR_MAXLEN);

	FREE_AND_NULL(s->interactive_diff_filter);
	git_config_get_string("interactive.difffilter",
			      &s->interactive_diff_filter);

	FREE_AND_NULL(s->interactive_diff_algorithm);
	git_config_get_string("diff.algorithm",
			      &s->interactive_diff_algorithm);

	git_config_get_bool("interactive.singlekey", &s->use_single_key);
	if (s->use_single_key)
		setbuf(stdin, NULL);
}

void clear_add_i_state(struct add_i_state *s)
{
	FREE_AND_NULL(s->interactive_diff_filter);
	FREE_AND_NULL(s->interactive_diff_algorithm);
	memset(s, 0, sizeof(*s));
	s->use_color = -1;
}

/*
 * A "prefix item list" is a list of items that are identified by a string, and
 * a unique prefix (if any) is determined for each item.
 *
 * It is implemented in the form of a pair of `string_list`s, the first one
 * duplicating the strings, with the `util` field pointing at a structure whose
 * first field must be `size_t prefix_length`.
 *
 * That `prefix_length` field will be computed by `find_unique_prefixes()`; It
 * will be set to zero if no valid, unique prefix could be found.
 *
 * The second `string_list` is called `sorted` and does _not_ duplicate the
 * strings but simply reuses the first one's, with the `util` field pointing at
 * the `string_item_list` of the first `string_list`. It  will be populated and
 * sorted by `find_unique_prefixes()`.
 */
struct prefix_item_list {
	struct string_list items;
	struct string_list sorted;
	int *selected; /* for multi-selections */
	size_t min_length, max_length;
};
#define PREFIX_ITEM_LIST_INIT { \
	.items = STRING_LIST_INIT_DUP, \
	.sorted = STRING_LIST_INIT_NODUP, \
	.min_length = 1, \
	.max_length = 4, \
}

static void prefix_item_list_clear(struct prefix_item_list *list)
{
	string_list_clear(&list->items, 1);
	string_list_clear(&list->sorted, 0);
	FREE_AND_NULL(list->selected);
}

static void extend_prefix_length(struct string_list_item *p,
				 const char *other_string, size_t max_length)
{
	size_t *len = p->util;

	if (!*len || memcmp(p->string, other_string, *len))
		return;

	for (;;) {
		char c = p->string[*len];

		/*
		 * Is `p` a strict prefix of `other`? Or have we exhausted the
		 * maximal length of the prefix? Or is the current character a
		 * multi-byte UTF-8 one? If so, there is no valid, unique
		 * prefix.
		 */
		if (!c || ++*len > max_length || !isascii(c)) {
			*len = 0;
			break;
		}

		if (c != other_string[*len - 1])
			break;
	}
}

static void find_unique_prefixes(struct prefix_item_list *list)
{
	size_t i;

	if (list->sorted.nr == list->items.nr)
		return;

	string_list_clear(&list->sorted, 0);
	/* Avoid reallocating incrementally */
	list->sorted.items = xmalloc(st_mult(sizeof(*list->sorted.items),
					     list->items.nr));
	list->sorted.nr = list->sorted.alloc = list->items.nr;

	for (i = 0; i < list->items.nr; i++) {
		list->sorted.items[i].string = list->items.items[i].string;
		list->sorted.items[i].util = list->items.items + i;
	}

	string_list_sort(&list->sorted);

	for (i = 0; i < list->sorted.nr; i++) {
		struct string_list_item *sorted_item = list->sorted.items + i;
		struct string_list_item *item = sorted_item->util;
		size_t *len = item->util;

		*len = 0;
		while (*len < list->min_length) {
			char c = item->string[(*len)++];

			if (!c || !isascii(c)) {
				*len = 0;
				break;
			}
		}

		if (i > 0)
			extend_prefix_length(item, sorted_item[-1].string,
					     list->max_length);
		if (i + 1 < list->sorted.nr)
			extend_prefix_length(item, sorted_item[1].string,
					     list->max_length);
	}
}

static ssize_t find_unique(const char *string, struct prefix_item_list *list)
{
	int index = string_list_find_insert_index(&list->sorted, string, 1);
	struct string_list_item *item;

	if (list->items.nr != list->sorted.nr)
		BUG("prefix_item_list in inconsistent state (%"PRIuMAX
		    " vs %"PRIuMAX")",
		    (uintmax_t)list->items.nr, (uintmax_t)list->sorted.nr);

	if (index < 0)
		item = list->sorted.items[-1 - index].util;
	else if (index > 0 &&
		 starts_with(list->sorted.items[index - 1].string, string))
		return -1;
	else if (index + 1 < list->sorted.nr &&
		 starts_with(list->sorted.items[index + 1].string, string))
		return -1;
	else if (index < list->sorted.nr &&
		 starts_with(list->sorted.items[index].string, string))
		item = list->sorted.items[index].util;
	else
		return -1;
	return item - list->items.items;
}

struct list_options {
	int columns;
	const char *header;
	void (*print_item)(int i, int selected, struct string_list_item *item,
			   void *print_item_data);
	void *print_item_data;
};

static void list(struct add_i_state *s, struct string_list *list, int *selected,
		 struct list_options *opts)
{
	int i, last_lf = 0;

	if (!list->nr)
		return;

	if (opts->header)
		color_fprintf_ln(stdout, s->header_color,
				 "%s", opts->header);

	for (i = 0; i < list->nr; i++) {
		opts->print_item(i, selected ? selected[i] : 0, list->items + i,
				 opts->print_item_data);

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
	enum {
		SINGLETON = (1<<0),
		IMMEDIATE = (1<<1),
	} flags;
	void (*print_help)(struct add_i_state *s);
};

#define LIST_AND_CHOOSE_ERROR (-1)
#define LIST_AND_CHOOSE_QUIT  (-2)

/*
 * Returns the selected index in singleton mode, the number of selected items
 * otherwise.
 *
 * If an error occurred, returns `LIST_AND_CHOOSE_ERROR`. Upon EOF,
 * `LIST_AND_CHOOSE_QUIT` is returned.
 */
static ssize_t list_and_choose(struct add_i_state *s,
			       struct prefix_item_list *items,
			       struct list_and_choose_options *opts)
{
	int singleton = opts->flags & SINGLETON;
	int immediate = opts->flags & IMMEDIATE;

	struct strbuf input = STRBUF_INIT;
	ssize_t res = singleton ? LIST_AND_CHOOSE_ERROR : 0;

	if (!singleton) {
		free(items->selected);
		CALLOC_ARRAY(items->selected, items->items.nr);
	}

	if (singleton && !immediate)
		BUG("singleton requires immediate");

	find_unique_prefixes(items);

	for (;;) {
		char *p;

		strbuf_reset(&input);

		list(s, &items->items, items->selected, &opts->list_opts);

		color_fprintf(stdout, s->prompt_color, "%s", opts->prompt);
		fputs(singleton ? "> " : ">> ", stdout);
		fflush(stdout);

		if (git_read_line_interactively(&input) == EOF) {
			putchar('\n');
			if (immediate)
				res = LIST_AND_CHOOSE_QUIT;
			break;
		}

		if (!input.len)
			break;

		if (!strcmp(input.buf, "?")) {
			opts->print_help(s);
			continue;
		}

		p = input.buf;
		for (;;) {
			size_t sep = strcspn(p, " \t\r\n,");
			int choose = 1;
			/* `from` is inclusive, `to` is exclusive */
			ssize_t from = -1, to = -1;

			if (!sep) {
				if (!*p)
					break;
				p++;
				continue;
			}

			/* Input that begins with '-'; de-select */
			if (*p == '-') {
				choose = 0;
				p++;
				sep--;
			}

			if (sep == 1 && *p == '*') {
				from = 0;
				to = items->items.nr;
			} else if (isdigit(*p)) {
				char *endp;
				/*
				 * A range can be specified like 5-7 or 5-.
				 *
				 * Note: `from` is 0-based while the user input
				 * is 1-based, hence we have to decrement by
				 * one. We do not have to decrement `to` even
				 * if it is 0-based because it is an exclusive
				 * boundary.
				 */
				from = strtoul(p, &endp, 10) - 1;
				if (endp == p + sep)
					to = from + 1;
				else if (*endp == '-') {
					if (isdigit(*(++endp)))
						to = strtoul(endp, &endp, 10);
					else
						to = items->items.nr;
					/* extra characters after the range? */
					if (endp != p + sep)
						from = -1;
				}
			}

			if (p[sep])
				p[sep++] = '\0';
			if (from < 0) {
				from = find_unique(p, items);
				if (from >= 0)
					to = from + 1;
			}

			if (from < 0 || from >= items->items.nr ||
			    (singleton && from + 1 != to)) {
				color_fprintf_ln(stderr, s->error_color,
						 _("Huh (%s)?"), p);
				break;
			} else if (singleton) {
				res = from;
				break;
			}

			if (to > items->items.nr)
				to = items->items.nr;

			for (; from < to; from++)
				if (items->selected[from] != choose) {
					items->selected[from] = choose;
					res += choose ? +1 : -1;
				}

			p += sep;
		}

		if ((immediate && res != LIST_AND_CHOOSE_ERROR) ||
		    !strcmp(input.buf, "*"))
			break;
	}

	strbuf_release(&input);
	return res;
}

struct adddel {
	uintmax_t add, del;
	unsigned seen:1, unmerged:1, binary:1;
};

struct file_item {
	size_t prefix_length;
	struct adddel index, worktree;
};

static void add_file_item(struct string_list *files, const char *name)
{
	struct file_item *item = xcalloc(1, sizeof(*item));

	string_list_append(files, name)->util = item;
}

struct pathname_entry {
	struct hashmap_entry ent;
	const char *name;
	struct file_item *item;
};

static int pathname_entry_cmp(const void *cmp_data UNUSED,
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
	enum { FROM_WORKTREE = 0, FROM_INDEX = 1 } mode;

	const char *reference;

	unsigned skip_unseen:1;
	size_t unmerged_count, binary_count;
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
		struct adddel *adddel, *other_adddel;

		entry = hashmap_get_entry_from_hash(&s->file_map, hash, name,
						    struct pathname_entry, ent);
		if (!entry) {
			if (s->skip_unseen)
				continue;

			add_file_item(s->files, name);

			CALLOC_ARRAY(entry, 1);
			hashmap_entry_init(&entry->ent, hash);
			entry->name = s->files->items[s->files->nr - 1].string;
			entry->item = s->files->items[s->files->nr - 1].util;
			hashmap_add(&s->file_map, &entry->ent);
		}

		file_item = entry->item;
		adddel = s->mode == FROM_INDEX ?
			&file_item->index : &file_item->worktree;
		other_adddel = s->mode == FROM_INDEX ?
			&file_item->worktree : &file_item->index;
		adddel->seen = 1;
		adddel->add = stat.files[i]->added;
		adddel->del = stat.files[i]->deleted;
		if (stat.files[i]->is_binary) {
			if (!other_adddel->binary)
				s->binary_count++;
			adddel->binary = 1;
		}
		if (stat.files[i]->is_unmerged) {
			if (!other_adddel->unmerged)
				s->unmerged_count++;
			adddel->unmerged = 1;
		}
	}
	free_diffstat_info(&stat);
}

enum modified_files_filter {
	NO_FILTER = 0,
	WORKTREE_ONLY = 1,
	INDEX_ONLY = 2,
};

static int get_modified_files(struct repository *r,
			      enum modified_files_filter filter,
			      struct prefix_item_list *files,
			      const struct pathspec *ps,
			      size_t *unmerged_count,
			      size_t *binary_count)
{
	struct object_id head_oid;
	int is_initial = !resolve_ref_unsafe("HEAD", RESOLVE_REF_READING,
					     &head_oid, NULL);
	struct collection_status s = { 0 };
	int i;

	discard_index(r->index);
	if (repo_read_index_preload(r, ps, 0) < 0)
		return error(_("could not read index"));

	prefix_item_list_clear(files);
	s.files = &files->items;
	hashmap_init(&s.file_map, pathname_entry_cmp, NULL, 0);

	for (i = 0; i < 2; i++) {
		struct rev_info rev;
		struct setup_revision_opt opt = { 0 };

		if (filter == INDEX_ONLY)
			s.mode = (i == 0) ? FROM_INDEX : FROM_WORKTREE;
		else
			s.mode = (i == 0) ? FROM_WORKTREE : FROM_INDEX;
		s.skip_unseen = filter && i;

		opt.def = is_initial ?
			empty_tree_oid_hex() : oid_to_hex(&head_oid);

		repo_init_revisions(r, &rev, NULL);
		setup_revisions(0, NULL, &rev, &opt);

		rev.diffopt.output_format = DIFF_FORMAT_CALLBACK;
		rev.diffopt.format_callback = collect_changes_cb;
		rev.diffopt.format_callback_data = &s;

		if (ps)
			copy_pathspec(&rev.prune_data, ps);

		if (s.mode == FROM_INDEX)
			run_diff_index(&rev, 1);
		else {
			rev.diffopt.flags.ignore_dirty_submodules = 1;
			run_diff_files(&rev, 0);
		}

		release_revisions(&rev);
	}
	hashmap_clear_and_free(&s.file_map, struct pathname_entry, ent);
	if (unmerged_count)
		*unmerged_count = s.unmerged_count;
	if (binary_count)
		*binary_count = s.binary_count;

	/* While the diffs are ordered already, we ran *two* diffs... */
	string_list_sort(&files->items);

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
	const char *modified_fmt, *color, *reset;
	struct strbuf buf, name, index, worktree;
	unsigned only_names:1;
};

static void print_file_item(int i, int selected, struct string_list_item *item,
			    void *print_file_item_data)
{
	struct file_item *c = item->util;
	struct print_file_item_data *d = print_file_item_data;
	const char *highlighted = NULL;

	strbuf_reset(&d->index);
	strbuf_reset(&d->worktree);
	strbuf_reset(&d->buf);

	/* Format the item with the prefix highlighted. */
	if (c->prefix_length > 0 &&
	    is_valid_prefix(item->string, c->prefix_length)) {
		strbuf_reset(&d->name);
		strbuf_addf(&d->name, "%s%.*s%s%s", d->color,
			    (int)c->prefix_length, item->string, d->reset,
			    item->string + c->prefix_length);
		highlighted = d->name.buf;
	}

	if (d->only_names) {
		printf("%c%2d: %s", selected ? '*' : ' ', i + 1,
		       highlighted ? highlighted : item->string);
		return;
	}

	render_adddel(&d->worktree, &c->worktree, _("nothing"));
	render_adddel(&d->index, &c->index, _("unchanged"));

	strbuf_addf(&d->buf, d->modified_fmt, d->index.buf, d->worktree.buf,
		    highlighted ? highlighted : item->string);

	printf("%c%2d: %s", selected ? '*' : ' ', i + 1, d->buf.buf);
}

static int run_status(struct add_i_state *s, const struct pathspec *ps,
		      struct prefix_item_list *files,
		      struct list_and_choose_options *opts)
{
	if (get_modified_files(s->r, NO_FILTER, files, ps, NULL, NULL) < 0)
		return -1;

	list(s, &files->items, NULL, &opts->list_opts);
	putchar('\n');

	return 0;
}

static int run_update(struct add_i_state *s, const struct pathspec *ps,
		      struct prefix_item_list *files,
		      struct list_and_choose_options *opts)
{
	int res = 0, fd;
	size_t count, i;
	struct lock_file index_lock;

	if (get_modified_files(s->r, WORKTREE_ONLY, files, ps, NULL, NULL) < 0)
		return -1;

	if (!files->items.nr) {
		putchar('\n');
		return 0;
	}

	opts->prompt = N_("Update");
	count = list_and_choose(s, files, opts);
	if (count <= 0) {
		putchar('\n');
		return 0;
	}

	fd = repo_hold_locked_index(s->r, &index_lock, LOCK_REPORT_ON_ERROR);
	if (fd < 0) {
		putchar('\n');
		return -1;
	}

	for (i = 0; i < files->items.nr; i++) {
		const char *name = files->items.items[i].string;
		struct stat st;

		if (!files->selected[i])
			continue;
		if (lstat(name, &st) && is_missing_file_error(errno)) {
			if (remove_file_from_index(s->r->index, name) < 0) {
				res = error(_("could not stage '%s'"), name);
				break;
			}
		} else if (add_file_to_index(s->r->index, name, 0) < 0) {
			res = error(_("could not stage '%s'"), name);
			break;
		}
	}

	if (!res && write_locked_index(s->r->index, &index_lock, COMMIT_LOCK) < 0)
		res = error(_("could not write index"));

	if (!res)
		printf(Q_("updated %d path\n",
			  "updated %d paths\n", count), (int)count);

	putchar('\n');
	return res;
}

static void revert_from_diff(struct diff_queue_struct *q,
			     struct diff_options *opt, void *data UNUSED)
{
	int i, add_flags = ADD_CACHE_OK_TO_ADD | ADD_CACHE_OK_TO_REPLACE;

	for (i = 0; i < q->nr; i++) {
		struct diff_filespec *one = q->queue[i]->one;
		struct cache_entry *ce;

		if (!(one->mode && !is_null_oid(&one->oid))) {
			remove_file_from_index(opt->repo->index, one->path);
			printf(_("note: %s is untracked now.\n"), one->path);
		} else {
			ce = make_cache_entry(opt->repo->index, one->mode,
					      &one->oid, one->path, 0, 0);
			if (!ce)
				die(_("make_cache_entry failed for path '%s'"),
				    one->path);
			add_index_entry(opt->repo->index, ce, add_flags);
		}
	}
}

static int run_revert(struct add_i_state *s, const struct pathspec *ps,
		      struct prefix_item_list *files,
		      struct list_and_choose_options *opts)
{
	int res = 0, fd;
	size_t count, i, j;

	struct object_id oid;
	int is_initial = !resolve_ref_unsafe("HEAD", RESOLVE_REF_READING, &oid,
					     NULL);
	struct lock_file index_lock;
	const char **paths;
	struct tree *tree;
	struct diff_options diffopt = { NULL };

	if (get_modified_files(s->r, INDEX_ONLY, files, ps, NULL, NULL) < 0)
		return -1;

	if (!files->items.nr) {
		putchar('\n');
		return 0;
	}

	opts->prompt = N_("Revert");
	count = list_and_choose(s, files, opts);
	if (count <= 0)
		goto finish_revert;

	fd = repo_hold_locked_index(s->r, &index_lock, LOCK_REPORT_ON_ERROR);
	if (fd < 0) {
		res = -1;
		goto finish_revert;
	}

	if (is_initial)
		oidcpy(&oid, s->r->hash_algo->empty_tree);
	else {
		tree = parse_tree_indirect(&oid);
		if (!tree) {
			res = error(_("Could not parse HEAD^{tree}"));
			goto finish_revert;
		}
		oidcpy(&oid, &tree->object.oid);
	}

	ALLOC_ARRAY(paths, count + 1);
	for (i = j = 0; i < files->items.nr; i++)
		if (files->selected[i])
			paths[j++] = files->items.items[i].string;
	paths[j] = NULL;

	parse_pathspec(&diffopt.pathspec, 0,
		       PATHSPEC_PREFER_FULL | PATHSPEC_LITERAL_PATH,
		       NULL, paths);

	diffopt.output_format = DIFF_FORMAT_CALLBACK;
	diffopt.format_callback = revert_from_diff;
	diffopt.flags.override_submodule_config = 1;
	diffopt.repo = s->r;

	if (do_diff_cache(&oid, &diffopt)) {
		diff_free(&diffopt);
		res = -1;
	} else {
		diffcore_std(&diffopt);
		diff_flush(&diffopt);
	}
	free(paths);

	if (!res && write_locked_index(s->r->index, &index_lock,
				       COMMIT_LOCK) < 0)
		res = -1;
	else
		res = repo_refresh_and_write_index(s->r, REFRESH_QUIET, 0, 1,
						   NULL, NULL, NULL);

	if (!res)
		printf(Q_("reverted %d path\n",
			  "reverted %d paths\n", count), (int)count);

finish_revert:
	putchar('\n');
	return res;
}

static int get_untracked_files(struct repository *r,
			       struct prefix_item_list *files,
			       const struct pathspec *ps)
{
	struct dir_struct dir = { 0 };
	size_t i;
	struct strbuf buf = STRBUF_INIT;

	if (repo_read_index(r) < 0)
		return error(_("could not read index"));

	prefix_item_list_clear(files);
	setup_standard_excludes(&dir);
	add_pattern_list(&dir, EXC_CMDL, "--exclude option");
	fill_directory(&dir, r->index, ps);

	for (i = 0; i < dir.nr; i++) {
		struct dir_entry *ent = dir.entries[i];

		if (index_name_is_other(r->index, ent->name, ent->len)) {
			strbuf_reset(&buf);
			strbuf_add(&buf, ent->name, ent->len);
			add_file_item(&files->items, buf.buf);
		}
	}

	strbuf_release(&buf);
	return 0;
}

static int run_add_untracked(struct add_i_state *s, const struct pathspec *ps,
		      struct prefix_item_list *files,
		      struct list_and_choose_options *opts)
{
	struct print_file_item_data *d = opts->list_opts.print_item_data;
	int res = 0, fd;
	size_t count, i;
	struct lock_file index_lock;

	if (get_untracked_files(s->r, files, ps) < 0)
		return -1;

	if (!files->items.nr) {
		printf(_("No untracked files.\n"));
		goto finish_add_untracked;
	}

	opts->prompt = N_("Add untracked");
	d->only_names = 1;
	count = list_and_choose(s, files, opts);
	d->only_names = 0;
	if (count <= 0)
		goto finish_add_untracked;

	fd = repo_hold_locked_index(s->r, &index_lock, LOCK_REPORT_ON_ERROR);
	if (fd < 0) {
		res = -1;
		goto finish_add_untracked;
	}

	for (i = 0; i < files->items.nr; i++) {
		const char *name = files->items.items[i].string;
		if (files->selected[i] &&
		    add_file_to_index(s->r->index, name, 0) < 0) {
			res = error(_("could not stage '%s'"), name);
			break;
		}
	}

	if (!res &&
	    write_locked_index(s->r->index, &index_lock, COMMIT_LOCK) < 0)
		res = error(_("could not write index"));

	if (!res)
		printf(Q_("added %d path\n",
			  "added %d paths\n", count), (int)count);

finish_add_untracked:
	putchar('\n');
	return res;
}

static int run_patch(struct add_i_state *s, const struct pathspec *ps,
		     struct prefix_item_list *files,
		     struct list_and_choose_options *opts)
{
	int res = 0;
	ssize_t count, i, j;
	size_t unmerged_count = 0, binary_count = 0;

	if (get_modified_files(s->r, WORKTREE_ONLY, files, ps,
			       &unmerged_count, &binary_count) < 0)
		return -1;

	if (unmerged_count || binary_count) {
		for (i = j = 0; i < files->items.nr; i++) {
			struct file_item *item = files->items.items[i].util;

			if (item->index.binary || item->worktree.binary) {
				free(item);
				free(files->items.items[i].string);
			} else if (item->index.unmerged ||
				 item->worktree.unmerged) {
				color_fprintf_ln(stderr, s->error_color,
						 _("ignoring unmerged: %s"),
						 files->items.items[i].string);
				free(item);
				free(files->items.items[i].string);
			} else
				files->items.items[j++] = files->items.items[i];
		}
		files->items.nr = j;
	}

	if (!files->items.nr) {
		if (binary_count)
			fprintf(stderr, _("Only binary files changed.\n"));
		else
			fprintf(stderr, _("No changes.\n"));
		return 0;
	}

	opts->prompt = N_("Patch update");
	count = list_and_choose(s, files, opts);
	if (count > 0) {
		struct strvec args = STRVEC_INIT;
		struct pathspec ps_selected = { 0 };

		for (i = 0; i < files->items.nr; i++)
			if (files->selected[i])
				strvec_push(&args,
					    files->items.items[i].string);
		parse_pathspec(&ps_selected,
			       PATHSPEC_ALL_MAGIC & ~PATHSPEC_LITERAL,
			       PATHSPEC_LITERAL_PATH, "", args.v);
		res = run_add_p(s->r, ADD_P_ADD, NULL, &ps_selected);
		strvec_clear(&args);
		clear_pathspec(&ps_selected);
	}

	return res;
}

static int run_diff(struct add_i_state *s, const struct pathspec *ps,
		    struct prefix_item_list *files,
		    struct list_and_choose_options *opts)
{
	int res = 0;
	ssize_t count, i;

	struct object_id oid;
	int is_initial = !resolve_ref_unsafe("HEAD", RESOLVE_REF_READING, &oid,
					     NULL);
	if (get_modified_files(s->r, INDEX_ONLY, files, ps, NULL, NULL) < 0)
		return -1;

	if (!files->items.nr) {
		putchar('\n');
		return 0;
	}

	opts->prompt = N_("Review diff");
	opts->flags = IMMEDIATE;
	count = list_and_choose(s, files, opts);
	opts->flags = 0;
	if (count > 0) {
		struct child_process cmd = CHILD_PROCESS_INIT;

		strvec_pushl(&cmd.args, "git", "diff", "-p", "--cached",
			     oid_to_hex(!is_initial ? &oid :
					s->r->hash_algo->empty_tree),
			     "--", NULL);
		for (i = 0; i < files->items.nr; i++)
			if (files->selected[i])
				strvec_push(&cmd.args,
					    files->items.items[i].string);
		res = run_command(&cmd);
	}

	putchar('\n');
	return res;
}

static int run_help(struct add_i_state *s, const struct pathspec *unused_ps,
		    struct prefix_item_list *unused_files,
		    struct list_and_choose_options *unused_opts)
{
	color_fprintf_ln(stdout, s->help_color, "status        - %s",
			 _("show paths with changes"));
	color_fprintf_ln(stdout, s->help_color, "update        - %s",
			 _("add working tree state to the staged set of changes"));
	color_fprintf_ln(stdout, s->help_color, "revert        - %s",
			 _("revert staged set of changes back to the HEAD version"));
	color_fprintf_ln(stdout, s->help_color, "patch         - %s",
			 _("pick hunks and update selectively"));
	color_fprintf_ln(stdout, s->help_color, "diff          - %s",
			 _("view diff between HEAD and index"));
	color_fprintf_ln(stdout, s->help_color, "add untracked - %s",
			 _("add contents of untracked files to the staged set of changes"));

	return 0;
}

static void choose_prompt_help(struct add_i_state *s)
{
	color_fprintf_ln(stdout, s->help_color, "%s",
			 _("Prompt help:"));
	color_fprintf_ln(stdout, s->help_color, "1          - %s",
			 _("select a single item"));
	color_fprintf_ln(stdout, s->help_color, "3-5        - %s",
			 _("select a range of items"));
	color_fprintf_ln(stdout, s->help_color, "2-3,6-9    - %s",
			 _("select multiple ranges"));
	color_fprintf_ln(stdout, s->help_color, "foo        - %s",
			 _("select item based on unique prefix"));
	color_fprintf_ln(stdout, s->help_color, "-...       - %s",
			 _("unselect specified items"));
	color_fprintf_ln(stdout, s->help_color, "*          - %s",
			 _("choose all items"));
	color_fprintf_ln(stdout, s->help_color, "           - %s",
			 _("(empty) finish selecting"));
}

typedef int (*command_t)(struct add_i_state *s, const struct pathspec *ps,
			 struct prefix_item_list *files,
			 struct list_and_choose_options *opts);

struct command_item {
	size_t prefix_length;
	command_t command;
};

struct print_command_item_data {
	const char *color, *reset;
};

static void print_command_item(int i, int selected,
			       struct string_list_item *item,
			       void *print_command_item_data)
{
	struct print_command_item_data *d = print_command_item_data;
	struct command_item *util = item->util;

	if (!util->prefix_length ||
	    !is_valid_prefix(item->string, util->prefix_length))
		printf(" %2d: %s", i + 1, item->string);
	else
		printf(" %2d: %s%.*s%s%s", i + 1,
		       d->color, (int)util->prefix_length, item->string,
		       d->reset, item->string + util->prefix_length);
}

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
	struct print_command_item_data data = { "[", "]" };
	struct list_and_choose_options main_loop_opts = {
		{ 4, N_("*** Commands ***"), print_command_item, &data },
		N_("What now"), SINGLETON | IMMEDIATE, command_prompt_help
	};
	struct {
		const char *string;
		command_t command;
	} command_list[] = {
		{ "status", run_status },
		{ "update", run_update },
		{ "revert", run_revert },
		{ "add untracked", run_add_untracked },
		{ "patch", run_patch },
		{ "diff", run_diff },
		{ "quit", NULL },
		{ "help", run_help },
	};
	struct prefix_item_list commands = PREFIX_ITEM_LIST_INIT;

	struct print_file_item_data print_file_item_data = {
		"%12s %12s %s", NULL, NULL,
		STRBUF_INIT, STRBUF_INIT, STRBUF_INIT, STRBUF_INIT
	};
	struct list_and_choose_options opts = {
		{ 0, NULL, print_file_item, &print_file_item_data },
		NULL, 0, choose_prompt_help
	};
	struct strbuf header = STRBUF_INIT;
	struct prefix_item_list files = PREFIX_ITEM_LIST_INIT;
	ssize_t i;
	int res = 0;

	for (i = 0; i < ARRAY_SIZE(command_list); i++) {
		struct command_item *util = xcalloc(1, sizeof(*util));
		util->command = command_list[i].command;
		string_list_append(&commands.items, command_list[i].string)
			->util = util;
	}

	init_add_i_state(&s, r);

	/*
	 * When color was asked for, use the prompt color for
	 * highlighting, otherwise use square brackets.
	 */
	if (s.use_color) {
		data.color = s.prompt_color;
		data.reset = s.reset_color;
	}
	print_file_item_data.color = data.color;
	print_file_item_data.reset = data.reset;

	strbuf_addstr(&header, "     ");
	strbuf_addf(&header, print_file_item_data.modified_fmt,
		    _("staged"), _("unstaged"), _("path"));
	opts.list_opts.header = header.buf;

	discard_index(r->index);
	if (repo_read_index(r) < 0 ||
	    repo_refresh_and_write_index(r, REFRESH_QUIET, 0, 1,
					 NULL, NULL, NULL) < 0)
		warning(_("could not refresh index"));

	res = run_status(&s, ps, &files, &opts);

	for (;;) {
		struct command_item *util;

		i = list_and_choose(&s, &commands, &main_loop_opts);
		if (i < 0 || i >= commands.items.nr)
			util = NULL;
		else
			util = commands.items.items[i].util;

		if (i == LIST_AND_CHOOSE_QUIT || (util && !util->command)) {
			printf(_("Bye.\n"));
			res = 0;
			break;
		}

		if (util)
			res = util->command(&s, ps, &files, &opts);
	}

	prefix_item_list_clear(&files);
	strbuf_release(&print_file_item_data.buf);
	strbuf_release(&print_file_item_data.name);
	strbuf_release(&print_file_item_data.index);
	strbuf_release(&print_file_item_data.worktree);
	strbuf_release(&header);
	prefix_item_list_clear(&commands);
	clear_add_i_state(&s);

	return res;
}
