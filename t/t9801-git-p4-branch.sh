#!/bin/sh

test_description='git-p4 p4 branching tests'

. ./lib-git-p4.sh

test_expect_success 'start p4d' '
	start_p4d
'

#
# 1: //depot/main/f1
# 2: //depot/main/f2
# 3: integrate //depot/main/... -> //depot/branch1/...
# 4: //depot/main/f4
# 5: //depot/branch1/f5
# .: named branch branch2
# 6: integrate -b branch2
# 7: //depot/branch2/f7
# 8: //depot/main/f8
#
test_expect_success 'basic p4 branches' '
	(
		cd "$cli" &&
		mkdir -p main &&

		echo f1 >main/f1 &&
		p4 add main/f1 &&
		p4 submit -d "main/f1" &&

		echo f2 >main/f2 &&
		p4 add main/f2 &&
		p4 submit -d "main/f2" &&

		p4 integrate //depot/main/... //depot/branch1/... &&
		p4 submit -d "integrate main to branch1" &&

		echo f4 >main/f4 &&
		p4 add main/f4 &&
		p4 submit -d "main/f4" &&

		echo f5 >branch1/f5 &&
		p4 add branch1/f5 &&
		p4 submit -d "branch1/f5" &&

		p4 branch -i <<-EOF &&
		Branch: branch2
		View: //depot/main/... //depot/branch2/...
		EOF

		p4 integrate -b branch2 &&
		p4 submit -d "integrate main to branch2" &&

		echo f7 >branch2/f7 &&
		p4 add branch2/f7 &&
		p4 submit -d "branch2/f7" &&

		echo f8 >main/f8 &&
		p4 add main/f8 &&
		p4 submit -d "main/f8"
	)
'

test_expect_success 'import main, no branch detection' '
	test_when_finished cleanup_git &&
	"$GITP4" clone --dest="$git" //depot/main@all &&
	(
		cd "$git" &&
		git log --oneline --graph --decorate --all &&
		git rev-list master >wc &&
		test_line_count = 4 wc
	)
'

test_expect_success 'import branch1, no branch detection' '
	test_when_finished cleanup_git &&
	"$GITP4" clone --dest="$git" //depot/branch1@all &&
	(
		cd "$git" &&
		git log --oneline --graph --decorate --all &&
		git rev-list master >wc &&
		test_line_count = 2 wc
	)
'

test_expect_success 'import branch2, no branch detection' '
	test_when_finished cleanup_git &&
	"$GITP4" clone --dest="$git" //depot/branch2@all &&
	(
		cd "$git" &&
		git log --oneline --graph --decorate --all &&
		git rev-list master >wc &&
		test_line_count = 2 wc
	)
'

test_expect_success 'import depot, no branch detection' '
	test_when_finished cleanup_git &&
	"$GITP4" clone --dest="$git" //depot@all &&
	(
		cd "$git" &&
		git log --oneline --graph --decorate --all &&
		git rev-list master >wc &&
		test_line_count = 8 wc
	)
'

test_expect_success 'import depot, branch detection' '
	test_when_finished cleanup_git &&
	"$GITP4" clone --dest="$git" --detect-branches //depot@all &&
	(
		cd "$git" &&

		git log --oneline --graph --decorate --all &&

		# 4 main commits
		git rev-list master >wc &&
		test_line_count = 4 wc &&

		# 3 main, 1 integrate, 1 on branch2
		git rev-list p4/depot/branch2 >wc &&
		test_line_count = 5 wc &&

		# no branch1, since no p4 branch created for it
		test_must_fail git show-ref p4/depot/branch1
	)
'

test_expect_success 'import depot, branch detection, branchList branch definition' '
	test_when_finished cleanup_git &&
	test_create_repo "$git" &&
	(
		cd "$git" &&
		git config git-p4.branchList main:branch1 &&
		"$GITP4" clone --dest=. --detect-branches //depot@all &&

		git log --oneline --graph --decorate --all &&

		# 4 main commits
		git rev-list master >wc &&
		test_line_count = 4 wc &&

		# 3 main, 1 integrate, 1 on branch2
		git rev-list p4/depot/branch2 >wc &&
		test_line_count = 5 wc &&

		# 2 main, 1 integrate, 1 on branch1
		git rev-list p4/depot/branch1 >wc &&
		test_line_count = 4 wc
	)
'

test_expect_success 'restart p4d' '
	kill_p4d &&
	start_p4d
'

