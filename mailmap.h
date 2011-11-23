#ifndef MAILMAP_H
#define MAILMAP_H

int read_mailmap(struct string_list *map, char **repo_abbrev);
void clear_mailmap(struct string_list *map);

int map_user(struct string_list *mailmap,
	     char *email, int maxlen_email, char *name, int maxlen_name);

#endif
