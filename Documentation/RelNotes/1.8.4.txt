Git v1.8.4 Release Notes
========================

Updates since v1.8.3
--------------------

Foreign interface

 * Remote transport helper has been updated to report errors and
   maintain ref hierarchy used to keep track of its own state better.


UI, Workflows & Features

 * "check-ignore" (new feature since 1.8.2) has been updated to work
   more like "check-attr" over bidi-pipes.

 * We used the approxidate() parser for "--expire=<timestamp>" options
   of various commands, but it is better to treat --expire=all and
   --expire=now a bit more specially than using the current timestamp.
   "git gc" and "git reflog" have been updated with a new parsing
   function for expiry dates.


Performance, Internal Implementation, etc.

 * Object lookup logic, when the object hashtable starts to become
   crowded, has been optimized.

 * When TEST_OUTPUT_DIRECTORY setting is used, it was handled somewhat
   inconsistently between the test framework and t/Makefile, and logic
   to summarize the results looked at a wrong place.

 * Many warnings from sparse source checker in compat/ area has been
   squelched.

 * The code to reading and updating packed-refs file has been updated,
   correcting corner case bugs.


Also contains various documentation updates and code clean-ups.


Fixes since v1.8.3
------------------

Unless otherwise noted, all the fixes since v1.8.3 in the maintenance
track are contained in this release (see release notes to them for
details).

 * When $HOME is misconfigured to point at an unreadable directory, we
   used to complain and die. Loosen the check.
   (merge 4698c8f jn/config-ignore-inaccessible later to maint).

 * "git subtree" (in contrib/) had one codepath with loose error
   checks to lose data at the remote side.
   (merge 3212d56 jk/subtree-do-not-push-if-split-fails later to maint).

 * "git fetch" into a shallow repository from a repository that does
   not know about the shallow boundary commits (e.g. a different fork
   from the repository the current shallow repository was cloned from)
   did not work correctly.
   (merge 71d5f93 mh/fetch-into-shallow later to maint).

 * "git checkout foo" DWIMs the intended "upstream" and turns it into
   "git checkout -t -b foo remotes/origin/foo". This codepath has been
   updated to correctly take existing remote definitions into account.
   (merge 229177a jh/checkout-auto-tracking later to maint).
