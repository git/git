#include "git-compat-util.h"
#include "dir.h"
#include "midx.h"
#include "odb.h"
#include "packfile.h"
#include "path.h"
#include "repack.h"
#include "repository.h"
#include "run-command.h"
#include "tempfile.h"

void prepare_pack_objects(struct child_process *cmd,
			  const struct pack_objects_args *args,
			  const char *out)
{
	strvec_push(&cmd->args, "pack-objects");
	if (args->window)
		strvec_pushf(&cmd->args, "--window=%s", args->window);
	if (args->window_memory)
		strvec_pushf(&cmd->args, "--window-memory=%s", args->window_memory);
	if (args->depth)
		strvec_pushf(&cmd->args, "--depth=%s", args->depth);
	if (args->threads)
		strvec_pushf(&cmd->args, "--threads=%s", args->threads);
	if (args->max_pack_size)
		strvec_pushf(&cmd->args, "--max-pack-size=%lu", args->max_pack_size);
	if (args->no_reuse_delta)
		strvec_pushf(&cmd->args, "--no-reuse-delta");
	if (args->no_reuse_object)
		strvec_pushf(&cmd->args, "--no-reuse-object");
	if (args->name_hash_version)
		strvec_pushf(&cmd->args, "--name-hash-version=%d", args->name_hash_version);
	if (args->path_walk)
		strvec_pushf(&cmd->args, "--path-walk");
	if (args->local)
		strvec_push(&cmd->args,  "--local");
	if (args->quiet)
		strvec_push(&cmd->args,  "--quiet");
	if (args->delta_base_offset)
		strvec_push(&cmd->args,  "--delta-base-offset");
	if (!args->pack_kept_objects)
		strvec_push(&cmd->args,  "--honor-pack-keep");
	strvec_push(&cmd->args, out);
	cmd->git_cmd = 1;
	cmd->out = -1;
}

void pack_objects_args_release(struct pack_objects_args *args)
{
	free(args->window);
	free(args->window_memory);
	free(args->depth);
	free(args->threads);
	list_objects_filter_release(&args->filter_options);
}

void repack_remove_redundant_pack(struct repository *repo, const char *dir_name,
				  const char *base_name)
{
	struct strbuf buf = STRBUF_INIT;
	struct odb_source *source = repo->objects->sources;
	struct multi_pack_index *m = get_multi_pack_index(source);
	strbuf_addf(&buf, "%s.pack", base_name);
	if (m && source->local && midx_contains_pack(m, buf.buf))
		clear_midx_file(repo);
	strbuf_insertf(&buf, 0, "%s/", dir_name);
	unlink_pack_path(buf.buf, 1);
	strbuf_release(&buf);
}

const char *write_pack_opts_pack_prefix(const struct write_pack_opts *opts)
{
	const char *pack_prefix;
	if (!skip_prefix(opts->packtmp, opts->packdir, &pack_prefix))
		die(_("pack prefix %s does not begin with objdir %s"),
		    opts->packtmp, opts->packdir);
	if (*pack_prefix == '/')
		pack_prefix++;
	return pack_prefix;
}

bool write_pack_opts_is_local(const struct write_pack_opts *opts)
{
	return starts_with(opts->destination, opts->packdir);
}

int finish_pack_objects_cmd(const struct git_hash_algo *algop,
			    const struct write_pack_opts *opts,
			    struct child_process *cmd,
			    struct string_list *names)
{
	FILE *out;
	bool local = write_pack_opts_is_local(opts);
	struct strbuf line = STRBUF_INIT;

	out = xfdopen(cmd->out, "r");
	while (strbuf_getline_lf(&line, out) != EOF) {
		struct string_list_item *item;

		if (line.len != algop->hexsz)
			die(_("repack: Expecting full hex object ID lines only "
			      "from pack-objects."));
		/*
		 * Avoid putting packs written outside of the repository in the
		 * list of names.
		 */
		if (local) {
			item = string_list_append(names, line.buf);
			item->util = generated_pack_populate(line.buf,
							     opts->packtmp);
		}
	}
	fclose(out);

