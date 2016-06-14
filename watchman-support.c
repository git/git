#include "cache.h"
#include "watchman-support.h"
#include "strbuf.h"
#include "dir.h"
#include <watchman.h>

static struct watchman_query *make_query(const char *last_update)
{
	struct watchman_query *query = watchman_query();
	watchman_query_set_fields(query, WATCHMAN_FIELD_NAME |
					 WATCHMAN_FIELD_EXISTS |
					 WATCHMAN_FIELD_NEWER);
	watchman_query_set_empty_on_fresh(query, 1);
	query->sync_timeout = core_watchman_sync_timeout;
	if (*last_update)
		watchman_query_set_since_oclock(query, last_update);
	return query;
}

static struct watchman_query_result *query_watchman(
	struct index_state *istate, struct watchman_connection *connection,
	const char *fs_path, const char *last_update)
{
	struct watchman_error wm_error;
	struct watchman_query *query;
	struct watchman_expression *expr;
	struct watchman_query_result *result;

	query = make_query(last_update);
	expr = watchman_true_expression();
	result = watchman_do_query(connection, fs_path, query, expr, &wm_error);
	watchman_free_query(query);
	watchman_free_expression(expr);

	if (!result)
		warning("Watchman query error: %s (at %s)",
			wm_error.message,
			*last_update ? last_update : "the beginning");

	return result;
}

static void update_index(struct index_state *istate,
			 struct watchman_query_result *result)
{
	int i;

	if (result->is_fresh_instance) {
		/* let refresh clear them later */
		for (i = 0; i < istate->cache_nr; i++)
			istate->cache[i]->ce_flags |= CE_WATCHMAN_DIRTY;
		goto done;
	}

	for (i = 0; i < result->nr; i++) {
		struct watchman_stat *wm = result->stats + i;
		int pos;

		if (S_ISDIR(wm->mode) ||
		    !strncmp(wm->name, ".git/", 5) ||
		    strstr(wm->name, "/.git/"))
			continue;

		pos = index_name_pos(istate, wm->name, strlen(wm->name));
		if (pos < 0) {
			if (istate->untracked) {
				char *name = xstrdup(wm->name);
				char *dname = dirname(name);

				/*
				 * dirname() returns '.' for the root,
				 * but we call it ''.
				 */
				if (dname[0] == '.' && dname[1] == 0)
					string_list_append(&istate->untracked->invalid_untracked, "");
				else
					string_list_append(&istate->untracked->invalid_untracked,
							   dname);
				free(name);
			}
			continue;
		}
		/* FIXME: ignore staged entries and gitlinks too? */

		istate->cache[pos]->ce_flags |= CE_WATCHMAN_DIRTY;
	}

done:
	free(istate->last_update);
	istate->last_update    = xstrdup(result->clock);
	istate->cache_changed |= WATCHMAN_CHANGED;
	if (istate->untracked)
		string_list_remove_duplicates(&istate->untracked->invalid_untracked, 0);
}

int check_watchman(struct index_state *istate)
{
	struct watchman_error wm_error;
	struct watchman_connection *connection;
	struct watchman_query_result *result;
	const char *fs_path;
	struct timeval timeout;
	/*
	 * Convert core_watchman_sync_timeout, in milliseconds, to
	 * struct timeval, in seconds and microseconds.
	 */

	fs_path = get_git_work_tree();
	if (!fs_path)
		return -1;

	timeout.tv_sec = core_watchman_sync_timeout / 1000;
	timeout.tv_usec = (core_watchman_sync_timeout % 1000) * 1000;
	connection = watchman_connect(timeout, &wm_error);

	if (!connection) {
		warning("Watchman watch error: %s", wm_error.message);
		return -1;
	}

	if (watchman_watch(connection, fs_path, &wm_error)) {
		warning("Watchman watch error: %s", wm_error.message);
		watchman_connection_close(connection);
		return -1;
	}


	result = query_watchman(istate, connection, fs_path, istate->last_update);
	watchman_connection_close(connection);
	if (!result)
		return -1;
	update_index(istate, result);
	watchman_free_query_result(result);
	return 0;
}
