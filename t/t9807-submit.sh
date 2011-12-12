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

test_expect_success 'kill p4d' '
	kill_p4d
'

test_done
