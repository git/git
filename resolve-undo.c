#include "cache.h"
#include "resolve-undo.h"
#include "string-list.h"

/* The only error case is to run out of memory in string-list */
void record_resolve_undo(struct index_state *istate, struct cache_entry *ce)
{
	struct string_list_item *lost;
	struct resolve_undo_info *ui;
	struct string_list *resolve_undo;
	int stage = ce_stage(ce);

	if (!stage)
		return;

	if (!istate->resolve_undo) {
		resolve_undo = xcalloc(1, sizeof(*resolve_undo));
		resolve_undo->strdup_strings = 1;
		istate->resolve_undo = resolve_undo;
	}
	resolve_undo = istate->resolve_undo;
	lost = string_list_insert(ce->name, resolve_undo);
	if (!lost->util)
		lost->util = xcalloc(1, sizeof(*ui));
	ui = lost->util;
	hashcpy(ui->sha1[stage - 1], ce->sha1);
	ui->mode[stage - 1] = ce->ce_mode;
}

static int write_one(struct string_list_item *item, void *cbdata)
{
	struct strbuf *sb = cbdata;
	struct resolve_undo_info *ui = item->util;
	int i;

	if (!ui)
		return 0;
	strbuf_addstr(sb, item->string);
	strbuf_addch(sb, 0);
	for (i = 0; i < 3; i++)
		strbuf_addf(sb, "%o%c", ui->mode[i], 0);
	for (i = 0; i < 3; i++) {
		if (!ui->mode[i])
			continue;
		strbuf_add(sb, ui->sha1[i], 20);
	}
	return 0;
}

void resolve_undo_write(struct strbuf *sb, struct string_list *resolve_undo)
{
	for_each_string_list(write_one, resolve_undo, sb);
}

struct string_list *resolve_undo_read(void *data, unsigned long size)
{
	struct string_list *resolve_undo;
	size_t len;
	char *endptr;
	int i;

	resolve_undo = xcalloc(1, sizeof(*resolve_undo));
	resolve_undo->strdup_strings = 1;

	while (size) {
		struct string_list_item *lost;
		struct resolve_undo_info *ui;

		len = strlen(data) + 1;
		if (size <= len)
			goto error;
		lost = string_list_insert(data, resolve_undo);
		if (!lost->util)
			lost->util = xcalloc(1, sizeof(*ui));
		ui = lost->util;
		size -= len;
		data += len;

		for (i = 0; i < 3; i++) {
			ui->mode[i] = strtoul(data, &endptr, 8);
			if (!endptr || endptr == data || *endptr)
				goto error;
			len = (endptr + 1) - (char*)data;
			if (size <= len)
				goto error;
			size -= len;
			data += len;
		}

		for (i = 0; i < 3; i++) {
			if (!ui->mode[i])
				continue;
			if (size < 20)
				goto error;
			hashcpy(ui->sha1[i], data);
			size -= 20;
			data += 20;
		}
	}
	return resolve_undo;

error:
	string_list_clear(resolve_undo, 1);
	error("Index records invalid resolve-undo information");
	return NULL;
}

void resolve_undo_clear_index(struct index_state *istate)
{
	struct string_list *resolve_undo = istate->resolve_undo;
	if (!resolve_undo)
		return;
	string_list_clear(resolve_undo, 1);
	free(resolve_undo);
	istate->resolve_undo = NULL;
	istate->cache_changed = 1;
}
