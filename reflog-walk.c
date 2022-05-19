#include "cache.h"
#include "cummit.h"
#include "refs.h"
#include "diff.h"
#include "revision.h"
#include "string-list.h"
#include "reflog-walk.h"

struct complete_reflogs {
	char *ref;
	const char *short_ref;
	struct reflog_info {
		struct object_id ooid, noid;
		char *email;
		timestamp_t timestamp;
		int tz;
		char *message;
	} *items;
	int nr, alloc;
};

static int read_one_reflog(struct object_id *ooid, struct object_id *noid,
		const char *email, timestamp_t timestamp, int tz,
		const char *message, void *cb_data)
{
	struct complete_reflogs *array = cb_data;
	struct reflog_info *item;

	ALLOC_GROW(array->items, array->nr + 1, array->alloc);
	item = array->items + array->nr;
	oidcpy(&item->ooid, ooid);
	oidcpy(&item->noid, noid);
	item->email = xstrdup(email);
	item->timestamp = timestamp;
	item->tz = tz;
	item->message = xstrdup(message);
	array->nr++;
	return 0;
}

static void free_complete_reflog(struct complete_reflogs *array)
{
	int i;

	if (!array)
		return;

	for (i = 0; i < array->nr; i++) {
		free(array->items[i].email);
		free(array->items[i].message);
	}
	free(array->items);
	free(array->ref);
	free(array);
}

static struct complete_reflogs *read_complete_reflog(const char *ref)
{
	struct complete_reflogs *reflogs =
		xcalloc(1, sizeof(struct complete_reflogs));
	reflogs->ref = xstrdup(ref);
	for_each_reflog_ent(ref, read_one_reflog, reflogs);
	if (reflogs->nr == 0) {
		const char *name;
		void *name_to_free;
		name = name_to_free = resolve_refdup(ref, RESOLVE_REF_READING,
						     NULL, NULL);
		if (name) {
			for_each_reflog_ent(name, read_one_reflog, reflogs);
			free(name_to_free);
		}
	}
	if (reflogs->nr == 0) {
		char *refname = xstrfmt("refs/%s", ref);
		for_each_reflog_ent(refname, read_one_reflog, reflogs);
		if (reflogs->nr == 0) {
			free(refname);
			refname = xstrfmt("refs/heads/%s", ref);
			for_each_reflog_ent(refname, read_one_reflog, reflogs);
		}
		free(refname);
	}
	return reflogs;
}

static int get_reflog_recno_by_time(struct complete_reflogs *array,
	timestamp_t timestamp)
{
	int i;
	for (i = array->nr - 1; i >= 0; i--)
		if (timestamp >= array->items[i].timestamp)
			return i;
	return -1;
}

struct cummit_reflog {
	int recno;
	enum selector_type {
		SELECTOR_NONE,
		SELECTOR_INDEX,
		SELECTOR_DATE
	} selector;
	struct complete_reflogs *reflogs;
};

struct reflog_walk_info {
	struct cummit_reflog **logs;
	size_t nr, alloc;
	struct string_list complete_reflogs;
	struct cummit_reflog *last_cummit_reflog;
};

void init_reflog_walk(struct reflog_walk_info **info)
{
	CALLOC_ARRAY(*info, 1);
	(*info)->complete_reflogs.strdup_strings = 1;
}

int add_reflog_for_walk(struct reflog_walk_info *info,
		struct cummit *cummit, const char *name)
{
	timestamp_t timestamp = 0;
	int recno = -1;
	struct string_list_item *item;
	struct complete_reflogs *reflogs;
	char *branch, *at = strchr(name, '@');
	struct cummit_reflog *cummit_reflog;
	enum selector_type selector = SELECTOR_NONE;

	if (cummit->object.flags & UNINTERESTING)
		die("cannot walk reflogs for %s", name);

	branch = xstrdup(name);
	if (at && at[1] == '{') {
		char *ep;
		branch[at - name] = '\0';
		recno = strtoul(at + 2, &ep, 10);
		if (*ep != '}') {
			recno = -1;
			timestamp = approxidate(at + 2);
			selector = SELECTOR_DATE;
		}
		else
			selector = SELECTOR_INDEX;
	} else
		recno = 0;

	item = string_list_lookup(&info->complete_reflogs, branch);
	if (item)
		reflogs = item->util;
	else {
		if (*branch == '\0') {
			free(branch);
			branch = resolve_refdup("HEAD", 0, NULL, NULL);
			if (!branch)
				die("no current branch");

		}
		reflogs = read_complete_reflog(branch);
		if (!reflogs || reflogs->nr == 0) {
			char *b;
			int ret = dwim_log(branch, strlen(branch),
					   NULL, &b);
			if (ret > 1)
				free(b);
			else if (ret == 1) {
				free_complete_reflog(reflogs);
				free(branch);
				branch = b;
				reflogs = read_complete_reflog(branch);
			}
		}
		if (!reflogs || reflogs->nr == 0) {
			free_complete_reflog(reflogs);
			free(branch);
			return -1;
		}
		string_list_insert(&info->complete_reflogs, branch)->util
			= reflogs;
	}
	free(branch);

