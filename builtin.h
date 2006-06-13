#ifndef BUILTIN_H
#define BUILTIN_H

#include <stdio.h>

#ifndef PATH_MAX
# define PATH_MAX 4096
#endif

extern const char git_version_string[];

void cmd_usage(int show_all, const char *exec_path, const char *fmt, ...)
#ifdef __GNUC__
	__attribute__((__format__(__printf__, 3, 4), __noreturn__))
#endif
	;

extern int cmd_help(int argc, const char **argv, char **envp);
extern int cmd_version(int argc, const char **argv, char **envp);

extern int cmd_whatchanged(int argc, const char **argv, char **envp);
extern int cmd_show(int argc, const char **argv, char **envp);
extern int cmd_log(int argc, const char **argv, char **envp);
extern int cmd_diff(int argc, const char **argv, char **envp);
extern int cmd_format_patch(int argc, const char **argv, char **envp);
extern int cmd_count_objects(int argc, const char **argv, char **envp);

extern int cmd_push(int argc, const char **argv, char **envp);
extern int cmd_grep(int argc, const char **argv, char **envp);
extern int cmd_rm(int argc, const char **argv, char **envp);
extern int cmd_add(int argc, const char **argv, char **envp);
extern int cmd_rev_list(int argc, const char **argv, char **envp);
extern int cmd_check_ref_format(int argc, const char **argv, char **envp);
extern int cmd_init_db(int argc, const char **argv, char **envp);
extern int cmd_tar_tree(int argc, const char **argv, char **envp);
extern int cmd_upload_tar(int argc, const char **argv, char **envp);
extern int cmd_get_tar_commit_id(int argc, const char **argv, char **envp);
extern int cmd_ls_files(int argc, const char **argv, char **envp);
extern int cmd_ls_tree(int argc, const char **argv, char **envp);
extern int cmd_read_tree(int argc, const char **argv, char **envp);
extern int cmd_commit_tree(int argc, const char **argv, char **envp);
extern int cmd_apply(int argc, const char **argv, char **envp);
extern int cmd_show_branch(int argc, const char **argv, char **envp);
extern int cmd_diff_files(int argc, const char **argv, char **envp);
extern int cmd_diff_index(int argc, const char **argv, char **envp);
extern int cmd_diff_stages(int argc, const char **argv, char **envp);
extern int cmd_diff_tree(int argc, const char **argv, char **envp);
extern int cmd_cat_file(int argc, const char **argv, char **envp);
extern int cmd_rev_parse(int argc, const char **argv, char **envp);
extern int cmd_update_index(int argc, const char **argv, char **envp);
extern int cmd_update_ref(int argc, const char **argv, char **envp);

extern int cmd_write_tree(int argc, const char **argv, char **envp);
extern int write_tree(unsigned char *sha1, int missing_ok, const char *prefix);

extern int cmd_mailsplit(int argc, const char **argv, char **envp);
extern int split_mbox(const char **mbox, const char *dir, int allow_bare, int nr_prec, int skip);

extern int cmd_mailinfo(int argc, const char **argv, char **envp);
extern int mailinfo(FILE *in, FILE *out, int ks, const char *encoding, const char *msg, const char *patch);

extern int cmd_stripspace(int argc, const char **argv, char **envp);
extern void stripspace(FILE *in, FILE *out);
#endif
