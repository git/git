Git v2.7.3 Release Notes
========================

Fixes since v2.7.2
------------------

 * Traditionally, the tests that try commands that work on the
   contents in the working tree were named with "worktree" in their
   filenames, but with the recent addition of "git worktree"
   subcommand, whose tests are also named similarly, it has become
   harder to tell them apart.  The traditional tests have been renamed
   to use "work-tree" instead in an attempt to differentiate them.

 * Many codepaths forget to check return value from git_config_set();
   the function is made to die() to make sure we do not proceed when
   setting a configuration variable failed.

 * Handling of errors while writing into our internal asynchronous
   process has been made more robust, which reduces flakiness in our
   tests.

 * "git show 'HEAD:Foo[BAR]Baz'" did not interpret the argument as a
   rev, i.e. the object named by the the pathname with wildcard
   characters in a tree object.

 * "git rev-parse --git-common-dir" used in the worktree feature
   misbehaved when run from a subdirectory.

 * The "v(iew)" subcommand of the interactive "git am -i" command was
   broken in 2.6.0 timeframe when the command was rewritten in C.

 * "git merge-tree" used to mishandle "both sides added" conflict with
   its own "create a fake ancestor file that has the common parts of
   what both sides have added and do a 3-way merge" logic; this has
   been updated to use the usual "3-way merge with an empty blob as
   the fake common ancestor file" approach used in the rest of the
   system.

 * The memory ownership rule of fill_textconv() API, which was a bit
   tricky, has been documented a bit better.

 * The documentation did not clearly state that the 'simple' mode is
   now the default for "git push" when push.default configuration is
   not set.

 * Recent versions of GNU grep are pickier when their input contains
   arbitrary binary data, which some of our tests uses.  Rewrite the
   tests to sidestep the problem.

 * A helper function "git submodule" uses since v2.7.0 to list the
   modules that match the pathspec argument given to its subcommands
   (e.g. "submodule add <repo> <path>") has been fixed.

 * "git config section.var value" to set a value in per-repository
   configuration file failed when it was run outside any repository,
   but didn't say the reason correctly.

 * The code to read the pack data using the offsets stored in the pack
   idx file has been made more carefully check the validity of the
   data in the idx.

Also includes documentation and test updates.
