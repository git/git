Git v1.8.5.5 Release Notes
==========================

Fixes since v1.8.5.4
--------------------

 * The pathspec matching code, while comparing two trees (e.g. "git
   diff A B -- path1 path2") was too aggressive and failed to match
   some paths when multiple pathspecs were involved.

 * "git repack --max-pack-size=8g" stopped being parsed correctly when
   the command was reimplemented in C.

 * A recent update to "git send-email" broke platforms where
   /etc/ssl/certs/ directory exists but cannot be used as SSL_ca_path
   (e.g. Fedora rawhide).

 * A handful of bugs around interpreting $branch@{upstream} notation
   and its lookalike, when $branch part has interesting characters,
   e.g. "@", and ":", have been fixed.

 * "git clone" would fail to clone from a repository that has a ref
   directly under "refs/", e.g. "refs/stash", because different
   validation paths do different things on such a refname.  Loosen the
   client side's validation to allow such a ref.

 * "git log --left-right A...B" lost the "leftness" of commits
   reachable from A when A is a tag as a side effect of a recent
   bugfix.  This is a regression in 1.8.4.x series.

 * "git merge-base --octopus" used to leave cleaning up suboptimal
   result to the caller, but now it does the clean-up itself.

 * "git mv A B/", when B does not exist as a directory, should error
   out, but it didn't.

Also contains typofixes, documentation updates and trivial code clean-ups.
