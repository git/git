#include "cache.h"
#include "add-interactive.h"
#include "color.h"
#include "config.h"
#include "diffcore.h"
#include "revision.h"
#include "refs.h"
#include "prefix-map.h"
#include "lockfile.h"
#include "pathspec.h"
#include "dir.h"
#include "argv-array.h"
#include "run-command.h"

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

int init_add_i_state(struct repository *r, struct add_i_state *s)
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

	strlcpy(s->fraginfo_color,
		diff_get_color(s->use_color, DIFF_FRAGINFO), COLOR_MAXLEN);
	strlcpy(s->context_color,
		diff_get_color(s->use_color, DIFF_CONTEXT), COLOR_MAXLEN);
	strlcpy(s->file_old_color,
		diff_get_color(s->use_color, DIFF_FILE_OLD), COLOR_MAXLEN);
	strlcpy(s->file_new_color,
		diff_get_color(s->use_color, DIFF_FILE_NEW), COLOR_MAXLEN);

	free(s->interactive_diff_filter);
	if (git_config_get_string("interactive.difffilter",
				  &s->interactive_diff_filter))
		s->interactive_diff_filter = NULL;

	free(s->interactive_diff_algorithm);
	if (git_config_get_string("diff.algorithm",
				  &s->interactive_diff_algorithm))
		s->interactive_diff_algorithm = NULL;

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
	void (*print_item)(int i, int selected, struct prefix_item *item,
			   void *print_item_data);
	void *print_item_data;
};

static void list(struct prefix_item **list, int *selected, size_t nr,
		 struct add_i_state *s, struct list_options *opts)
{
	int i, last_lf = 0;

	if (!nr)
		return;

	if (opts->header)
		color_fprintf_ln(stdout, s->header_color,
				 "%s", opts->header);

