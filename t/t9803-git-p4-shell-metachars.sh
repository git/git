#!/bin/sh

test_description='but p4 transparency to shell metachars in filenames'

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

test_expect_success 'shell metachars in filenames' '
	but p4 clone --dest="$but" //depot &&
	test_when_finished cleanup_but &&
	(
		cd "$but" &&
		but config but-p4.skipSubmitEditCheck true &&
		echo f1 >foo\$bar &&
		but add foo\$bar &&
		echo f2 >"file with spaces" &&
		but add "file with spaces" &&
		but cummit -m "add files" &&
		P4EDITOR="test-tool chmtime +5" but p4 submit
	) &&
	(
		cd "$cli" &&
		p4 sync ... &&
		test -e "file with spaces" &&
		test -e "foo\$bar"
	)
'

test_expect_success 'deleting with shell metachars' '
	but p4 clone --dest="$but" //depot &&
	test_when_finished cleanup_but &&
	(
		cd "$but" &&
		but config but-p4.skipSubmitEditCheck true &&
		but rm foo\$bar &&
		but rm file\ with\ spaces &&
		but cummit -m "remove files" &&
		P4EDITOR="test-tool chmtime +5" but p4 submit
	) &&
	(
		cd "$cli" &&
		p4 sync ... &&
		test ! -e "file with spaces" &&
		test ! -e foo\$bar
	)
'

# Create a branch with a shell metachar in its name
#
# 1. //depot/main
# 2. //depot/branch$3

test_expect_success 'branch with shell char' '
	test_when_finished cleanup_but &&
	test_create_repo "$but" &&
	(
		cd "$cli" &&

		mkdir -p main &&

		echo f1 >main/f1 &&
		p4 add main/f1 &&
		p4 submit -d "main/f1" &&

		p4 integrate //depot/main/... //depot/branch\$3/... &&
		p4 submit -d "integrate main to branch\$3" &&

		echo f1 >branch\$3/shell_char_branch_file &&
		p4 add branch\$3/shell_char_branch_file &&
		p4 submit -d "branch\$3/shell_char_branch_file" &&

		p4 branch -i <<-EOF &&
		Branch: branch\$3
		View: //depot/main/... //depot/branch\$3/...
		EOF

		p4 edit main/f1 &&
		echo "a change" >> main/f1 &&
		p4 submit -d "a change" main/f1 &&

		p4 integrate -b branch\$3 &&
		p4 resolve -am branch\$3/... &&
		p4 submit -d "integrate main to branch\$3" &&

		cd "$but" &&

		but config but-p4.branchList main:branch\$3 &&
		but p4 clone --dest=. --detect-branches //depot@all &&
		but log --all --graph --decorate --stat &&
		but reset --hard p4/depot/branch\$3 &&
		test -f shell_char_branch_file &&
		test -f f1
	)
'

test_done
