#!/bin/sh

test_description='but am handling submodules'

. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-submodule-update.sh

am () {
	but format-patch --stdout --ignore-submodules=dirty "..$1" >patch &&
	may_only_be_test_must_fail "$2" &&
	$2 but am patch
}

test_submodule_switch_func "am"

am_3way () {
	but format-patch --stdout --ignore-submodules=dirty "..$1" >patch &&
	may_only_be_test_must_fail "$2" &&
	$2 but am --3way patch
}

KNOWN_FAILURE_NOFF_MERGE_ATTEMPTS_TO_MERGE_REMOVED_SUBMODULE_FILES=1
test_submodule_switch_func "am_3way"

test_expect_success 'setup diff.submodule' '
	test_cummit one &&
	INITIAL=$(but rev-parse HEAD) &&

	but init submodule &&
	(
		cd submodule &&
		test_cummit two &&
		but rev-parse HEAD >../initial-submodule
	) &&
	but submodule add ./submodule &&
	but cummit -m first &&

	(
		cd submodule &&
		test_cummit three &&
		but rev-parse HEAD >../first-submodule
	) &&
	but add submodule &&
	but cummit -m second &&
	SECOND=$(but rev-parse HEAD) &&

	(
		cd submodule &&
		but mv two.t four.t &&
		but cummit -m "second submodule" &&
		but rev-parse HEAD >../second-submodule
	) &&
	test_cummit four &&
	but add submodule &&
	but cummit --amend --no-edit &&
	THIRD=$(but rev-parse HEAD) &&
	but submodule update --init
'

run_test() {
	START_CUMMIT=$1 &&
	EXPECT=$2 &&
	# Abort any merges in progress: the previous
	# test may have failed, and we should clean up.
	test_might_fail but am --abort &&
	but reset --hard $START_CUMMIT &&
	rm -f *.patch &&
	but format-patch -1 &&
	but reset --hard $START_CUMMIT^ &&
	but submodule update &&
	but am *.patch &&
	but submodule update &&
	but -C submodule rev-parse HEAD >actual &&
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
