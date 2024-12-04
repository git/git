#!/bin/sh

test_description='pulling from symlinked subdir'

. ./test-lib.sh

# The scenario we are building:
#
#   trash\ directory/
#     clone-repo/
#       subdir/
#         bar
#     subdir-link -> clone-repo/subdir/
#
# The working directory is subdir-link.

test_expect_success SYMLINKS setup '
	mkdir subdir &&
	echo file >subdir/file &&
	git add subdir/file &&
	git commit -q -m file &&
	git clone -q . clone-repo &&
	ln -s clone-repo/subdir/ subdir-link &&
	(
		cd clone-repo &&
		git config receive.denyCurrentBranch warn
	) &&
	git config receive.denyCurrentBranch warn
'

# Demonstrate that things work if we just avoid the symlink
#
test_expect_success SYMLINKS 'pulling from real subdir' '
	(
		echo real >subdir/file &&
		git commit -m real subdir/file &&
		cd clone-repo/subdir/ &&
		git pull &&
		test real = $(cat file)
	)
'

# From subdir-link, pulling should work as it does from
# clone-repo/subdir/.
#
# Instead, the error pull gave was:
#
#   fatal: 'origin': unable to chdir or not a git archive
#   fatal: The remote end hung up unexpectedly
#
# because git would find the .git/config for the "trash directory"
# repo, not for the clone-repo repo.  The "trash directory" repo
# had no entry for origin.  Git found the wrong .git because
# git rev-parse --show-cdup printed a path relative to
# clone-repo/subdir/, not subdir-link/.  Git rev-parse --show-cdup
# used the correct .git, but when the git pull shell script did
# "cd $(git rev-parse --show-cdup)", it ended up in the wrong
# directory.  A POSIX shell's "cd" works a little differently
# than chdir() in C; "cd -P" is much closer to chdir().
#
test_expect_success SYMLINKS 'pulling from symlinked subdir' '
	(
		echo link >subdir/file &&
		git commit -m link subdir/file &&
		cd subdir-link/ &&
		git pull &&
		test link = $(cat file)
	)
'

# Prove that the remote end really is a repo, and other commands
# work fine in this context.  It's just that "git pull" breaks.
#
test_expect_success SYMLINKS 'pushing from symlinked subdir' '
	(
		cd subdir-link/ &&
		echo push >file &&
		git commit -m push ./file &&
		git push
	) &&
	echo push >expect &&
	git show HEAD:subdir/file >actual &&
	test_cmp expect actual
'

test_done
