#!/bin/sh

last_shelved_change () {
	p4 changes -s shelved -m1 | cut -d " " -f 2
}

test_description='git p4 unshelve'

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
		: >file_to_delete &&
		: >file_to_move &&
		p4 add file_to_delete &&
		p4 add file_to_move &&
		p4 submit -d "add files to delete" &&
		echo file_to_integrate >file_to_integrate &&
		p4 add file_to_integrate &&
		p4 submit -d "add file to integrate"
	)
'

# Create an initial clone, with a commit unrelated to the P4 change
# on HEAD
test_expect_success 'initial clone' '
	git p4 clone --dest="$git" //depot/@all &&
    test_commit -C "$git" "unrelated"
'

test_expect_success 'create shelved changelist' '
	(
		cd "$cli" &&
		p4 edit file1 &&
		echo "a change" >>file1 &&
		echo "new file" >file2 &&
		p4 add file2 &&
		p4 delete file_to_delete &&
		p4 edit file_to_move &&
		p4 move file_to_move moved_file &&
		p4 integrate file_to_integrate integrated_file &&
		p4 opened &&
		p4 shelve -i <<EOF
Change: new
Description:
	Test commit

	Further description
Files:
	//depot/file1
	//depot/file2
	//depot/file_to_delete
	//depot/file_to_move
	//depot/moved_file
	//depot/integrated_file
EOF

	) &&
	(
		cd "$git" &&
		change=$(last_shelved_change) &&
		git p4 unshelve $change &&
		git show refs/remotes/p4-unshelved/$change >actual &&
		grep -q "Further description" actual &&
		git cherry-pick refs/remotes/p4-unshelved/$change &&
		test_path_is_file file2 &&
		test_cmp file1 "$cli"/file1 &&
		test_cmp file2 "$cli"/file2 &&
		test_cmp file_to_integrate "$cli"/integrated_file &&
		test_path_is_missing file_to_delete &&
		test_path_is_missing file_to_move &&
		test_path_is_file moved_file
	)
'

test_expect_success 'update shelved changelist and re-unshelve' '
	test_when_finished cleanup_git &&
	(
		cd "$cli" &&
		change=$(last_shelved_change) &&
		echo "file3" >file3 &&
		p4 add -c $change file3 &&
		p4 shelve -i -r <<EOF &&
Change: $change
Description:
	Test commit

	Further description
Files:
	//depot/file1
	//depot/file2
	//depot/file3
	//depot/file_to_delete
EOF
		p4 describe $change
	) &&
	(
		cd "$git" &&
		change=$(last_shelved_change) &&
		git p4 unshelve $change &&
		git diff refs/remotes/p4-unshelved/$change.0 refs/remotes/p4-unshelved/$change | grep -q file3
	)
'

shelve_one_file () {
	description="Change to be unshelved" &&
	file="$1" &&
	p4 shelve -i <<EOF
Change: new
Description:
	$description
Files:
	$file
EOF
}

# This is the tricky case where the shelved changelist base revision doesn't
# match git-p4's idea of the base revision
#
# We will attempt to unshelve a change that is based on a change one commit
# ahead of p4/master

test_expect_success 'create shelved changelist based on p4 change ahead of p4/master' '
	git p4 clone --dest="$git" //depot/@all &&
	(
		cd "$cli" &&
		p4 revert ... &&
		p4 edit file1 &&
		echo "foo" >>file1 &&
		p4 submit -d "change:foo" &&
		p4 edit file1 &&
		echo "bar" >>file1 &&
		shelve_one_file //depot/file1 &&
		change=$(last_shelved_change) &&
		p4 describe -S $change >out.txt &&
		grep -q "Change to be unshelved" out.txt
	)
'

# Now try to unshelve it.
test_expect_success 'try to unshelve the change' '
	test_when_finished cleanup_git &&
	(
		change=$(last_shelved_change) &&
		cd "$git" &&
		git p4 unshelve $change >out.txt &&
		grep -q "unshelved changelist $change" out.txt
	)
'

# Specify the origin. Create 2 unrelated files, and check that
# we only get the one in HEAD~, not the one in HEAD.

test_expect_success 'unshelve specifying the origin' '
	(
		cd "$cli" &&
		: >unrelated_file0 &&
		p4 add unrelated_file0 &&
		p4 submit -d "unrelated" &&
		: >unrelated_file1 &&
		p4 add unrelated_file1 &&
		p4 submit -d "unrelated" &&
		: >file_to_shelve &&
		p4 add file_to_shelve &&
		shelve_one_file //depot/file_to_shelve
	) &&
	test_when_finished cleanup_git &&
	git p4 clone --dest="$git" //depot/@all &&
	(
		cd "$git" &&
		change=$(last_shelved_change) &&
		git p4 unshelve --origin HEAD~ $change &&
		git checkout refs/remotes/p4-unshelved/$change &&
		test_path_is_file unrelated_file0 &&
		test_path_is_missing unrelated_file1 &&
		test_path_is_file file_to_shelve
	)
'

test_done
