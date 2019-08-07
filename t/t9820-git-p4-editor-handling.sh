#!/bin/sh

test_description='git p4 handling of EDITOR'

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

# Check that the P4EDITOR argument can be given command-line
# options, which git-p4 will then pass through to the shell.
test_expect_success 'EDITOR with options' '
	git p4 clone --dest="$git" //depot &&
	test_when_finished cleanup_git &&
	(
		cd "$git" &&
		echo change >file1 &&
		git commit -m "change" file1 &&
		P4EDITOR=": >\"$git/touched\" && test-tool chmtime +5" git p4 submit &&
		test_path_is_file "$git/touched"
	)
'

test_done
