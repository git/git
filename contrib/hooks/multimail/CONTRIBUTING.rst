Contributing
============

git-multimail is an open-source project, built by volunteers. We would
welcome your help!

The current maintainers are `Matthieu Moy <http://matthieu-moy.fr>`__ and
`Michael Haggerty <https://github.com/mhagger>`__.

Please note that although a copy of git-multimail is distributed in
the "contrib" section of the main Git project, development takes place
in a separate `git-multimail repository on GitHub`_.

Whenever enough changes to git-multimail have accumulated, a new
code-drop of git-multimail will be submitted for inclusion in the Git
project.

We use the GitHub issue tracker to keep track of bugs and feature
requests, and we use GitHub pull requests to exchange patches (though,
if you prefer, you can send patches via the Git mailing list with CC
to the maintainers). Please sign off your patches as per the `Git
project practice
<https://github.com/git/git/blob/master/Documentation/SubmittingPatches#L234>`__.

Please vote for issues you would like to be addressed in priority
(click "add your reaction" and then the "+1" thumbs-up button on the
GitHub issue).

General discussion of git-multimail can take place on the main `Git
mailing list`_.

Please CC emails regarding git-multimail to the maintainers so that we
don't overlook them.

Help needed: testers/maintainer for specific environments/OS
------------------------------------------------------------

The current maintainer uses and tests git-multimail on Linux with the
Generic environment. More testers, or better contributors are needed
to test git-multimail on other real-life setups:

* Mac OS X, Windows: git-multimail is currently not supported on these
  platforms. But since we have no external dependencies and try to
  write code as portable as possible, it is possible that
  git-multimail already runs there and if not, it is likely that it
  could be ported easily.

  Patches to improve support for Windows and OS X are welcome.
  Ideally, there would be a sub-maintainer for each OS who would test
  at least once before each release (around twice a year).

* Gerrit, Stash, Gitolite environments: although the testsuite
  contains tests for these environments, a tester/maintainer for each
  environment would be welcome to test and report failure (or success)
  on real-life environments periodically (here also, feedback before
  each release would be highly appreciated).


.. _`git-multimail repository on GitHub`: https://github.com/git-multimail/git-multimail
.. _`Git mailing list`: git@vger.kernel.org
