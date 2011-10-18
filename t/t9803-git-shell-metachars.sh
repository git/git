#!/bin/sh

test_description='git-p4 transparency to shell metachars in filenames'

. ./lib-git-p4.sh

test_expect_success 'start p4d' '
	start_p4d
'

test_expect_success 'init depot' '
	(
		cd "$cli" &&
		echo file1 >file1 &&
		p4 add file1 &&
		p4 submit -d "file1"
	)
'

test_expect_success 'shell metachars in filenames' '
	"$GITP4" clone --dest="$git" //depot &&
	test_when_finished cleanup_git &&
	(
		cd "$git" &&
		git config git-p4.skipSubmitEditCheck true &&
		echo f1 >foo\$bar &&
		git add foo\$bar &&
		echo f2 >"file with spaces" &&
		git add "file with spaces" &&
		git commit -m "add files" &&
		P4EDITOR=touch "$GITP4" submit
	) &&
	(
		cd "$cli" &&
		p4 sync ... &&
		test -e "file with spaces" &&
		test -e "foo\$bar"
	)
'

test_expect_success 'deleting with shell metachars' '
	"$GITP4" clone --dest="$git" //depot &&
	test_when_finished cleanup_git &&
	(
		cd "$git" &&
		git config git-p4.skipSubmitEditCheck true &&
		git rm foo\$bar &&
		git rm file\ with\ spaces &&
		git commit -m "remove files" &&
		P4EDITOR=touch "$GITP4" submit
	) &&
	(
		cd "$cli" &&
		p4 sync ... &&
		test ! -e "file with spaces" &&
		test ! -e foo\$bar
	)
'

test_expect_success 'kill p4d' '
	kill_p4d
'

test_done
