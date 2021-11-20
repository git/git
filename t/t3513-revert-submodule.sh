#!/bin/sh

test_description='revert can handle submodules'

. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-submodule-update.sh

# Create a revert that moves from HEAD (including any test modifications to
# the work tree) to $1 by first checking out $1 and reverting it. Reverting
# the revert is the transition we test for. We tar the current work tree
# first so we can restore the work tree test setup after doing the checkout
# and revert.  We test here that the restored work tree content is identical
# to that at the beginning. The last revert is then tested by the framework.
git_revert () {
	git status -su >expect &&
	ls -1pR * >>expect &&
	"$TAR" cf "$TRASH_DIRECTORY/tmp.tar" * &&
	may_only_be_test_must_fail "$2" &&
	$2 git checkout "$1" &&
	if test -n "$2"
	then
		return
	fi &&
	git revert HEAD &&
	rm -rf * &&
	"$TAR" xf "$TRASH_DIRECTORY/tmp.tar" &&
	git status -su >actual &&
	ls -1pR * >>actual &&
	test_cmp expect actual &&
	git revert HEAD
}

if test "$GIT_TEST_MERGE_ALGORITHM" != ort
then
	KNOWN_FAILURE_NOFF_MERGE_DOESNT_CREATE_EMPTY_SUBMODULE_DIR=1
fi
test_submodule_switch_func "git_revert"

test_done
