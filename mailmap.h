#ifndef MAILMAP_H
#define MAILMAP_H

struct string_list;

int read_mailmap(struct string_list *map);
void clear_mailmap(struct string_list *map);

int map_user(struct string_list *map,
			 const char **email, size_t *emaillen, const char **name, size_t *namelen);

#endif
