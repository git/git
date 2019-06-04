#ifndef CHDIR_NOTIFY_H
#define CHDIR_NOTIFY_H

/*
 * An API to let code "subscribe" to changes to the current working directory.
 * The general idea is that some code asks to be notified when the working
 * directory changes, and other code that calls chdir uses a special wrapper
 * that notifies everyone.
 */

/*
 * Callers who need to know about changes can do:
 *
 *   void foo(const char *old_path, const char *new_path, void *data)
 *   {
 *	warning("switched from %s to %s!", old_path, new_path);
 *   }
 *   ...
 *   chdir_notify_register("description", foo, data);
 *
 * In practice most callers will want to move a relative path to the new root;
 * they can use the reparent_relative_path() helper for that. If that's all
 * you're doing, you can also use the convenience function:
 *
 *   chdir_notify_reparent("description", &my_path);
 *
 * Whenever a chdir event occurs, that will update my_path (if it's relative)
 * to adjust for the new cwd by freeing any existing string and allocating a
 * new one.
 *
 * Registered functions are called in the order in which they were added. Note
 * that there's currently no way to remove a function, so make sure that the
 * data parameter remains valid for the rest of the program.
 *
 * The "name" argument is used only for printing trace output from
 * $GIT_TRACE_SETUP. It may be NULL, but if non-NULL should point to
 * storage which lasts as long as the registration is active.
 */
typedef void (*chdir_notify_callback)(const char *name,
				      const char *old_cwd,
				      const char *new_cwd,
				      void *data);
void chdir_notify_register(const char *name, chdir_notify_callback cb, void *data);
void chdir_notify_reparent(const char *name, char **path);

/*
 *
 * Callers that want to chdir:
 *
 *   chdir_notify(new_path);
 *
 * to switch to the new path and notify any callbacks.
 *
 * Note that you don't need to chdir_notify() if you're just temporarily moving
 * to a directory and back, as long as you don't call any subscribed code in
 * between (but it should be safe to do so if you're unsure).
 */
int chdir_notify(const char *new_cwd);

/*
 * Reparent a relative path from old_root to new_root. For example:
 *
 *   reparent_relative_path("/a", "/a/b", "b/rel");
 *
 * would return the (newly allocated) string "rel". Note that we may return an
 * absolute path in some cases (e.g., if the resulting path is not inside
 * new_cwd).
 */
char *reparent_relative_path(const char *old_cwd,
			     const char *new_cwd,
			     const char *path);

#endif /* CHDIR_NOTIFY_H */
