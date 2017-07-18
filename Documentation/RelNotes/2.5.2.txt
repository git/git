Git v2.5.2 Release Notes
========================

Fixes since v2.5.1
------------------

 * "git init empty && git -C empty log" said "bad default revision 'HEAD'",
   which was found to be a bit confusing to new users.

 * The "interpret-trailers" helper mistook a multi-paragraph title of
   a commit log message with a colon in it as the end of the trailer
   block.

 * When re-priming the cache-tree opportunistically while committing
   the in-core index as-is, we mistakenly invalidated the in-core
   index too aggressively, causing the experimental split-index code
   to unnecessarily rewrite the on-disk index file(s).

 * "git archive" did not use zip64 extension when creating an archive
   with more than 64k entries, which nobody should need, right ;-)?

 * The code in "multiple-worktree" support that attempted to recover
   from an inconsistent state updated an incorrect file.

 * "git rev-list" does not take "--notes" option, but did not complain
   when one is given.

 * Because the configuration system does not allow "alias.0foo" and
   "pager.0foo" as the configuration key, the user cannot use '0foo'
   as a custom command name anyway, but "git 0foo" tried to look these
   keys up and emitted useless warnings before saying '0foo is not a
   git command'.  These warning messages have been squelched.

 * We recently rewrote one of the build scripts in Perl, which made it
   necessary to have Perl to build Git.  Reduced Perl dependency by
   rewriting it again using sed.

 * t1509 test that requires a dedicated VM environment had some
   bitrot, which has been corrected.

 * strbuf_read() used to have one extra iteration (and an unnecessary
   strbuf_grow() of 8kB), which was eliminated.

 * The codepath to produce error messages had a hard-coded limit to
   the size of the message, primarily to avoid memory allocation while
   calling die().

 * When trying to see that an object does not exist, a state errno
   leaked from our "first try to open a packfile with O_NOATIME and
   then if it fails retry without it" logic on a system that refuses
   O_NOATIME.  This confused us and caused us to die, saying that the
   packfile is unreadable, when we should have just reported that the
   object does not exist in that packfile to the caller.

 * An off-by-one error made "git remote" to mishandle a remote with a
   single letter nickname.

 * A handful of codepaths that used to use fixed-sized arrays to hold
   pathnames have been corrected to use strbuf and other mechanisms to
   allow longer pathnames without fearing overflows.

Also contains typofixes, documentation updates and trivial code
clean-ups.
