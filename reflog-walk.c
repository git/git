#include "cache.h"
#include "commit.h"
#include "refs.h"
#include "diff.h"
#include "revision.h"
#include "string-list.h"
#include "reflog-walk.h"

struct complete_reflogs {
	char *ref;
	const char *short_ref;
	struct reflog_info {
		unsigned char osha1[20], nsha1[20];
		char *email;
		unsigned long timestamp;
		int tz;
		char *message;
	} *items;
	int nr, alloc;
};

static int read_one_reflog(unsigned char *osha1, unsigned char *nsha1,
		const char *email, unsigned long timestamp, int tz,
		const char *message, void *cb_data)
{
	struct complete_reflogs *array = cb_data;
	struct reflog_info *item;

	ALLOC_GROW(array->items, array->nr + 1, array->alloc);
	item = array->items + array->nr;
	hashcpy(item->osha1, osha1);
	hashcpy(item->nsha1, nsha1);
	item->email = xstrdup(email);
	item->timestamp = timestamp;
	item->tz = tz;
	item->message = xstrdup(message);
	array->nr++;
	return 0;
}

static struct complete_reflogs *read_complete_reflog(const char *ref)
{
	struct complete_reflogs *reflogs =
		xcalloc(1, sizeof(struct complete_reflogs));
	reflogs->ref = xstrdup(ref);
	for_each_reflog_ent(ref, read_one_reflog, reflogs);
	if (reflogs->nr == 0) {
		unsigned char sha1[20];
		const char *name;
		void *name_to_free;
		name = name_to_free = resolve_refdup(ref, RESOLVE_REF_READING,
						     sha1, NULL);
		if (name) {
			for_each_reflog_ent(name, read_one_reflog, reflogs);
			free(name_to_free);
		}
	}
	if (reflogs->nr == 0) {
		int len = strlen(ref);
		char *refname = xmalloc(len + 12);
		sprintf(refname, "refs/%s", ref);
		for_each_reflog_ent(refname, read_one_reflog, reflogs);
		if (reflogs->nr == 0) {
			sprintf(refname, "refs/heads/%s", ref);
			for_each_reflog_ent(refname, read_one_reflog, reflogs);
		}
		free(refname);
	}
	return reflogs;
}

static int get_reflog_recno_by_time(struct complete_reflogs *array,
	unsigned long timestamp)
{
	int i;
	for (i = array->nr - 1; i >= 0; i--)
		if (timestamp >= array->items[i].timestamp)
			return i;
	return -1;
}

struct commit_info_lifo {
	struct commit_info {
		struct commit *commit;
		void *util;
	} *items;
	int nr, alloc;
};

static struct commit_info *get_commit_info(struct commit *commit,
		struct commit_info_lifo *lifo, int pop)
{
	int i;
	for (i = 0; i < lifo->nr; i++)
		if (lifo->items[i].commit == commit) {
			struct commit_info *result = &lifo->items[i];
			if (pop) {
				if (i + 1 < lifo->nr)
					memmove(lifo->items + i,
						lifo->items + i + 1,
						(lifo->nr - i) *
						sizeof(struct commit_info));
				lifo->nr--;
			}
			return result;
		}
	return NULL;
}

static void add_commit_info(struct commit *commit, void *util,
		struct commit_info_lifo *lifo)
{
	struct commit_info *info;
	ALLOC_GROW(lifo->items, lifo->nr + 1, lifo->alloc);
	info = lifo->items + lifo->nr;
	info->commit = commit;
	info->util = util;
	lifo->nr++;
}

struct commit_reflog {
	int recno;
	enum selector_type {
		SELECTOR_NONE,
		SELECTOR_INDEX,
		SELECTOR_DATE
	} selector;
	struct complete_reflogs *reflogs;
};

struct reflog_walk_info {
	struct commit_info_lifo reflogs;
	struct string_list complete_reflogs;
	struct commit_reflog *last_commit_reflog;
};

void init_reflog_walk(struct reflog_walk_info **info)
{
	*info = xcalloc(1, sizeof(struct reflog_walk_info));
}

int add_reflog_for_walk(struct reflog_walk_info *info,
		struct commit *commit, const char *name)
{
	unsigned long timestamp = 0;
	int recno = -1;
	struct string_list_item *item;
	struct complete_reflogs *reflogs;
	char *branch, *at = strchr(name, '@');
	struct commit_reflog *commit_reflog;
	enum selector_type selector = SELECTOR_NONE;

