#!/bin/sh

test_description='cherry-pick can handle submodules'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-submodule-update.sh

if test "$GIT_TEST_MERGE_ALGORITHM" != ort
then
	KNOWN_FAILURE_NOFF_MERGE_DOESNT_CREATE_EMPTY_SUBMODULE_DIR=1
	KNOWN_FAILURE_NOFF_MERGE_ATTEMPTS_TO_MERGE_REMOVED_SUBMODULE_FILES=1
fi
test_submodule_switch "cherry-pick"

test_expect_success 'unrelated submodule/file conflict is ignored' '
	test_create_repo sub &&

	touch sub/file &&
	git -C sub add file &&
	git -C sub commit -m "add a file in a submodule" &&

	test_create_repo a_repo &&
	(
		cd a_repo &&
		>a_file &&
		git add a_file &&
		git commit -m "add a file" &&

		git branch test &&
		git checkout test &&

		mkdir sub &&
		>sub/content &&
		git add sub/content &&
		git commit -m "add a regular folder with name sub" &&

		echo "123" >a_file &&
		git add a_file &&
		git commit -m "modify a file" &&

		git checkout main &&

		git submodule add ../sub sub &&
		git submodule update sub &&
		git commit -m "add a submodule info folder with name sub" &&

		git cherry-pick test
	)
'

test_done
