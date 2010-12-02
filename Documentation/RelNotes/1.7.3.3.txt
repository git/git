Git v1.7.3.3 Release Notes
==========================

Fixes since v1.7.3.2
--------------------

 * "git apply" segfaulted when a bogus input is fed to it.

 * Running "git cherry-pick --ff" on a root commit segfaulted.

 * "diff", "blame" and friends incorrectly applied textconv filters to
   symlinks.

 * Highlighting of whitespace breakage in "diff" output was showing
   incorrect amount of whitespaces when blank-at-eol is set and the line
   consisted only of whitespaces and a TAB.

 * "diff" was overly inefficient when trying to find the line to use for
   the function header (i.e. equivalent to --show-c-function of GNU diff).

 * "git imap-send" depends on libcrypto but our build rule relied on the
   linker to implicitly link it via libssl, which was wrong.

 * "git merge-file" can be called from within a subdirectory now.

 * "git repack -f" expanded and recompressed non-delta objects in the
   existing pack, which was wasteful.  Use new "-F" option if you really
   want to (e.g. when changing the pack.compression level).

 * "git rev-list --format="...%x00..." incorrectly chopped its output
   at NUL.

 * "git send-email" did not correctly remove duplicate mail addresses from
   the Cc: header that appear on the To: header.

 * The completion script (in contrib/completion) ignored lightweight tags
   in __git_ps1().

 * "git-blame" mode (in contrib/emacs) didn't say (require 'format-spec)
   even though it depends on it; it didn't work with Emacs 22 or older
   unless Gnus is used.

 * "git-p4" (in contrib/) did not correctly handle deleted files.


Other minor fixes and documentation updates may be included.
