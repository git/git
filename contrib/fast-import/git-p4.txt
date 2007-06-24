git-p4 - Perforce <-> Git converter using git-fast-import

Usage
=====

git-p4 supports two main modes: Importing from Perforce to a Git repository is
done using "git-p4 sync" or "git-p4 rebase". Submitting changes from Git back
to Perforce is done using "git-p4 submit".

Importing
=========

You can simply start with

  git-p4 clone //depot/path/project

or

  git-p4 clone //depot/path/project myproject

This will create an empty git repository in a subdirectory called "project" (or
"myproject" with the second command), import the head revision from the
specified perforce path into a git "p4" branch (remotes/p4 actually), create a
master branch off it and check it out. If you want the entire history (not just
the head revision) then you can simply append a "@all" to the depot path:

  git-p4 clone //depot/project/main@all myproject



If you want more control you can also use the git-p4 sync command directly:

  mkdir repo-git
  cd repo-git
  git init
  git-p4 sync //path/in/your/perforce/depot

This will import the current head revision of the specified depot path into a
"remotes/p4/master" branch of your git repository. You can use the
--branch=mybranch option to use a different branch.

If you want to import the entire history of a given depot path just use

  git-p4 sync //path/in/depot@all

To achieve optimal compression you may want to run 'git repack -a -d -f' after
a big import. This may take a while.

Support for Perforce integrations is still work in progress. Don't bother
trying it unless you want to hack on it :)

Incremental Imports
===================

After an initial import you can easily synchronize your git repository with
newer changes from the Perforce depot by just calling

  git-p4 sync

in your git repository. By default the "remotes/p4/master" branch is updated.

It is recommended to run 'git repack -a -d -f' from time to time when using
incremental imports to optimally combine the individual git packs that each
incremental import creates through the use of git-fast-import.


A useful setup may be that you have a periodically updated git repository
somewhere that contains a complete import of a Perforce project. That git
repository can be used to clone the working repository from and one would
import from Perforce directly after cloning using git-p4. If the connection to
the Perforce server is slow and the working repository hasn't been synced for a
while it may be desirable to fetch changes from the origin git repository using
the efficient git protocol. git-p4 supports this setup by calling "git fetch origin"
by default if there is an origin branch. You can disable this using

  git config git-p4.syncFromOrigin false

Updating
========

A common working pattern is to fetch the latest changes from the Perforce depot
and merge them with local uncommitted changes. The recommended way is to use
git's rebase mechanism to preserve linear history. git-p4 provides a convenient

  git-p4 rebase

command that calls git-p4 sync followed by git rebase to rebase the current
working branch.

Submitting
==========

git-p4 has support for submitting changes from a git repository back to the
Perforce depot. This requires a Perforce checkout separate to your git
repository. To submit all changes that are in the current git branch but not in
the "p4" branch (or "origin" if "p4" doesn't exist) simply call

    git-p4 submit

in your git repository. If you want to submit changes in a specific branch that
is not your current git branch you can also pass that as an argument:

    git-p4 submit mytopicbranch

You can override the reference branch with the --origin=mysourcebranch option.

If a submit fails you may have to "p4 resolve" and submit manually. You can
continue importing the remaining changes with

  git-p4 submit --continue

After submitting you should sync your perforce import branch ("p4" or "origin")
from Perforce using git-p4's sync command.

If you have changes in your working directory that you haven't committed into
git yet but that you want to commit to Perforce directly ("quick fixes") then
you do not have to go through the intermediate step of creating a git commit
first but you can just call

  git-p4 submit --direct


Example
=======

# Clone a repository
  git-p4 clone //depot/path/project
# Enter the newly cloned directory
  cd project
# Do some work...
  vi foo.h
# ... and commit locally to gi
  git commit foo.h
# In the meantime somebody submitted changes to the Perforce depot. Rebase your latest
# changes against the latest changes in Perforce:
  git-p4 rebase
# Submit your locally committed changes back to Perforce
  git-p4 submit
# ... and synchronize with Perforce
  git-p4 rebase


Implementation Details...
=========================

* Changesets from Perforce are imported using git fast-import.
* The import does not require anything from the Perforce client view as it just uses
  "p4 print //depot/path/file#revision" to get the actual file contents.
* Every imported changeset has a special [git-p4...] line at the
  end of the log message that gives information about the corresponding
  Perforce change number and is also used by git-p4 itself to find out
  where to continue importing when doing incremental imports.
  Basically when syncing it extracts the perforce change number of the
  latest commit in the "p4" branch and uses "p4 changes //depot/path/...@changenum,#head"
  to find out which changes need to be imported.
* git-p4 submit uses "git rev-list" to pick the commits between the "p4" branch
  and the current branch.
  The commits themselves are applied using git diff/format-patch ... | git apply

