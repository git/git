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
	but add subdir/file &&
	but cummit -q -m file &&
	but clone -q . clone-repo &&
	ln -s clone-repo/subdir/ subdir-link &&
	(
		cd clone-repo &&
		but config receive.denyCurrentBranch warn
	) &&
	but config receive.denyCurrentBranch warn
'

# Demonstrate that things work if we just avoid the symlink
#
test_expect_success SYMLINKS 'pulling from real subdir' '
	(
		echo real >subdir/file &&
		but cummit -m real subdir/file &&
		cd clone-repo/subdir/ &&
		but pull &&
		test real = $(cat file)
	)
'

# From subdir-link, pulling should work as it does from
# clone-repo/subdir/.
#
# Instead, the error pull gave was:
#
#   fatal: 'origin': unable to chdir or not a but archive
#   fatal: The remote end hung up unexpectedly
#
# because but would find the .but/config for the "trash directory"
# repo, not for the clone-repo repo.  The "trash directory" repo
# had no entry for origin.  Git found the wrong .but because
# but rev-parse --show-cdup printed a path relative to
# clone-repo/subdir/, not subdir-link/.  Git rev-parse --show-cdup
# used the correct .but, but when the but pull shell script did
# "cd $(but rev-parse --show-cdup)", it ended up in the wrong
# directory.  A POSIX shell's "cd" works a little differently
# than chdir() in C; "cd -P" is much closer to chdir().
#
test_expect_success SYMLINKS 'pulling from symlinked subdir' '
	(
		echo link >subdir/file &&
		but cummit -m link subdir/file &&
		cd subdir-link/ &&
		but pull &&
		test link = $(cat file)
	)
'

# Prove that the remote end really is a repo, and other commands
# work fine in this context.  It's just that "but pull" breaks.
#
test_expect_success SYMLINKS 'pushing from symlinked subdir' '
	(
		cd subdir-link/ &&
		echo push >file &&
		but cummit -m push ./file &&
		but push
	) &&
	test push = $(but show HEAD:subdir/file)
'

test_done
