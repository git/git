#include "cache.h"
#include "remote.h"
#include "refspec.h"
#include "checkout.h"
#include "config.h"

struct tracking_name_data {
	/* const */ char *src_ref;
	char *dst_ref;
	struct object_id *dst_oid;
	int num_matches;
	const char *default_remote;
	char *default_dst_ref;
	struct object_id *default_dst_oid;
};

#define TRACKING_NAME_DATA_INIT { NULL, NULL, NULL, 0, NULL, NULL, NULL }

static int check_tracking_name(struct remote *remote, void *cb_data)
{
	struct tracking_name_data *cb = cb_data;
	struct refspec_item query;
	memset(&query, 0, sizeof(struct refspec_item));
	query.src = cb->src_ref;
	if (remote_find_tracking(remote, &query) ||
	    get_oid(query.dst, cb->dst_oid)) {
		free(query.dst);
		return 0;
	}
	cb->num_matches++;
	if (cb->default_remote && !strcmp(remote->name, cb->default_remote)) {
		struct object_id *dst = xmalloc(sizeof(*cb->default_dst_oid));
		cb->default_dst_ref = xstrdup(query.dst);
		oidcpy(dst, cb->dst_oid);
		cb->default_dst_oid = dst;
	}
	if (cb->dst_ref) {
		free(query.dst);
		return 0;
	}
	cb->dst_ref = query.dst;
	return 0;
}

const char *unique_tracking_name(const char *name, struct object_id *oid,
				 int *dwim_remotes_matched)
{
	struct tracking_name_data cb_data = TRACKING_NAME_DATA_INIT;
	const char *default_remote = NULL;
	if (!git_config_get_string_const("checkout.defaultremote", &default_remote))
		cb_data.default_remote = default_remote;
	cb_data.src_ref = xstrfmt("refs/heads/%s", name);
	cb_data.dst_oid = oid;
	for_each_remote(check_tracking_name, &cb_data);
	if (dwim_remotes_matched)
		*dwim_remotes_matched = cb_data.num_matches;
	free(cb_data.src_ref);
	free((char *)default_remote);
	if (cb_data.num_matches == 1) {
		free(cb_data.default_dst_ref);
		free(cb_data.default_dst_oid);
		return cb_data.dst_ref;
	}
	free(cb_data.dst_ref);
	if (cb_data.default_dst_ref) {
		oidcpy(oid, cb_data.default_dst_oid);
		free(cb_data.default_dst_oid);
		return cb_data.default_dst_ref;
	}
	return NULL;
}