#
# 1: //depot/branch1/file1
#    //depot/branch1/file2
# 2: integrate //depot/branch1/... -> //depot/branch2/...
# 3: //depot/branch1/file3
# 4: //depot/branch1/file2 (edit)
# 5: integrate //depot/branch1/... -> //depot/branch3/...
#
## Create a simple branch structure in P4 depot.
test_expect_success 'add simple p4 branches' '
	(
		cd "$cli" &&
		mkdir branch1 &&
		cd branch1 &&
		echo file1 >file1 &&
		echo file2 >file2 &&
		p4 add file1 file2 &&
		p4 submit -d "Create branch1" &&
		p4 integrate //depot/branch1/... //depot/branch2/... &&
		p4 submit -d "Integrate branch2 from branch1" &&
		echo file3 >file3 &&
		p4 add file3 &&
		p4 submit -d "add file3 in branch1" &&
		p4 open file2 &&
		echo update >>file2 &&
		p4 submit -d "update file2 in branch1" &&
		p4 integrate //depot/branch1/... //depot/branch3/... &&
		p4 submit -d "Integrate branch3 from branch1"
	)
'

# Configure branches through git-config and clone them.
# All files are tested to make sure branches were cloned correctly.
# Finally, make an update to branch1 on P4 side to check if it is imported
# correctly by git-p4.
test_expect_success 'git-p4 clone simple branches' '
	test_when_finished cleanup_git &&
	test_create_repo "$git" &&
	(
		cd "$git" &&
		git config git-p4.branchList branch1:branch2 &&
		git config --add git-p4.branchList branch1:branch3 &&
		"$GITP4" clone --dest=. --detect-branches //depot@all &&
		git log --all --graph --decorate --stat &&
		git reset --hard p4/depot/branch1 &&
		test -f file1 &&
		test -f file2 &&
		test -f file3 &&
		grep update file2 &&
		git reset --hard p4/depot/branch2 &&
		test -f file1 &&
		test -f file2 &&
		test ! -f file3 &&
		! grep update file2 &&
		git reset --hard p4/depot/branch3 &&
		test -f file1 &&
		test -f file2 &&
		test -f file3 &&
		grep update file2 &&
		cd "$cli" &&
		cd branch1 &&
		p4 edit file2 &&
		echo file2_ >>file2 &&
		p4 submit -d "update file2 in branch3" &&
		cd "$git" &&
		git reset --hard p4/depot/branch1 &&
		"$GITP4" rebase &&
		grep file2_ file2
	)
'

# Create a complex branch structure in P4 depot to check if they are correctly
# cloned. The branches are created from older changelists to check if git-p4 is
# able to correctly detect them.
# The final expected structure is:
# `branch1
# | `- file1
# | `- file2 (updated)
# | `- file3
# `branch2
# | `- file1
# | `- file2
# `branch3
# | `- file1
# | `- file2 (updated)
# | `- file3
# `branch4
# | `- file1
# | `- file2
# `branch5
#   `- file1
#   `- file2
#   `- file3
test_expect_success 'git-p4 add complex branches' '
	test_when_finished cleanup_git &&
	test_create_repo "$git" &&
	(
		cd "$cli" &&
		changelist=$(p4 changes -m1 //depot/... | cut -d" " -f2) &&
		changelist=$(($changelist - 5)) &&
		p4 integrate //depot/branch1/...@$changelist //depot/branch4/... &&
		p4 submit -d "Integrate branch4 from branch1@${changelist}" &&
		changelist=$(($changelist + 2)) &&
		p4 integrate //depot/branch1/...@$changelist //depot/branch5/... &&
		p4 submit -d "Integrate branch5 from branch1@${changelist}"
	)
'

# Configure branches through git-config and clone them. git-p4 will only be able
# to clone the original structure if it is able to detect the origin changelist
# of each branch.
test_expect_success 'git-p4 clone complex branches' '
	test_when_finished cleanup_git &&
	test_create_repo "$git" &&
	(
		cd "$git" &&
		git config git-p4.branchList branch1:branch2 &&
		git config --add git-p4.branchList branch1:branch3 &&
		git config --add git-p4.branchList branch1:branch4 &&
		git config --add git-p4.branchList branch1:branch5 &&
		"$GITP4" clone --dest=. --detect-branches //depot@all &&
		git log --all --graph --decorate --stat &&
		git reset --hard p4/depot/branch1 &&
		test_path_is_file file1 &&
		test_path_is_file file2 &&
		test_path_is_file file3 &&
		grep update file2 &&
		git reset --hard p4/depot/branch2 &&
		test_path_is_file file1 &&
		test_path_is_file file2 &&
		test_path_is_missing file3 &&
		! grep update file2 &&
		git reset --hard p4/depot/branch3 &&
		test_path_is_file file1 &&
		test_path_is_file file2 &&
		test_path_is_file file3 &&
		grep update file2 &&
		git reset --hard p4/depot/branch4 &&
		test_path_is_file file1 &&
		test_path_is_file file2 &&
		test_path_is_missing file3 &&
		! grep update file2 &&
		git reset --hard p4/depot/branch5 &&
		test_path_is_file file1 &&
		test_path_is_file file2 &&
		test_path_is_file file3 &&
		! grep update file2 &&
		test_path_is_missing .git/git-p4-tmp
	)
'

test_expect_success 'kill p4d' '
	kill_p4d
'

test_done
