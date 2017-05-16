#include "cache.h"
#include "add-interactive.h"
#include "diffcore.h"
#include "revision.h"
#include "refs.h"
#include "string-list.h"

struct add_i_state {
	struct repository *r;
};

static void init_add_i_state(struct add_i_state *s, struct repository *r)
{
       s->r = r;
}

struct list_options {
	const char *header;
	void (*print_item)(int i, struct string_list_item *item, void *print_item_data);
	void *print_item_data;
};

static void list(struct string_list *list, struct list_options *opts)
{
	int i;

	if (!list->nr)
		return;

	if (opts->header)
		printf("%s\n", opts->header);

	for (i = 0; i < list->nr; i++) {
		opts->print_item(i, list->items + i, opts->print_item_data);
		putchar('\n');
	}
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

	list(files, opts);
	putchar('\n');

	return 0;
}

int run_add_i(struct repository *r, const struct pathspec *ps)
{
	struct add_i_state s = { NULL };
	struct print_file_item_data print_file_item_data = {
		"%12s %12s %s", STRBUF_INIT, STRBUF_INIT, STRBUF_INIT
	};
	struct list_options opts = {
		NULL, print_file_item, &print_file_item_data
	};
	struct strbuf header = STRBUF_INIT;
	struct string_list files = STRING_LIST_INIT_DUP;
	int res = 0;

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

	string_list_clear(&files, 1);
	strbuf_release(&print_file_item_data.buf);
	strbuf_release(&print_file_item_data.index);
	strbuf_release(&print_file_item_data.worktree);
	strbuf_release(&header);

	return res;
}
