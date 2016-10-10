#ifndef TMP_OBJDIR_H
#define TMP_OBJDIR_H

/*
 * This API allows you to create a temporary object directory, advertise it to
 * sub-processes via GIT_OBJECT_DIRECTORY and GIT_ALTERNATE_OBJECT_DIRECTORIES,
 * and then either migrate its object into the main object directory, or remove
 * it. The library handles unexpected signal/exit death by cleaning up the
 * temporary directory.
 *
 * Example:
 *
 *	struct tmp_objdir *t = tmp_objdir_create();
 *	if (!run_command_v_opt_cd_env(cmd, 0, NULL, tmp_objdir_env(t)) &&
 *	    !tmp_objdir_migrate(t))
 *		printf("success!\n");
 *	else
 *		die("failed...tmp_objdir will clean up for us");
 *
 */

struct tmp_objdir;

/*
 * Create a new temporary object directory; returns NULL on failure.
 */
struct tmp_objdir *tmp_objdir_create(void);

/*
 * Return a list of environment strings, suitable for use with
 * child_process.env, that can be passed to child programs to make use of the
 * temporary object directory.
 */
const char **tmp_objdir_env(const struct tmp_objdir *);

/*
 * Finalize a temporary object directory by migrating its objects into the main
 * object database, removing the temporary directory, and freeing any
 * associated resources.
 */
int tmp_objdir_migrate(struct tmp_objdir *);

/*
 * Destroy a temporary object directory, discarding any objects it contains.
 */
int tmp_objdir_destroy(struct tmp_objdir *);

/*
 * Add the temporary object directory as an alternate object store in the
 * current process.
 */
void tmp_objdir_add_as_alternate(const struct tmp_objdir *);

#endif /* TMP_OBJDIR_H */
