Git v1.6.5.5 Release Notes
==========================

Fixes since v1.6.5.4
--------------------

 * Manual pages can be formatted with older xmlto again.

 * GREP_OPTIONS exported from user's environment could have broken
   our scripted commands.

 * In configuration files, a few variables that name paths can begin with
   ~/ and ~username/ and they are expanded as expected.  This is not a
   bugfix but 1.6.6 will have this and without backporting users cannot
   easily use the same ~/.gitconfig across versions.

 * "git diff -B -M" did the same computation to hash lines of contents
   twice, and held onto memory after it has used the data in it
   unnecessarily before it freed.

 * "git diff -B" and "git diff --dirstat" was not counting newly added
   contents correctly.

 * "git format-patch revisions... -- path" issued an incorrect error
   message that suggested to use "--" on the command line when path
   does not exist in the current work tree (it is a separate matter if
   it makes sense to limit format-patch with pathspecs like that
   without using the --full-diff option).

 * "git grep -F -i StRiNg" did not work as expected.

 * Enumeration of available merge strategies iterated over the list of
   commands in a wrong way, sometimes producing an incorrect result.

 * "git shortlog" did not honor the "encoding" header embedded in the
   commit object like "git log" did.

 * Reading progress messages that come from the remote side while running
   "git pull" is given precedence over reading the actual pack data to
   prevent garbled progress message on the user's terminal.

 * "git rebase" got confused when the log message began with certain
   strings that looked like Subject:, Date: or From: header.

 * "git reset" accidentally run in .git/ directory checked out the
   work tree contents in there.


Other minor documentation updates are included.
