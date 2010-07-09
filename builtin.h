#ifndef BUILTIN_H
#define BUILTIN_H

#include "git-compat-util.h"
#include "strbuf.h"
#include "cache.h"
#include "commit.h"
#include "notes.h"

extern const char git_version_string[];
extern const char git_usage_string[];
extern const char git_more_info_string[];

extern void list_common_cmds_help(void);
extern const char *help_unknown_cmd(const char *cmd);
extern void prune_packed_objects(int);
extern int fmt_merge_msg(int merge_summary, struct strbuf *in,
	struct strbuf *out);
extern int fmt_merge_msg_shortlog(struct strbuf *in, struct strbuf *out);
extern int commit_notes(struct notes_tree *t, const char *msg);

struct notes_rewrite_cfg {
	struct notes_tree **trees;
	const char *cmd;
	int enabled;
	combine_notes_fn combine;
	struct string_list *refs;
	int refs_from_env;
	int mode_from_env;
};

combine_notes_fn parse_combine_notes_fn(const char *v);
struct notes_rewrite_cfg *init_copy_notes_for_rewrite(const char *cmd);
int copy_note_for_rewrite(struct notes_rewrite_cfg *c,
			  const unsigned char *from_obj, const unsigned char *to_obj);
void finish_copy_notes_for_rewrite(struct notes_rewrite_cfg *c);

extern int check_pager_config(const char *cmd);

extern int textconv_object(const char *path, const unsigned char *sha1, char **buf, unsigned long *buf_size);