	if (commit->object.flags & UNINTERESTING)
		die ("Cannot walk reflogs for %s", name);

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
			unsigned char sha1[20];
			free(branch);
			branch = resolve_refdup("HEAD", 0, sha1, NULL);
			if (!branch)
				die ("No current branch");

		}
		reflogs = read_complete_reflog(branch);
		if (!reflogs || reflogs->nr == 0) {
			unsigned char sha1[20];
			char *b;
			if (dwim_log(branch, strlen(branch), sha1, &b) == 1) {
				if (reflogs) {
					free(reflogs->ref);
					free(reflogs);
				}
				free(branch);
				branch = b;
				reflogs = read_complete_reflog(branch);
			}
		}
		if (!reflogs || reflogs->nr == 0)
			return -1;
		string_list_insert(&info->complete_reflogs, branch)->util
			= reflogs;
	}

	commit_reflog = xcalloc(1, sizeof(struct commit_reflog));
	if (recno < 0) {
		commit_reflog->recno = get_reflog_recno_by_time(reflogs, timestamp);
		if (commit_reflog->recno < 0) {
			free(branch);
			free(commit_reflog);
			return -1;
		}
	} else
		commit_reflog->recno = reflogs->nr - recno - 1;
	commit_reflog->selector = selector;
	commit_reflog->reflogs = reflogs;

	add_commit_info(commit, commit_reflog, &info->reflogs);
	return 0;
}

void fake_reflog_parent(struct reflog_walk_info *info, struct commit *commit)
{
	struct commit_info *commit_info =
		get_commit_info(commit, &info->reflogs, 0);
	struct commit_reflog *commit_reflog;
	struct reflog_info *reflog;

	info->last_commit_reflog = NULL;
	if (!commit_info)
		return;

	commit_reflog = commit_info->util;
	if (commit_reflog->recno < 0) {
		commit->parents = NULL;
		return;
	}

	reflog = &commit_reflog->reflogs->items[commit_reflog->recno];
	info->last_commit_reflog = commit_reflog;
	commit_reflog->recno--;
	commit_info->commit = (struct commit *)parse_object(reflog->osha1);
	if (!commit_info->commit) {
		commit->parents = NULL;
		return;
	}

	commit->parents = xcalloc(1, sizeof(struct commit_list));
	commit->parents->item = commit_info->commit;
}

void get_reflog_selector(struct strbuf *sb,
			 struct reflog_walk_info *reflog_info,
			 const struct date_mode *dmode, int force_date,
			 int shorten)
{
	struct commit_reflog *commit_reflog = reflog_info->last_commit_reflog;
	struct reflog_info *info;
	const char *printed_ref;

	if (!commit_reflog)
		return;

	if (shorten) {
		if (!commit_reflog->reflogs->short_ref)
			commit_reflog->reflogs->short_ref
				= shorten_unambiguous_ref(commit_reflog->reflogs->ref, 0);
		printed_ref = commit_reflog->reflogs->short_ref;
	} else {
		printed_ref = commit_reflog->reflogs->ref;
	}

	strbuf_addf(sb, "%s@{", printed_ref);
	if (commit_reflog->selector == SELECTOR_DATE ||
	    (commit_reflog->selector == SELECTOR_NONE && force_date)) {
		info = &commit_reflog->reflogs->items[commit_reflog->recno+1];
		strbuf_addstr(sb, show_date(info->timestamp, info->tz, dmode));
	} else {
		strbuf_addf(sb, "%d", commit_reflog->reflogs->nr
			    - 2 - commit_reflog->recno);
	}

	strbuf_addch(sb, '}');
}

void get_reflog_message(struct strbuf *sb,
			struct reflog_walk_info *reflog_info)
{
	struct commit_reflog *commit_reflog = reflog_info->last_commit_reflog;
	struct reflog_info *info;
	size_t len;

	if (!commit_reflog)
		return;

	info = &commit_reflog->reflogs->items[commit_reflog->recno+1];
	len = strlen(info->message);
	if (len > 0)
		len--; /* strip away trailing newline */
	strbuf_add(sb, info->message, len);
}

const char *get_reflog_ident(struct reflog_walk_info *reflog_info)
{
	struct commit_reflog *commit_reflog = reflog_info->last_commit_reflog;
	struct reflog_info *info;

	if (!commit_reflog)
		return NULL;

	info = &commit_reflog->reflogs->items[commit_reflog->recno+1];
	return info->email;
}

void show_reflog_message(struct reflog_walk_info *reflog_info, int oneline,
			 const struct date_mode *dmode, int force_date)
{
	if (reflog_info && reflog_info->last_commit_reflog) {
		struct commit_reflog *commit_reflog = reflog_info->last_commit_reflog;
		struct reflog_info *info;
		struct strbuf selector = STRBUF_INIT;

		info = &commit_reflog->reflogs->items[commit_reflog->recno+1];
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
