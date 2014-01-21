#!/bin/sh

test_description='git p4 locked file behavior'

. ./lib-git-p4.sh

test_expect_success 'start p4d' '
	start_p4d
'

# See
# http://www.perforce.com/perforce/doc.current/manuals/p4sag/03_superuser.html#1088563
# for suggestions on how to configure "sitewide pessimistic locking"
# where only one person can have a file open for edit at a time.
test_expect_success 'init depot' '
	(
		cd "$cli" &&
		echo "TypeMap: +l //depot/..." | p4 typemap -i &&
		echo file1 >file1 &&
		p4 add file1 &&
		p4 submit -d "add file1"
	)
'

test_expect_success 'edit with lock not taken' '
	test_when_finished cleanup_git &&
	git p4 clone --dest="$git" //depot &&
	(
		cd "$git" &&
		echo line2 >>file1 &&
		git add file1 &&
		git commit -m "line2 in file1" &&
		git config git-p4.skipSubmitEdit true &&
		git p4 submit
	)
'

test_expect_failure 'add with lock not taken' '
	test_when_finished cleanup_git &&
	git p4 clone --dest="$git" //depot &&
	(
		cd "$git" &&
		echo line1 >>add-lock-not-taken &&
		git add file2 &&
		git commit -m "add add-lock-not-taken" &&
		git config git-p4.skipSubmitEdit true &&
		git p4 submit --verbose
	)
'

lock_in_another_client() {
	# build a different client
	cli2="$TRASH_DIRECTORY/cli2" &&
	mkdir -p "$cli2" &&
	test_when_finished "p4 client -f -d client2 && rm -rf \"$cli2\"" &&
	(
		cd "$cli2" &&
		P4CLIENT=client2 &&
		cli="$cli2" &&
		client_view "//depot/... //client2/..." &&
		p4 sync &&
		p4 open file1
	)
}

test_expect_failure 'edit with lock taken' '
	lock_in_another_client &&
	test_when_finished cleanup_git &&
	test_when_finished "cd \"$cli\" && p4 sync -f file1" &&
	git p4 clone --dest="$git" //depot &&
	(
		cd "$git" &&
		echo line3 >>file1 &&
		git add file1 &&
		git commit -m "line3 in file1" &&
		git config git-p4.skipSubmitEdit true &&
		git p4 submit --verbose
	)
'

test_expect_failure 'delete with lock taken' '
	lock_in_another_client &&
	test_when_finished cleanup_git &&
	test_when_finished "cd \"$cli\" && p4 sync -f file1" &&
	git p4 clone --dest="$git" //depot &&
	(
		cd "$git" &&
		git rm file1 &&
		git commit -m "delete file1" &&
		git config git-p4.skipSubmitEdit true &&
		git p4 submit --verbose
	)
'

test_expect_failure 'chmod with lock taken' '
	lock_in_another_client &&
	test_when_finished cleanup_git &&
	test_when_finished "cd \"$cli\" && p4 sync -f file1" &&
	git p4 clone --dest="$git" //depot &&
	(
		cd "$git" &&
		chmod +x file1 &&
		git add file1 &&
		git commit -m "chmod +x file1" &&
		git config git-p4.skipSubmitEdit true &&
		git p4 submit --verbose
	)
'

test_expect_failure 'copy with lock taken' '
	lock_in_another_client &&
	test_when_finished cleanup_git &&
	test_when_finished "cd \"$cli\" && p4 revert file2 && rm -f file2" &&
	git p4 clone --dest="$git" //depot &&
	(
		cd "$git" &&
		cp file1 file2 &&
		git add file2 &&
		git commit -m "cp file1 to file2" &&
		git config git-p4.skipSubmitEdit true &&
		git config git-p4.detectCopies true &&
		git p4 submit --verbose
	)
'

test_expect_failure 'move with lock taken' '
	lock_in_another_client &&
	test_when_finished cleanup_git &&
	test_when_finished "cd \"$cli\" && p4 sync file1 && rm -f file2" &&
	git p4 clone --dest="$git" //depot &&
	(
		cd "$git" &&
		git mv file1 file2 &&
		git commit -m "mv file1 to file2" &&
		git config git-p4.skipSubmitEdit true &&
		git config git-p4.detectRenames true &&
		git p4 submit --verbose
	)
'

test_expect_success 'kill p4d' '
	kill_p4d
'

test_done
