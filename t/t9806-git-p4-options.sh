#!/bin/sh

test_description='git-p4 options'

. ./lib-git-p4.sh

test_expect_success 'start p4d' '
	start_p4d
'

test_expect_success 'init depot' '
	(
		cd "$cli" &&
		echo file1 >file1 &&
		p4 add file1 &&
		p4 submit -d "change 1" &&
		echo file2 >file2 &&
		p4 add file2 &&
		p4 submit -d "change 2" &&
		echo file3 >file3 &&
		p4 add file3 &&
		p4 submit -d "change 3"
	)
'

test_expect_success 'clone no --git-dir' '
	test_must_fail "$GITP4" clone --git-dir=xx //depot
'

test_expect_success 'kill p4d' '
	kill_p4d
'

test_done
