#!/bin/sh

test_description='cherry-pick can handle submodules'

BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-submodule-update.sh

if test "$BUT_TEST_MERGE_ALGORITHM" != ort
then
	KNOWN_FAILURE_NOFF_MERGE_DOESNT_CREATE_EMPTY_SUBMODULE_DIR=1
	KNOWN_FAILURE_NOFF_MERGE_ATTEMPTS_TO_MERGE_REMOVED_SUBMODULE_FILES=1
fi
test_submodule_switch "cherry-pick"

test_expect_success 'unrelated submodule/file conflict is ignored' '
	test_create_repo sub &&

	touch sub/file &&
	but -C sub add file &&
	but -C sub cummit -m "add a file in a submodule" &&

	test_create_repo a_repo &&
	(
		cd a_repo &&
		>a_file &&
		but add a_file &&
		but cummit -m "add a file" &&

		but branch test &&
		but checkout test &&

		mkdir sub &&
		>sub/content &&
		but add sub/content &&
		but cummit -m "add a regular folder with name sub" &&

		echo "123" >a_file &&
		but add a_file &&
		but cummit -m "modify a file" &&

		but checkout main &&

		but submodule add ../sub sub &&
		but submodule update sub &&
		but cummit -m "add a submodule info folder with name sub" &&

		but cherry-pick test
	)
'

test_done