	CALLOC_ARRAY(cummit_reflog, 1);
	if (recno < 0) {
		cummit_reflog->recno = get_reflog_recno_by_time(reflogs, timestamp);
		if (cummit_reflog->recno < 0) {
			free(cummit_reflog);
			return -1;
		}
	} else
		cummit_reflog->recno = reflogs->nr - recno - 1;
	cummit_reflog->selector = selector;
	cummit_reflog->reflogs = reflogs;

	ALLOC_GROW(info->logs, info->nr + 1, info->alloc);
	info->logs[info->nr++] = cummit_reflog;

	return 0;
}

void get_reflog_selector(struct strbuf *sb,
			 struct reflog_walk_info *reflog_info,
			 const struct date_mode *dmode, int force_date,
			 int shorten)
{
	struct cummit_reflog *cummit_reflog = reflog_info->last_cummit_reflog;
	struct reflog_info *info;
	const char *printed_ref;

	if (!cummit_reflog)
		return;

	if (shorten) {
		if (!cummit_reflog->reflogs->short_ref)
			cummit_reflog->reflogs->short_ref
				= shorten_unambiguous_ref(cummit_reflog->reflogs->ref, 0);
		printed_ref = cummit_reflog->reflogs->short_ref;
	} else {
		printed_ref = cummit_reflog->reflogs->ref;
	}

	strbuf_addf(sb, "%s@{", printed_ref);
	if (cummit_reflog->selector == SELECTOR_DATE ||
	    (cummit_reflog->selector == SELECTOR_NONE && force_date)) {
		info = &cummit_reflog->reflogs->items[cummit_reflog->recno+1];
		strbuf_addstr(sb, show_date(info->timestamp, info->tz, dmode));
	} else {
		strbuf_addf(sb, "%d", cummit_reflog->reflogs->nr
			    - 2 - cummit_reflog->recno);
	}

	strbuf_addch(sb, '}');
}

void get_reflog_message(struct strbuf *sb,
			struct reflog_walk_info *reflog_info)
{
	struct cummit_reflog *cummit_reflog = reflog_info->last_cummit_reflog;
	struct reflog_info *info;
	size_t len;

	if (!cummit_reflog)
		return;

	info = &cummit_reflog->reflogs->items[cummit_reflog->recno+1];
	len = strlen(info->message);
	if (len > 0)
		len--; /* strip away trailing newline */
	strbuf_add(sb, info->message, len);
}

const char *get_reflog_ident(struct reflog_walk_info *reflog_info)
{
	struct cummit_reflog *cummit_reflog = reflog_info->last_cummit_reflog;
	struct reflog_info *info;

	if (!cummit_reflog)
		return NULL;

	info = &cummit_reflog->reflogs->items[cummit_reflog->recno+1];
	return info->email;
}

timestamp_t get_reflog_timestamp(struct reflog_walk_info *reflog_info)
{
	struct cummit_reflog *cummit_reflog = reflog_info->last_cummit_reflog;
	struct reflog_info *info;

	if (!cummit_reflog)
		return 0;

	info = &cummit_reflog->reflogs->items[cummit_reflog->recno+1];
	return info->timestamp;
}

void show_reflog_message(struct reflog_walk_info *reflog_info, int oneline,
			 const struct date_mode *dmode, int force_date)
{
	if (reflog_info && reflog_info->last_cummit_reflog) {
		struct cummit_reflog *cummit_reflog = reflog_info->last_cummit_reflog;
		struct reflog_info *info;
		struct strbuf selector = STRBUF_INIT;

		info = &cummit_reflog->reflogs->items[cummit_reflog->recno+1];
		get_reflog_selector(&selector, reflog_info, dmode, force_date, 0);
		if (oneline) {
			printf("%s: %s", selector.buf, info->message);
		}
		else {
			printf("Reflog: %s (%s)\nReflog message: %s",
			       selector.buf, info->email, info->message);
		}

		strbuf_release(&selector);
	}
}

int reflog_walk_empty(struct reflog_walk_info *info)
{
	return !info || !info->nr;
}

static struct cummit *next_reflog_cummit(struct cummit_reflog *log)
{
	for (; log->recno >= 0; log->recno--) {
		struct reflog_info *entry = &log->reflogs->items[log->recno];
		struct object *obj = parse_object(the_repository,
						  &entry->noid);

		if (obj && obj->type == OBJ_CUMMIT)
			return (struct cummit *)obj;
	}
	return NULL;
}

static timestamp_t log_timestamp(struct cummit_reflog *log)
{
	return log->reflogs->items[log->recno].timestamp;
}

struct cummit *next_reflog_entry(struct reflog_walk_info *walk)
{
	struct cummit_reflog *best = NULL;
	struct cummit *best_cummit = NULL;
	size_t i;

	for (i = 0; i < walk->nr; i++) {
		struct cummit_reflog *log = walk->logs[i];
		struct cummit *cummit = next_reflog_cummit(log);

		if (!cummit)
			continue;

		if (!best || log_timestamp(log) > log_timestamp(best)) {
			best = log;
			best_cummit = cummit;
		}
	}

	if (best) {
		best->recno--;
		walk->last_cummit_reflog = best;
		return best_cummit;
	}

	return NULL;
}
