#ifndef HOOK_H
#define HOOK_H

/*
 * Returns the path to the hook file, or NULL if the hook is missing
 * or disabled. Note that this points to static storage that will be
 * overwritten by further calls to find_hook and run_hook_*.
 */
const char *find_hook(const char *name);

/*
 * A boolean version of find_hook()
 */
int hook_exists(const char *hookname);

#endif
