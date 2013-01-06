#ifndef PATHSPEC_H
#define PATHSPEC_H

extern char *find_used_pathspec(const char **pathspec);
extern void fill_pathspec_matches(const char **pathspec, char *seen, int specs);

#endif /* PATHSPEC_H */
