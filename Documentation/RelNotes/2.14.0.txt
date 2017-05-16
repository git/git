Git 2.14 Release Notes
======================

Backward compatibility notes.

 * Use of an empty string as a pathspec element that is used for
   'everything matches' is still warned and Git asks users to use a
   more explicit '.' for that instead.  The hope is that existing
   users will not mind this change, and eventually the warning can be
   turned into a hard error, upgrading the deprecation into removal of
   this (mis)feature.  That is not scheduled to happen in the upcoming
   release (yet).

 * Git now avoids blindly falling back to ".git" when the setup
   sequence said we are _not_ in Git repository.  A corner case that
   happens to work right now may be broken by a call to die("BUG").
   We've tried hard to locate such cases and fixed them, but there
   might still be cases that need to be addressed--bug reports are
   greatly appreciated.


Updates since v2.13
-------------------

UI, Workflows & Features

 * The colors in which "git status --short --branch" showed the names
   of the current branch and its remote-tracking branch are now
   configurable.

 * "git clone" learned the "--no-tags" option not to fetch all tags
   initially, and also set up the tagopt not to follow any tags in
   subsequent fetches.

 * "git archive --format=zip" learned to use zip64 extension when
   necessary to go beyond the 4GB limit.
   (merge 867e40ff3a rs/large-zip later to maint).


Performance, Internal Implementation, Development Support etc.

 * The default packed-git limit value has been raised on larger
   platforms to save "git fetch" from a (recoverable) failure while
   "gc" is running in parallel.

 * Code to update the cache-tree has been tightened so that we won't
   accidentally write out any 0{40} entry in the tree object.
   (merge a96d3cc3f6 jk/no-null-sha1-in-cache-tree later to maint).

 * Attempt to allow us notice "fishy" situation where we fail to
   remove the temporary directory used during the test.

 * Travis CI gained a task to format the documentation with both
   AsciiDoc and AsciiDoctor.
   (merge 505ad91304 ls/travis-doc-asciidoctor later to maint).

 * Some platforms have ulong that is smaller than time_t, and our
   historical use of ulong for timestamp would mean they cannot
   represent some timestamp that the platform allows.  Invent a
   separate and dedicated timestamp_t (so that we can distingiuish
   timestamps and a vanilla ulongs, which along is already a good
   move), and then declare uintmax_t is the type to be used as the
   timestamp_t.


Also contains various documentation updates and code clean-ups.


Fixes since v2.13
-----------------

Unless otherwise noted, all the fixes since v2.13 in the maintenance
track are contained in this release (see the maintenance releases'
notes for details).

 * "git gc" did not interact well with "git worktree"-managed
   per-worktree refs.

 * "git cherry-pick" and other uses of the sequencer machinery
   mishandled a trailer block whose last line is an incomplete line.
   This has been fixed so that an additional sign-off etc. are added
   after completing the existing incomplete line.
   (merge 44dc738a39 jt/use-trailer-api-in-commands later to maint).

 * The codepath in "git am" that is used when running "git rebase"
   leaked memory held for the log message of the commits being rebased.
   (merge 721f5f1e35 jk/am-leakfix later to maint).

 * "git clone --config var=val" is a way to populate the
   per-repository configuration file of the new repository, but it did
   not work well when val is an empty string.  This has been fixed.
   (merge db4eca1fea jn/clone-add-empty-config-from-command-line later to maint).

 * Other minor doc, test and build updates and code cleanups.
   (merge 515360f9e9 jn/credential-doc-on-clear later to maint).
   (merge 0e6d899fee ab/aix-needs-compat-regex later to maint).
   (merge e294e8959f jc/apply-fix-mismerge later to maint).
