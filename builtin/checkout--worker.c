#define USE_THE_REPOSITORY_VARIABLE
#include "builtin.h"
#include "config.h"
#include "entry.h"
#include "gettext.h"
#include "parallel-checkout.h"
#include "parse-options.h"
#include "pkt-line.h"
#include "read-cache-ll.h"

static void packet_to_pc_item(const char *buffer, int len,
			      struct parallel_checkout_item *pc_item)
{
	const struct pc_item_fixed_portion *fixed_portion;
	const char *variant;
	char *encoding;

	if (len < sizeof(struct pc_item_fixed_portion))
		BUG("checkout worker received too short item (got %dB, exp %dB)",
		    len, (int)sizeof(struct pc_item_fixed_portion));

	fixed_portion = (struct pc_item_fixed_portion *)buffer;

	if (len - sizeof(struct pc_item_fixed_portion) !=
		fixed_portion->name_len + fixed_portion->working_tree_encoding_len)
		BUG("checkout worker received corrupted item");

	variant = buffer + sizeof(struct pc_item_fixed_portion);

	/*
	 * Note: the main process uses zero length to communicate that the
	 * encoding is NULL. There is no use case that requires sending an
	 * actual empty string, since convert_attrs() never sets
	 * ca.working_tree_enconding to "".
	 */
	if (fixed_portion->working_tree_encoding_len) {
		encoding = xmemdupz(variant,
				    fixed_portion->working_tree_encoding_len);
		variant += fixed_portion->working_tree_encoding_len;
	} else {
		encoding = NULL;
	}

	memset(pc_item, 0, sizeof(*pc_item));
	pc_item->ce = make_empty_transient_cache_entry(fixed_portion->name_len, NULL);
	pc_item->ce->ce_namelen = fixed_portion->name_len;
	pc_item->ce->ce_mode = fixed_portion->ce_mode;
	memcpy(pc_item->ce->name, variant, pc_item->ce->ce_namelen);
	oidcpy(&pc_item->ce->oid, &fixed_portion->oid);

	pc_item->id = fixed_portion->id;
	pc_item->ca.crlf_action = fixed_portion->crlf_action;
	pc_item->ca.ident = fixed_portion->ident;
	pc_item->ca.working_tree_encoding = encoding;
}

static void report_result(struct parallel_checkout_item *pc_item)
{
	struct pc_item_result res = { 0 };
	size_t size;

	res.id = pc_item->id;
	res.status = pc_item->status;

	if (pc_item->status == PC_ITEM_WRITTEN) {
		res.st = pc_item->st;
		size = sizeof(res);
	} else {
		size = PC_ITEM_RESULT_BASE_SIZE;
	}

	packet_write(1, (const char *)&res, size);
}

/* Free the worker-side malloced data, but not pc_item itself. */
static void release_pc_item_data(struct parallel_checkout_item *pc_item)
{
	free((char *)pc_item->ca.working_tree_encoding);
	discard_cache_entry(pc_item->ce);
}

static void worker_loop(struct checkout *state)
{
	struct parallel_checkout_item *items = NULL;
	size_t i, nr = 0, alloc = 0;

	while (1) {
		int len = packet_read(0, packet_buffer, sizeof(packet_buffer),
				      0);

		if (len < 0)
			BUG("packet_read() returned negative value");
		else if (!len)
			break;

		ALLOC_GROW(items, nr + 1, alloc);
		packet_to_pc_item(packet_buffer, len, &items[nr++]);
	}

	for (i = 0; i < nr; i++) {
		struct parallel_checkout_item *pc_item = &items[i];
		write_pc_item(pc_item, state);
		report_result(pc_item);
		release_pc_item_data(pc_item);
	}

	packet_flush(1);

	free(items);
}

static const char * const checkout_worker_usage[] = {
	N_("git checkout--worker [<options>]"),
	NULL
};

int cmd_checkout__worker(int argc,
			 const char **argv,
			 const char *prefix,
			 struct repository *repo UNUSED)
{
	struct checkout state = CHECKOUT_INIT;
	struct option checkout_worker_options[] = {
		OPT_STRING(0, "prefix", &state.base_dir, N_("string"),
			N_("when creating files, prepend <string>")),
		OPT_END()
	};

	if (argc == 2 && !strcmp(argv[1], "-h"))
		usage_with_options(checkout_worker_usage,
				   checkout_worker_options);

	git_config(git_default_config, NULL);
	argc = parse_options(argc, argv, prefix, checkout_worker_options,
			     checkout_worker_usage, 0);
	if (argc > 0)
		usage_with_options(checkout_worker_usage, checkout_worker_options);

	if (state.base_dir)
		state.base_dir_len = strlen(state.base_dir);

	/*
	 * Setting this on a worker won't actually update the index. We just
	 * need to tell the checkout machinery to lstat() the written entries,
	 * so that we can send this data back to the main process.
	 */
	state.refresh_cache = 1;

	worker_loop(&state);
	return 0;
}
