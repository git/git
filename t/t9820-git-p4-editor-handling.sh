#!/bin/sh

test_description='but p4 handling of EDITOR'

. ./lib-but-p4.sh

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
# options, which but-p4 will then pass through to the shell.
test_expect_success 'EDITOR with options' '
	but p4 clone --dest="$but" //depot &&
	test_when_finished cleanup_but &&
	(
		cd "$but" &&
		echo change >file1 &&
		but cummit -m "change" file1 &&
		P4EDITOR=": >\"$but/touched\" && test-tool chmtime +5" but p4 submit &&
		test_path_is_file "$but/touched"
	)
'

test_done
