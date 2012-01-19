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

# Create a branch with a shell metachar in its name
#
# 1. //depot/main
# 2. //depot/branch$3

test_expect_success 'branch with shell char' '
	test_when_finished cleanup_git &&
	test_create_repo "$git" &&
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

		cd "$git" &&

		git config git-p4.branchList main:branch\$3 &&
		"$GITP4" clone --dest=. --detect-branches //depot@all &&
		git log --all --graph --decorate --stat &&
		git reset --hard p4/depot/branch\$3 &&
		test -f shell_char_branch_file &&
		test -f f1
	)
'

test_expect_success 'kill p4d' '
	kill_p4d
'

test_done
