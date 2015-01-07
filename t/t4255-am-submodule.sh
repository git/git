#!/bin/sh

test_description='git am handling submodules'

. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-submodule-update.sh

am () {
	git format-patch --stdout --ignore-submodules=dirty "..$1" | git am -
}

test_submodule_switch "am"

am_3way () {
	git format-patch --stdout --ignore-submodules=dirty "..$1" | git am --3way -
}

KNOWN_FAILURE_NOFF_MERGE_ATTEMPTS_TO_MERGE_REMOVED_SUBMODULE_FILES=1
test_submodule_switch "am_3way"

test_expect_success 'setup diff.submodule' '
	test_commit one &&
	INITIAL=$(git rev-parse HEAD) &&

	git init submodule &&
	(
		cd submodule &&
		test_commit two &&
		git rev-parse HEAD >../initial-submodule
	) &&
	git submodule add ./submodule &&
	git commit -m first &&

	(
		cd submodule &&
		test_commit three &&
		git rev-parse HEAD >../first-submodule
	) &&
	git add submodule &&
	git commit -m second &&
	SECOND=$(git rev-parse HEAD) &&

	(
		cd submodule &&
		git mv two.t four.t &&
		git commit -m "second submodule" &&
		git rev-parse HEAD >../second-submodule
	) &&
	test_commit four &&
	git add submodule &&
	git commit --amend --no-edit &&
	THIRD=$(git rev-parse HEAD) &&
	git submodule update --init
'

run_test() {
	START_COMMIT=$1 &&
	EXPECT=$2 &&
	# Abort any merges in progress: the previous
	# test may have failed, and we should clean up.
	test_might_fail git am --abort &&
	git reset --hard $START_COMMIT &&
	rm -f *.patch &&
	git format-patch -1 &&
	git reset --hard $START_COMMIT^ &&
	git submodule update &&
	git am *.patch &&
	git submodule update &&
	git -C submodule rev-parse HEAD >actual &&
	test_cmp $EXPECT actual
}

test_expect_success 'diff.submodule unset' '
	test_unconfig diff.submodule &&
	run_test $SECOND first-submodule
'

test_expect_success 'diff.submodule unset with extra file' '
	test_unconfig diff.submodule &&
	run_test $THIRD second-submodule
'

test_expect_success 'diff.submodule=log' '
	test_config diff.submodule log &&
	run_test $SECOND first-submodule
'

test_expect_success 'diff.submodule=log with extra file' '
	test_config diff.submodule log &&
	run_test $THIRD second-submodule
'

test_done
