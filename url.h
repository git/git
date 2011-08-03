#ifndef URL_H
#define URL_H

extern int is_url(const char *url);
extern int is_urlschemechar(int first_flag, int ch);
extern char *url_decode(const char *url);
extern char *url_decode_mem(const char *url, int len);
extern char *url_decode_parameter_name(const char **query);
extern char *url_decode_parameter_value(const char **query);

extern void end_url_with_slash(struct strbuf *buf, const char *url);
extern void str_end_url_with_slash(const char *url, char **dest);

#endif /* URL_H */
