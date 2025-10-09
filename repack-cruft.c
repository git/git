#include "git-compat-util.h"
#include "repack.h"
#include "packfile.h"
#include "repository.h"
#include "run-command.h"

static void combine_small_cruft_packs(FILE *in, off_t combine_cruft_below_size,
				      struct existing_packs *existing)
{
	struct packed_git *p;
	struct strbuf buf = STRBUF_INIT;
	size_t i;

	repo_for_each_pack(existing->repo, p) {
		if (!(p->is_cruft && p->pack_local))
			continue;

		strbuf_reset(&buf);
		strbuf_addstr(&buf, pack_basename(p));
		strbuf_strip_suffix(&buf, ".pack");

		if (!string_list_has_string(&existing->cruft_packs, buf.buf))
			continue;

		if (p->pack_size < combine_cruft_below_size) {
			fprintf(in, "-%s\n", pack_basename(p));
		} else {
			existing_packs_retain_cruft(existing, p);
			fprintf(in, "%s\n", pack_basename(p));
		}
	}

	for (i = 0; i < existing->non_kept_packs.nr; i++)
		fprintf(in, "-%s.pack\n",
			existing->non_kept_packs.items[i].string);

	strbuf_release(&buf);
}

int write_cruft_pack(const struct write_pack_opts *opts,
		     const char *cruft_expiration,
		     unsigned long combine_cruft_below_size,
		     struct string_list *names,
		     struct existing_packs *existing)
{
	struct child_process cmd = CHILD_PROCESS_INIT;
	struct string_list_item *item;
	FILE *in;
	int ret;
	const char *pack_prefix = write_pack_opts_pack_prefix(opts);

	prepare_pack_objects(&cmd, opts->po_args, opts->destination);

	strvec_push(&cmd.args, "--cruft");
	if (cruft_expiration)
		strvec_pushf(&cmd.args, "--cruft-expiration=%s",
			     cruft_expiration);

	strvec_push(&cmd.args, "--non-empty");

	cmd.in = -1;

	ret = start_command(&cmd);
	if (ret)
		return ret;

	/*
	 * names has a confusing double use: it both provides the list
	 * of just-written new packs, and accepts the name of the cruft
	 * pack we are writing.
	 *
	 * By the time it is read here, it contains only the pack(s)
	 * that were just written, which is exactly the set of packs we
	 * want to consider kept.
	 *
	 * If `--expire-to` is given, the double-use served by `names`
	 * ensures that the pack written to `--expire-to` excludes any
	 * objects contained in the cruft pack.
	 */
	in = xfdopen(cmd.in, "w");
	for_each_string_list_item(item, names)
		fprintf(in, "%s-%s.pack\n", pack_prefix, item->string);
	if (combine_cruft_below_size && !cruft_expiration) {
		combine_small_cruft_packs(in, combine_cruft_below_size,
					  existing);
	} else {
		for_each_string_list_item(item, &existing->non_kept_packs)
			fprintf(in, "-%s.pack\n", item->string);
		for_each_string_list_item(item, &existing->cruft_packs)
			fprintf(in, "-%s.pack\n", item->string);
	}
	for_each_string_list_item(item, &existing->kept_packs)
		fprintf(in, "%s.pack\n", item->string);
	fclose(in);

	return finish_pack_objects_cmd(existing->repo->hash_algo, opts, &cmd,
				       names);
}
