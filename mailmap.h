#ifndef MAILMAP_H
#define MAILMAP_H

struct string_list;

extern char *git_mailmap_file;
extern char *git_mailmap_blob;

/* Flags for read_mailmap_file() */
#define MAILMAP_NOFOLLOW (1<<0)

int read_mailmap_file(struct string_list *map, const char *filename,
		      unsigned flags);
int read_mailmap_blob(struct string_list *map, const char *name);

int read_mailmap(struct string_list *map);
void clear_mailmap(struct string_list *map);

int map_user(struct string_list *map,
			 const char **email, size_t *emaillen, const char **name, size_t *namelen);

#endif
