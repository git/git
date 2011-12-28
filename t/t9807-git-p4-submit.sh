#!/bin/sh

test_description='git-p4 submit'

. ./lib-git-p4.sh

test_expect_success 'start p4d' '
	start_p4d
'

test_expect_success 'init depot' '
	(
		cd "$cli" &&
		echo file1 >file1 &&
		p4 add file1 &&
		p4 submit -d "change 1"
	)
'

test_expect_success 'submit with no client dir' '
	test_when_finished cleanup_git &&
	"$GITP4" clone --dest="$git" //depot &&
	(
		cd "$git" &&
		echo file2 >file2 &&
		git add file2 &&
		git commit -m "git commit 2" &&
		rm -rf "$cli" &&
		git config git-p4.skipSubmitEdit true &&
		"$GITP4" submit
	)
'

# make two commits, but tell it to apply only from HEAD^
test_expect_success 'submit --origin' '
	test_when_finished cleanup_git &&
	"$GITP4" clone --dest="$git" //depot &&
	(
		cd "$git" &&
		test_commit "file3" &&
		test_commit "file4" &&
		git config git-p4.skipSubmitEdit true &&
		"$GITP4" submit --origin=HEAD^
	) &&
	(
		cd "$cli" &&
		p4 sync &&
		test_path_is_missing "file3.t" &&
		test_path_is_file "file4.t"
	)
'

test_expect_success 'submit with allowSubmit' '
	test_when_finished cleanup_git &&
	"$GITP4" clone --dest="$git" //depot &&
	(
		cd "$git" &&
		test_commit "file5" &&
		git config git-p4.skipSubmitEdit true &&
		git config git-p4.allowSubmit "nobranch" &&
		test_must_fail "$GITP4" submit &&
		git config git-p4.allowSubmit "nobranch,master" &&
		"$GITP4" submit
	)
'

test_expect_success 'submit with master branch name from argv' '
	test_when_finished cleanup_git &&
	"$GITP4" clone --dest="$git" //depot &&
	(
		cd "$git" &&
		test_commit "file6" &&
		git config git-p4.skipSubmitEdit true &&
		test_must_fail "$GITP4" submit nobranch &&
		git branch otherbranch &&
		git reset --hard HEAD^ &&
		test_commit "file7" &&
		"$GITP4" submit otherbranch
	) &&
	(
		cd "$cli" &&
		p4 sync &&
		test_path_is_file "file6.t" &&
		test_path_is_missing "file7.t"
	)
'

test_expect_success 'kill p4d' '
	kill_p4d
'

test_done
