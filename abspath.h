/* This file was automatically generated.  Do not edit! */
#undef INTERFACE
char *prefix_filename(const char *pfx,const char *arg);
char *absolute_pathdup(const char *path);
const char *absolute_path(const char *path);
char *real_pathdup(const char *path,int die_on_error);
char *strbuf_realpath_forgiving(struct strbuf *resolved,const char *path,int die_on_error);
char *strbuf_realpath(struct strbuf *resolved,const char *path,int die_on_error);
int is_directory(const char *path);
