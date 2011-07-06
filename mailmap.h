#ifndef MAILMAP_H
#define MAILMAP_H

int read_mailmap(struct string_list *map, const char *filename, char **repo_abbrev);
int map_email(struct string_list *mailmap, const char *email, char *name, int maxlen);

#endif