extern int cmd_add(int argc, const char **argv, const char *prefix);
extern int cmd_annotate(int argc, const char **argv, const char *prefix);
extern int cmd_apply(int argc, const char **argv, const char *prefix);
extern int cmd_archive(int argc, const char **argv, const char *prefix);
extern int cmd_bisect__helper(int argc, const char **argv, const char *prefix);
extern int cmd_blame(int argc, const char **argv, const char *prefix);
extern int cmd_branch(int argc, const char **argv, const char *prefix);
extern int cmd_bundle(int argc, const char **argv, const char *prefix);
extern int cmd_cat_file(int argc, const char **argv, const char *prefix);
extern int cmd_checkout(int argc, const char **argv, const char *prefix);
extern int cmd_checkout_index(int argc, const char **argv, const char *prefix);
extern int cmd_check_attr(int argc, const char **argv, const char *prefix);
extern int cmd_check_ref_format(int argc, const char **argv, const char *prefix);
extern int cmd_cherry(int argc, const char **argv, const char *prefix);
extern int cmd_cherry_pick(int argc, const char **argv, const char *prefix);
extern int cmd_clone(int argc, const char **argv, const char *prefix);
extern int cmd_clean(int argc, const char **argv, const char *prefix);
extern int cmd_commit(int argc, const char **argv, const char *prefix);
extern int cmd_commit_tree(int argc, const char **argv, const char *prefix);
extern int cmd_count_objects(int argc, const char **argv, const char *prefix);
extern int cmd_describe(int argc, const char **argv, const char *prefix);
extern int cmd_diff_files(int argc, const char **argv, const char *prefix);
extern int cmd_diff_index(int argc, const char **argv, const char *prefix);
extern int cmd_diff(int argc, const char **argv, const char *prefix);
extern int cmd_diff_tree(int argc, const char **argv, const char *prefix);
extern int cmd_fast_export(int argc, const char **argv, const char *prefix);
extern int cmd_fetch(int argc, const char **argv, const char *prefix);
extern int cmd_fetch_pack(int argc, const char **argv, const char *prefix);
extern int cmd_fmt_merge_msg(int argc, const char **argv, const char *prefix);
extern int cmd_for_each_ref(int argc, const char **argv, const char *prefix);
extern int cmd_format_patch(int argc, const char **argv, const char *prefix);
extern int cmd_fsck(int argc, const char **argv, const char *prefix);
extern int cmd_gc(int argc, const char **argv, const char *prefix);
extern int cmd_get_tar_commit_id(int argc, const char **argv, const char *prefix);
extern int cmd_grep(int argc, const char **argv, const char *prefix);
extern int cmd_hash_object(int argc, const char **argv, const char *prefix);
extern int cmd_help(int argc, const char **argv, const char *prefix);
extern int cmd_http_fetch(int argc, const char **argv, const char *prefix);
extern int cmd_index_pack(int argc, const char **argv, const char *prefix);
extern int cmd_init_db(int argc, const char **argv, const char *prefix);
extern int cmd_log(int argc, const char **argv, const char *prefix);
extern int cmd_log_reflog(int argc, const char **argv, const char *prefix);
extern int cmd_ls_files(int argc, const char **argv, const char *prefix);
extern int cmd_ls_tree(int argc, const char **argv, const char *prefix);
extern int cmd_ls_remote(int argc, const char **argv, const char *prefix);
extern int cmd_mailinfo(int argc, const char **argv, const char *prefix);
extern int cmd_mailsplit(int argc, const char **argv, const char *prefix);
extern int cmd_merge(int argc, const char **argv, const char *prefix);
extern int cmd_merge_base(int argc, const char **argv, const char *prefix);
extern int cmd_merge_index(int argc, const char **argv, const char *prefix);
extern int cmd_merge_ours(int argc, const char **argv, const char *prefix);
extern int cmd_merge_file(int argc, const char **argv, const char *prefix);
extern int cmd_merge_recursive(int argc, const char **argv, const char *prefix);
extern int cmd_merge_tree(int argc, const char **argv, const char *prefix);
extern int cmd_mktag(int argc, const char **argv, const char *prefix);
extern int cmd_mktree(int argc, const char **argv, const char *prefix);
extern int cmd_mv(int argc, const char **argv, const char *prefix);
extern int cmd_name_rev(int argc, const char **argv, const char *prefix);
extern int cmd_notes(int argc, const char **argv, const char *prefix);
extern int cmd_pack_objects(int argc, const char **argv, const char *prefix);
extern int cmd_pack_redundant(int argc, const char **argv, const char *prefix);
extern int cmd_patch_id(int argc, const char **argv, const char *prefix);
extern int cmd_pickaxe(int argc, const char **argv, const char *prefix);
extern int cmd_prune(int argc, const char **argv, const char *prefix);
extern int cmd_prune_packed(int argc, const char **argv, const char *prefix);
extern int cmd_push(int argc, const char **argv, const char *prefix);
extern int cmd_read_tree(int argc, const char **argv, const char *prefix);
extern int cmd_receive_pack(int argc, const char **argv, const char *prefix);
extern int cmd_reflog(int argc, const char **argv, const char *prefix);
extern int cmd_remote(int argc, const char **argv, const char *prefix);
extern int cmd_config(int argc, const char **argv, const char *prefix);
extern int cmd_rerere(int argc, const char **argv, const char *prefix);
extern int cmd_reset(int argc, const char **argv, const char *prefix);
extern int cmd_rev_list(int argc, const char **argv, const char *prefix);
extern int cmd_rev_parse(int argc, const char **argv, const char *prefix);
extern int cmd_revert(int argc, const char **argv, const char *prefix);
extern int cmd_rm(int argc, const char **argv, const char *prefix);
extern int cmd_send_pack(int argc, const char **argv, const char *prefix);
extern int cmd_shortlog(int argc, const char **argv, const char *prefix);
extern int cmd_show(int argc, const char **argv, const char *prefix);
extern int cmd_show_branch(int argc, const char **argv, const char *prefix);
extern int cmd_status(int argc, const char **argv, const char *prefix);
extern int cmd_stripspace(int argc, const char **argv, const char *prefix);
extern int cmd_symbolic_ref(int argc, const char **argv, const char *prefix);
extern int cmd_tag(int argc, const char **argv, const char *prefix);
extern int cmd_tar_tree(int argc, const char **argv, const char *prefix);
extern int cmd_unpack_file(int argc, const char **argv, const char *prefix);
extern int cmd_unpack_objects(int argc, const char **argv, const char *prefix);
extern int cmd_update_index(int argc, const char **argv, const char *prefix);
extern int cmd_update_ref(int argc, const char **argv, const char *prefix);
extern int cmd_update_server_info(int argc, const char **argv, const char *prefix);
extern int cmd_upload_archive(int argc, const char **argv, const char *prefix);
extern int cmd_upload_tar(int argc, const char **argv, const char *prefix);
extern int cmd_var(int argc, const char **argv, const char *prefix);
extern int cmd_verify_tag(int argc, const char **argv, const char *prefix);
extern int cmd_version(int argc, const char **argv, const char *prefix);
extern int cmd_whatchanged(int argc, const char **argv, const char *prefix);
extern int cmd_write_tree(int argc, const char **argv, const char *prefix);
extern int cmd_verify_pack(int argc, const char **argv, const char *prefix);
extern int cmd_show_ref(int argc, const char **argv, const char *prefix);
extern int cmd_pack_refs(int argc, const char **argv, const char *prefix);
extern int cmd_replace(int argc, const char **argv, const char *prefix);

#endif
