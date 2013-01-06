#ifndef PATHSPEC_H
#define PATHSPEC_H

extern char *find_pathspecs_matching_against_index(const char **pathspec);
extern void add_pathspec_matches_against_index(const char **pathspec, char *seen, int specs);

#endif /* PATHSPEC_H */