	for (i = 0; i < nr; i++) {
		opts->print_item(i, selected ? selected[i] : 0, list[i],
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
static ssize_t list_and_choose(struct prefix_item **items, int *selected,
			       size_t nr, struct add_i_state *s,
			       struct list_and_choose_options *opts)
{
	int singleton = opts->flags & SINGLETON;
	int immediate = opts->flags & IMMEDIATE;

	struct strbuf input = STRBUF_INIT;
	ssize_t res = singleton ? LIST_AND_CHOOSE_ERROR : 0;

	if (!selected && !singleton)
		BUG("need a selected array in non-singleton mode");

	if (singleton && !immediate)
		BUG("singleton requires immediate");

	find_unique_prefixes(items, nr, 1, 4);

	for (;;) {
		char *p, *endp;

		strbuf_reset(&input);

		list(items, selected, nr, s, &opts->list_opts);

		color_fprintf(stdout, s->prompt_color, "%s", opts->prompt);
		fputs(singleton ? "> " : ">> ", stdout);
		fflush(stdout);

		if (strbuf_getline(&input, stdin) == EOF) {
			putchar('\n');
			if (immediate)
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
			int choose = 1;
			/* `from` is inclusive, `to` is exclusive */
			ssize_t from = -1, to = -1;

			if (!sep) {
				if (!*p)
					break;
				p++;
				continue;
			}

			/* Input that begins with '-'; unchoose */
			if (*p == '-') {
				choose = 0;
				p++;
				sep--;
			}

			if (sep == 1 && *p == '*') {
				from = 0;
				to = nr;
			} else if (isdigit(*p)) {
				/* A range can be specified like 5-7 or 5-. */
				from = strtoul(p, &endp, 10) - 1;
				if (endp == p + sep)
					to = from + 1;
				else if (*endp == '-') {
					to = strtoul(++endp, &endp, 10);
					/* extra characters after the range? */
					if (endp != p + sep)
						from = -1;
				}
			}

			p[sep] = '\0';
			if (from < 0) {
				from = find_unique(p, items, nr);
				if (from >= 0)
					to = from + 1;
			}

			if (from < 0 || from >= nr ||
			    (singleton && from + 1 != to)) {
				color_fprintf_ln(stdout, s->error_color,
						 _("Huh (%s)?"), p);
				break;
			} else if (singleton) {
				res = from;
				break;
			}

			if (to > nr)
				to = nr;

			for (; from < to; from++)
				if (selected[from] != choose) {
					selected[from] = choose;
					res += choose ? +1 : -1;
				}

			p += sep + 1;
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

	unsigned skip_unseen:1;
	size_t unmerged_count, binary_count;
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
		struct adddel *adddel, *other_adddel;

		entry = hashmap_get_from_hash(&s->file_map, hash, name);
		if (entry) {
			if (entry->index == (size_t)-1)
				continue;
			file_index = entry->index;
		} else if (s->skip_unseen)
			continue;
		else {
			FLEX_ALLOC_STR(entry, pathname, name);
			hashmap_entry_init(entry, hash);
			entry->index = file_index = s->list->nr;
			hashmap_add(&s->file_map, entry);

			add_file_item(s->list, name);
		}
		file = s->list->file[file_index];

		adddel = s->phase == FROM_INDEX ? &file->index : &file->worktree;
		other_adddel = s->phase == FROM_INDEX ? &file->worktree : &file->index;
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
}

enum modified_files_filter {
	NO_FILTER = 0,
	WORKTREE_ONLY = 1,
	INDEX_ONLY = 2,
};

static int get_modified_files(struct repository *r,
			      enum modified_files_filter filter,
			      size_t *unmerged_count,
			      size_t *binary_count,
			      struct file_list *list,
			      const struct pathspec *ps)
{
	struct object_id head_oid;
	int is_initial = !resolve_ref_unsafe("HEAD", RESOLVE_REF_READING,
					     &head_oid, NULL);
	struct collection_status s = { FROM_WORKTREE };
	int i;

	if (repo_read_index_preload(r, ps, 0) < 0)
		return error(_("could not read index"));

	s.list = list;
	hashmap_init(&s.file_map, pathname_entry_cmp, NULL, 0);

	for (i = 0; i < 2; i++) {
		struct rev_info rev;
		struct setup_revision_opt opt = { 0 };

		if (filter == INDEX_ONLY)
			s.phase = i ? FROM_WORKTREE : FROM_INDEX;
		else
			s.phase = i ? FROM_INDEX : FROM_WORKTREE;
		s.skip_unseen = filter && i;

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
	if (unmerged_count)
		*unmerged_count = s.unmerged_count;
	if (binary_count)
		*binary_count = s.binary_count;

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
	const char *modified_fmt, *color, *reset;
	struct strbuf buf, name, index, worktree;
	unsigned only_names:1;
};

static void print_file_item(int i, int selected, struct prefix_item *item,
			    void *print_file_item_data)
{
	struct file_item *c = (struct file_item *)item;
	struct print_file_item_data *d = print_file_item_data;
	const char *highlighted = NULL;

	strbuf_reset(&d->index);
	strbuf_reset(&d->worktree);
	strbuf_reset(&d->buf);

	/* Format the item with the prefix highlighted. */
	if (item->prefix_length > 0 &&
	    is_valid_prefix(item->name, item->prefix_length)) {
		strbuf_reset(&d->name);
		strbuf_addf(&d->name, "%s%.*s%s%s", d->color,
			    (int)item->prefix_length, item->name, d->reset,
			    item->name + item->prefix_length);
		highlighted = d->name.buf;
	}

	if (d->only_names) {
		printf("%c%2d: %s", selected ? '*' : ' ', i + 1,
		       highlighted ? highlighted : item->name);
		return;
	}

	populate_wi_changes(&d->worktree, &c->worktree, _("nothing"));
	populate_wi_changes(&d->index, &c->index, _("unchanged"));
	strbuf_addf(&d->buf, d->modified_fmt,
		    d->index.buf, d->worktree.buf,
		    highlighted ? highlighted : item->name);

	printf("%c%2d: %s", selected ? '*' : ' ', i + 1, d->buf.buf);
}

static int run_status(struct add_i_state *s, const struct pathspec *ps,
		      struct file_list *files,
		      struct list_and_choose_options *opts)
{
	reset_file_list(files);

	if (get_modified_files(s->r, 0, NULL, NULL, files, ps) < 0)
		return -1;

	if (files->nr)
		list((struct prefix_item **)files->file, NULL, files->nr,
		     s, &opts->list_opts);
	putchar('\n');

	return 0;
}

static int run_update(struct add_i_state *s, const struct pathspec *ps,
		      struct file_list *files,
		      struct list_and_choose_options *opts)
{
	int res = 0, fd, *selected = NULL;
	size_t count, i;
	struct lock_file index_lock;

	reset_file_list(files);

	if (get_modified_files(s->r, WORKTREE_ONLY, NULL, NULL, files, ps) < 0)
		return -1;

	if (!files->nr) {
		putchar('\n');
		return 0;
	}

	opts->prompt = N_("Update");
	CALLOC_ARRAY(selected, files->nr);

	count = list_and_choose((struct prefix_item **)files->file,
				selected, files->nr, s, opts);
	if (count <= 0) {
		putchar('\n');
		free(selected);
		return 0;
	}

	fd = repo_hold_locked_index(s->r, &index_lock, LOCK_REPORT_ON_ERROR);
	if (fd < 0) {
		putchar('\n');
		free(selected);
		return -1;
	}

	for (i = 0; i < files->nr; i++) {
		const char *name = files->file[i]->item.name;
		if (selected[i] &&
		    add_file_to_index(s->r->index, name, 0) < 0) {
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
	free(selected);
	return res;
}

static void revert_from_diff(struct diff_queue_struct *q,
			     struct diff_options *opt, void *data)
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
		      struct file_list *files,
		      struct list_and_choose_options *opts)
{
	int res = 0, fd, *selected = NULL;
	size_t count, i, j;

	struct object_id oid;
	int is_initial = !resolve_ref_unsafe("HEAD", RESOLVE_REF_READING, &oid,
					     NULL);
	struct lock_file index_lock;
	const char **paths;
	struct tree *tree;
	struct diff_options diffopt = { NULL };

	reset_file_list(files);
	if (get_modified_files(s->r, INDEX_ONLY, NULL, NULL, files, ps) < 0)
		return -1;

	if (!files->nr) {
		putchar('\n');
		return 0;
	}

	opts->prompt = N_("Revert");
	CALLOC_ARRAY(selected, files->nr);

	count = list_and_choose((struct prefix_item **)files->file,
				selected, files->nr, s, opts);
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
	for (i = j = 0; i < files->nr; i++)
		if (selected[i])
			paths[j++] = files->file[i]->item.name;
	paths[j] = NULL;

	parse_pathspec(&diffopt.pathspec, 0,
		       PATHSPEC_PREFER_FULL | PATHSPEC_LITERAL_PATH,
		       NULL, paths);

	diffopt.output_format = DIFF_FORMAT_CALLBACK;
	diffopt.format_callback = revert_from_diff;
	diffopt.flags.override_submodule_config = 1;
	diffopt.repo = s->r;

	if (do_diff_cache(&oid, &diffopt))
		res = -1;
	else {
		diffcore_std(&diffopt);
		diff_flush(&diffopt);
	}
	free(paths);
	clear_pathspec(&diffopt.pathspec);

	if (!res && write_locked_index(s->r->index, &index_lock,
				       COMMIT_LOCK) < 0)
		res = -1;
	else
		res = repo_refresh_and_write_index(s->r, REFRESH_QUIET, 1);
	if (!res)
		printf(Q_("reverted %d path\n",
			  "reverted %d paths\n", count), (int)count);

finish_revert:
	putchar('\n');
	free(selected);
	return res;
}

static int get_untracked_files(struct repository *r, struct file_list *list,
			       const struct pathspec *ps)
{
	struct dir_struct dir = { 0 };
	size_t i;

	if (repo_read_index(r) < 0)
		return error(_("could not read index"));

	setup_standard_excludes(&dir);
	add_exclude_list(&dir, EXC_CMDL, "--exclude option");
	fill_directory(&dir, r->index, ps);

	for (i = 0; i < dir.nr; i++) {
		struct dir_entry *ent = dir.entries[i];

		if (index_name_is_other(r->index, ent->name, ent->len)) {
			struct file_item *item;

			FLEXPTR_ALLOC_MEM(item, item.name, ent->name, ent->len);

			ALLOC_GROW(list->file, list->nr + 1, list->alloc);
			list->file[list->nr++] = item;
		}
	}

	return 0;
}

static int run_add_untracked(struct add_i_state *s, const struct pathspec *ps,
		      struct file_list *files,
		      struct list_and_choose_options *opts)
{
	struct print_file_item_data *d = opts->list_opts.print_item_data;
	int res = 0, fd, *selected = NULL;
	size_t count, i;

	struct lock_file index_lock;

	reset_file_list(files);
	if (get_untracked_files(s->r, files, ps) < 0)
		return -1;

	if (!files->nr) {
		printf(_("No untracked files.\n"));
		goto finish_add_untracked;
	}

	opts->prompt = N_("Add untracked");
	CALLOC_ARRAY(selected, files->nr);

	d->only_names = 1;
	count = list_and_choose((struct prefix_item **)files->file,
				selected, files->nr, s, opts);
	d->only_names = 0;
	if (count <= 0)
		goto finish_add_untracked;

	fd = repo_hold_locked_index(s->r, &index_lock, LOCK_REPORT_ON_ERROR);
	if (fd < 0) {
		res = -1;
		goto finish_add_untracked;
	}

	for (i = 0; i < files->nr; i++) {
		const char *name = files->file[i]->item.name;
		if (selected[i] &&
		    add_file_to_index(s->r->index, name, 0) < 0) {
			res = error(_("could not stage '%s'"), name);
			break;
		}
	}

	if (!res && write_locked_index(s->r->index, &index_lock, COMMIT_LOCK) < 0)
		res = error(_("could not write index"));

	if (!res)
		printf(Q_("added %d path\n",
			  "added %d paths\n", count), (int)count);

finish_add_untracked:
	putchar('\n');
	free(selected);
	return res;
}

static int run_patch(struct add_i_state *s, const struct pathspec *ps,
		     struct file_list *files,
		     struct list_and_choose_options *opts)
{
	struct prefix_item **items = (struct prefix_item **)files->file;
	int res = 0, *selected = NULL;
	ssize_t count, i, j;
	size_t unmerged_count = 0, binary_count = 0;

	reset_file_list(files);
	if (get_modified_files(s->r, WORKTREE_ONLY, &unmerged_count,
			       &binary_count, files, ps) < 0)
		return -1;

	if (unmerged_count || binary_count) {
		for (i = j = 0; i < files->nr; i++)
			if (files->file[i]->index.binary ||
			    files->file[i]->worktree.binary)
				free(items[i]);
			else if (files->file[i]->index.unmerged ||
				 files->file[i]->worktree.unmerged) {
				color_fprintf_ln(stderr, s->error_color,
						 _("ignoring unmerged: %s"),
						 files->file[i]->item.name);
				free(items[i]);
			} else
				items[j++] = items[i];
		files->nr = j;
	}

	if (!files->nr) {
		if (binary_count)
			fprintf(stderr, _("Only binary files changed.\n"));
		else
			fprintf(stderr, _("No changes.\n"));
		return 0;
	}

	opts->prompt = N_("Patch update");
	CALLOC_ARRAY(selected, files->nr);

	count = list_and_choose(items, selected, files->nr, s, opts);
	if (count >= 0) {
		struct argv_array args = ARGV_ARRAY_INIT;
		struct pathspec ps_selected = { 0 };

		for (i = 0; i < files->nr; i++)
			if (selected[i])
				argv_array_push(&args, items[i]->name);
		parse_pathspec(&ps_selected,
			       PATHSPEC_ALL_MAGIC & ~PATHSPEC_LITERAL,
			       PATHSPEC_LITERAL_PATH, "", args.argv);
		res = run_add_p(s->r, ADD_P_STAGE, NULL, &ps_selected);
		argv_array_clear(&args);
		clear_pathspec(&ps_selected);
	}

	free(selected);
	return res;
}

static int run_diff(struct add_i_state *s, const struct pathspec *ps,
		    struct file_list *files,
		    struct list_and_choose_options *opts)
{
	struct prefix_item **items = (struct prefix_item **)files->file;
	int res = 0, *selected = NULL;
	ssize_t count, i;

	struct object_id oid;
	int is_initial = !resolve_ref_unsafe("HEAD", RESOLVE_REF_READING, &oid,
					     NULL);
	reset_file_list(files);
	if (get_modified_files(s->r, INDEX_ONLY, NULL, NULL, files, ps) < 0)
		return -1;

	if (!files->nr) {
		putchar('\n');
		return 0;
	}

	opts->prompt = N_("Review diff");
	CALLOC_ARRAY(selected, files->nr);

	opts->flags = IMMEDIATE;
	count = list_and_choose(items, selected, files->nr, s, opts);
	opts->flags = 0;
	if (count >= 0) {
		struct argv_array args = ARGV_ARRAY_INIT;

		argv_array_pushl(&args, "git", "diff", "-p", "--cached",
				 oid_to_hex(!is_initial ? &oid :
					    s->r->hash_algo->empty_tree),
				 "--", NULL);
		for (i = 0; i < files->nr; i++)
			if (selected[i])
				argv_array_push(&args, items[i]->name);
		res = run_command_v_opt(args.argv, 0);
		argv_array_clear(&args);
	}

	putchar('\n');
	free(selected);
	return res;
}

static int run_help(struct add_i_state *s, const struct pathspec *ps,
		    struct file_list *files,
		    struct list_and_choose_options *opts)
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

static void choose_prompt_help(struct add_i_state *s)
{
	const char *help_color = s->help_color;
	color_fprintf_ln(stdout, help_color, "%s",
			 _("Prompt help:"));
	color_fprintf_ln(stdout, help_color, "1          - %s",
			 _("select a single item"));
	color_fprintf_ln(stdout, help_color, "3-5        - %s",
			 _("select a range of items"));
	color_fprintf_ln(stdout, help_color, "2-3,6-9    - %s",
			 _("select multiple ranges"));
	color_fprintf_ln(stdout, help_color, "foo        - %s",
			 _("select item based on unique prefix"));
	color_fprintf_ln(stdout, help_color, "-...       - %s",
			 _("unselect specified items"));
	color_fprintf_ln(stdout, help_color, "*          - %s",
			 _("choose all items"));
	color_fprintf_ln(stdout, help_color, "           - %s",
			 _("(empty) finish selecting"));
}

struct print_command_item_data {
	const char *color, *reset;
};

static void print_command_item(int i, int selected, struct prefix_item *item,
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
		       struct file_list *files,
		       struct list_and_choose_options *opts);
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
		N_("What now"), SINGLETON | IMMEDIATE, command_prompt_help
	};
	struct command_item
		status = { { "status" }, run_status },
		update = { { "update" }, run_update },
		revert = { { "revert" }, run_revert },
		add_untracked = { { "add untracked" }, run_add_untracked },
		patch = { { "patch" }, run_patch },
		diff = { { "diff" }, run_diff },
		quit = { { "quit" }, NULL },
		help = { { "help" }, run_help };
	struct command_item *commands[] = {
		&status, &update, &revert, &add_untracked,
		&patch, &diff, &quit, &help
	};

	struct print_file_item_data print_file_item_data = {
		"%12s %12s %s", NULL, NULL,
		STRBUF_INIT, STRBUF_INIT, STRBUF_INIT, STRBUF_INIT
	};
	struct list_and_choose_options opts = {
		{ 0, NULL, print_file_item, &print_file_item_data },
		NULL, 0, choose_prompt_help
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
	print_file_item_data.color = data.color;
	print_file_item_data.reset = data.reset;

	strbuf_addstr(&header, "      ");
	strbuf_addf(&header, print_file_item_data.modified_fmt,
		    _("staged"), _("unstaged"), _("path"));
	opts.list_opts.header = header.buf;

	repo_refresh_and_write_index(r, REFRESH_QUIET, 1);
	if (run_status(&s, ps, &files, &opts) < 0)
		res = -1;

	for (;;) {
		i = list_and_choose((struct prefix_item **)commands, NULL,
				    ARRAY_SIZE(commands), &s, &main_loop_opts);
		if (i == LIST_AND_CHOOSE_QUIT ||
		    (i != LIST_AND_CHOOSE_ERROR && !commands[i]->command)) {
			printf(_("Bye.\n"));
			res = 0;
			break;
		}
		if (i != LIST_AND_CHOOSE_ERROR)
			res = commands[i]->command(&s, ps, &files, &opts);
	}

	release_file_list(&files);
	strbuf_release(&print_file_item_data.buf);
	strbuf_release(&print_file_item_data.name);
	strbuf_release(&print_file_item_data.index);
	strbuf_release(&print_file_item_data.worktree);
	strbuf_release(&header);

	return res;
}
