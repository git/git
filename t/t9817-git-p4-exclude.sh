#!/bin/sh

test_description='git p4 tests for excluded paths during clone and sync'

. ./lib-git-p4.sh

test_expect_success 'start p4d' '
	start_p4d
'

# Create a repo with the structure:
#
#    //depot/wanted/foo
#    //depot/discard/foo
#
# Check that we can exclude a subdirectory with both
# clone and sync operations.

test_expect_success 'create exclude repo' '
	(
		cd "$cli" &&
		mkdir -p wanted discard &&
		echo wanted >wanted/foo &&
		echo discard >discard/foo &&
		echo discard_file >discard_file &&
		echo discard_file_not >discard_file_not &&
		p4 add wanted/foo discard/foo discard_file discard_file_not &&
		p4 submit -d "initial revision"
	)
'

test_expect_success 'check the repo was created correctly' '
	test_when_finished cleanup_git &&
	git p4 clone --dest="$git" //depot/...@all &&
	(
		cd "$git" &&
		test_path_is_file wanted/foo &&
		test_path_is_file discard/foo &&
		test_path_is_file discard_file &&
		test_path_is_file discard_file_not
	)
'

test_expect_success 'clone, excluding part of repo' '
	test_when_finished cleanup_git &&
	git p4 clone -//depot/discard/... --dest="$git" //depot/...@all &&
	(
		cd "$git" &&
		test_path_is_file wanted/foo &&
		test_path_is_missing discard/foo &&
		test_path_is_file discard_file &&
		test_path_is_file discard_file_not
	)
'

test_expect_success 'clone, excluding single file, no trailing /' '
	test_when_finished cleanup_git &&
	git p4 clone -//depot/discard_file --dest="$git" //depot/...@all &&
	(
		cd "$git" &&
		test_path_is_file wanted/foo &&
		test_path_is_file discard/foo &&
		test_path_is_missing discard_file &&
		test_path_is_file discard_file_not
	)
'

test_expect_success 'clone, then sync with exclude' '
	test_when_finished cleanup_git &&
	git p4 clone -//depot/discard/... --dest="$git" //depot/...@all &&
	(
		cd "$cli" &&
		p4 edit wanted/foo discard/foo discard_file_not &&
		date >>wanted/foo &&
		date >>discard/foo &&
		date >>discard_file_not &&
		p4 submit -d "updating" &&

		cd "$git" &&
		git p4 sync -//depot/discard/... &&
		test_path_is_file wanted/foo &&
		test_path_is_missing discard/foo &&
		test_path_is_file discard_file &&
		test_path_is_file discard_file_not
	)
'

test_expect_success 'clone, then sync with exclude, no trailing /' '
	test_when_finished cleanup_git &&
	git p4 clone -//depot/discard/... -//depot/discard_file --dest="$git" //depot/...@all &&
	(
		cd "$cli" &&
		p4 edit wanted/foo discard/foo discard_file_not &&
		date >>wanted/foo &&
		date >>discard/foo &&
		date >>discard_file_not &&
		p4 submit -d "updating" &&

		cd "$git" &&
		git p4 sync -//depot/discard/... -//depot/discard_file &&
		test_path_is_file wanted/foo &&
		test_path_is_missing discard/foo &&
		test_path_is_missing discard_file &&
		test_path_is_file discard_file_not
	)
'

test_done