	strbuf_release(&line);

	return finish_command(cmd);
}

#define DELETE_PACK 1
#define RETAIN_PACK 2

void existing_packs_collect(struct existing_packs *existing,
			    const struct string_list *extra_keep)
{
	struct packed_git *p;
	struct strbuf buf = STRBUF_INIT;

	repo_for_each_pack(existing->repo, p) {
		size_t i;
		const char *base;

		if (p->multi_pack_index)
			string_list_append(&existing->midx_packs,
					    pack_basename(p));
		if (!p->pack_local)
			continue;

		base = pack_basename(p);

		for (i = 0; i < extra_keep->nr; i++)
			if (!fspathcmp(base, extra_keep->items[i].string))
				break;

		strbuf_reset(&buf);
		strbuf_addstr(&buf, base);
		strbuf_strip_suffix(&buf, ".pack");

		if ((extra_keep->nr > 0 && i < extra_keep->nr) || p->pack_keep)
			string_list_append(&existing->kept_packs, buf.buf);
		else if (p->is_cruft)
			string_list_append(&existing->cruft_packs, buf.buf);
		else
			string_list_append(&existing->non_kept_packs, buf.buf);
	}

	string_list_sort(&existing->kept_packs);
	string_list_sort(&existing->non_kept_packs);
	string_list_sort(&existing->cruft_packs);
	string_list_sort(&existing->midx_packs);
	strbuf_release(&buf);
}

int existing_packs_has_non_kept(const struct existing_packs *existing)
{
	return existing->non_kept_packs.nr || existing->cruft_packs.nr;
}

static void existing_pack_mark_for_deletion(struct string_list_item *item)
{
	item->util = (void*)((uintptr_t)item->util | DELETE_PACK);
}

static void existing_pack_unmark_for_deletion(struct string_list_item *item)
{
	item->util = (void*)((uintptr_t)item->util & ~DELETE_PACK);
}

int existing_pack_is_marked_for_deletion(struct string_list_item *item)
{
	return (uintptr_t)item->util & DELETE_PACK;
}

static void existing_packs_mark_retained(struct string_list_item *item)
{
	item->util = (void*)((uintptr_t)item->util | RETAIN_PACK);
}

static int existing_pack_is_retained(struct string_list_item *item)
{
	return (uintptr_t)item->util & RETAIN_PACK;
}

static void existing_packs_mark_for_deletion_1(const struct git_hash_algo *algop,
					       struct string_list *names,
					       struct string_list *list)
{
	struct string_list_item *item;
	const size_t hexsz = algop->hexsz;

	for_each_string_list_item(item, list) {
		char *sha1;
		size_t len = strlen(item->string);
		if (len < hexsz)
			continue;
		sha1 = item->string + len - hexsz;

		if (existing_pack_is_retained(item)) {
			existing_pack_unmark_for_deletion(item);
		} else if (!string_list_has_string(names, sha1)) {
			/*
			 * Mark this pack for deletion, which ensures
			 * that this pack won't be included in a MIDX
			 * (if `--write-midx` was given) and that we
			 * will actually delete this pack (if `-d` was
			 * given).
			 */
			existing_pack_mark_for_deletion(item);
		}
	}
}

void existing_packs_retain_cruft(struct existing_packs *existing,
				 struct packed_git *cruft)
{
	struct strbuf buf = STRBUF_INIT;
	struct string_list_item *item;

	strbuf_addstr(&buf, pack_basename(cruft));
	strbuf_strip_suffix(&buf, ".pack");

	item = string_list_lookup(&existing->cruft_packs, buf.buf);
	if (!item)
		BUG("could not find cruft pack '%s'", pack_basename(cruft));

