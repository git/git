#ifndef PATHSPEC_H
#define PATHSPEC_H

extern char *find_pathspecs_matching_against_index(const char **pathspec);
extern void add_pathspec_matches_against_index(const char **pathspec, char *seen, int specs);
extern const char *check_path_for_gitlink(const char *path);
extern void die_if_path_beyond_symlink(const char *path, const char *prefix);

#endif /* PATHSPEC_H */
