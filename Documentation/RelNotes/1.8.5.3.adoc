Git v1.8.5.3 Release Notes
==========================

Fixes since v1.8.5.2
--------------------

 * The "--[no-]informative-errors" options to "git daemon" were parsed
   a bit too loosely, allowing any other string after these option
   names.

 * A "gc" process running as a different user should be able to stop a
   new "gc" process from starting.

 * An earlier "clean-up" introduced an unnecessary memory leak to the
   credential subsystem.

 * "git mv A B/", when B does not exist as a directory, should error
   out, but it didn't.

 * "git rev-parse <revs> -- <paths>" did not implement the usual
   disambiguation rules the commands in the "git log" family used in
   the same way.

 * "git cat-file --batch=", an admittedly useless command, did not
   behave very well.

Also contains typofixes, documentation updates and trivial code clean-ups.