	existing_packs_mark_retained(item);
	strbuf_release(&buf);
}

void existing_packs_mark_for_deletion(struct existing_packs *existing,
				      struct string_list *names)

{
	const struct git_hash_algo *algop = existing->repo->hash_algo;
	existing_packs_mark_for_deletion_1(algop, names,
					   &existing->non_kept_packs);
	existing_packs_mark_for_deletion_1(algop, names,
					   &existing->cruft_packs);
}

static void remove_redundant_packs_1(struct repository *repo,
				     struct string_list *packs,
				     const char *packdir)
{
	struct string_list_item *item;
	for_each_string_list_item(item, packs) {
		if (!existing_pack_is_marked_for_deletion(item))
			continue;
		repack_remove_redundant_pack(repo, packdir, item->string);
	}
}

void existing_packs_remove_redundant(struct existing_packs *existing,
				     const char *packdir)
{
	remove_redundant_packs_1(existing->repo, &existing->non_kept_packs,
				 packdir);
	remove_redundant_packs_1(existing->repo, &existing->cruft_packs,
				 packdir);
}

void existing_packs_release(struct existing_packs *existing)
{
	string_list_clear(&existing->kept_packs, 0);
	string_list_clear(&existing->non_kept_packs, 0);
	string_list_clear(&existing->cruft_packs, 0);
	string_list_clear(&existing->midx_packs, 0);
}

static struct {
	const char *name;
	unsigned optional:1;
} exts[] = {
	{".pack"},
	{".rev", 1},
	{".mtimes", 1},
	{".bitmap", 1},
	{".promisor", 1},
	{".idx"},
};

struct generated_pack {
	struct tempfile *tempfiles[ARRAY_SIZE(exts)];
};

struct generated_pack *generated_pack_populate(const char *name,
					       const char *packtmp)
{
	struct stat statbuf;
	struct strbuf path = STRBUF_INIT;
	struct generated_pack *pack = xcalloc(1, sizeof(*pack));
	size_t i;

	for (i = 0; i < ARRAY_SIZE(exts); i++) {
		strbuf_reset(&path);
		strbuf_addf(&path, "%s-%s%s", packtmp, name, exts[i].name);

		if (stat(path.buf, &statbuf))
			continue;

		pack->tempfiles[i] = register_tempfile(path.buf);
	}

	strbuf_release(&path);
	return pack;
}

int generated_pack_has_ext(const struct generated_pack *pack, const char *ext)
{
	size_t i;
	for (i = 0; i < ARRAY_SIZE(exts); i++) {
		if (strcmp(exts[i].name, ext))
			continue;
		return !!pack->tempfiles[i];
	}
	BUG("unknown pack extension: '%s'", ext);
}

void generated_pack_install(struct generated_pack *pack, const char *name,
			    const char *packdir, const char *packtmp)
{
	size_t ext;
	for (ext = 0; ext < ARRAY_SIZE(exts); ext++) {
		char *fname;

		fname = mkpathdup("%s/pack-%s%s", packdir, name,
				  exts[ext].name);

		if (pack->tempfiles[ext]) {
			const char *fname_old = get_tempfile_path(pack->tempfiles[ext]);
			struct stat statbuffer;

			if (!stat(fname_old, &statbuffer)) {
				statbuffer.st_mode &= ~(S_IWUSR | S_IWGRP | S_IWOTH);
				chmod(fname_old, statbuffer.st_mode);
			}

			if (rename_tempfile(&pack->tempfiles[ext], fname))
				die_errno(_("renaming pack to '%s' failed"),
					  fname);
		} else if (!exts[ext].optional)
			die(_("pack-objects did not write a '%s' file for pack %s-%s"),
			    exts[ext].name, packtmp, name);
		else if (unlink(fname) < 0 && errno != ENOENT)
			die_errno(_("could not unlink: %s"), fname);

		free(fname);
	}
}
