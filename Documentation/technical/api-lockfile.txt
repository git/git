lockfile API
============

Talk about <lockfile.c>, things like:

* lockfile lifetime -- atexit(3) looks at them, do not put them on the
  stack;
* hold_lock_file_for_update()
* commit_lock_file()
* rollback_rock_file()

(JC, Dscho, Shawn)
