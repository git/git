#ifndef URL_H
#define URL_H

extern int is_url(const char *url);
extern int is_urlschemechar(int first_flag, int ch);
extern char *url_decode(const char *url);
extern char *url_decode_parameter_name(const char **query);
extern char *url_decode_parameter_value(const char **query);

#endif /* URL_H */
