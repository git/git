#!/bin/sh

test_description='but p4 locked file behavior'

. ./lib-but-p4.sh

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
	test_when_finished cleanup_but &&
	but p4 clone --dest="$but" //depot &&
	(
		cd "$but" &&
		echo line2 >>file1 &&
		but add file1 &&
		but cummit -m "line2 in file1" &&
		but config but-p4.skipSubmitEdit true &&
		but p4 submit
	)
'

test_expect_success 'add with lock not taken' '
	test_when_finished cleanup_but &&
	but p4 clone --dest="$but" //depot &&
	(
		cd "$but" &&
		echo line1 >>add-lock-not-taken &&
		but add add-lock-not-taken &&
		but cummit -m "add add-lock-not-taken" &&
		but config but-p4.skipSubmitEdit true &&
		but p4 submit --verbose
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
	test_when_finished cleanup_but &&
	test_when_finished "cd \"$cli\" && p4 sync -f file1" &&
	but p4 clone --dest="$but" //depot &&
	(
		cd "$but" &&
		echo line3 >>file1 &&
		but add file1 &&
		but cummit -m "line3 in file1" &&
		but config but-p4.skipSubmitEdit true &&
		but p4 submit --verbose
	)
'

test_expect_failure 'delete with lock taken' '
	lock_in_another_client &&
	test_when_finished cleanup_but &&
	test_when_finished "cd \"$cli\" && p4 sync -f file1" &&
	but p4 clone --dest="$but" //depot &&
	(
		cd "$but" &&
		but rm file1 &&
		but cummit -m "delete file1" &&
		but config but-p4.skipSubmitEdit true &&
		but p4 submit --verbose
	)
'

test_expect_failure 'chmod with lock taken' '
	lock_in_another_client &&
	test_when_finished cleanup_but &&
	test_when_finished "cd \"$cli\" && p4 sync -f file1" &&
	but p4 clone --dest="$but" //depot &&
	(
		cd "$but" &&
		chmod +x file1 &&
		but add file1 &&
		but cummit -m "chmod +x file1" &&
		but config but-p4.skipSubmitEdit true &&
		but p4 submit --verbose
	)
'

test_expect_success 'copy with lock taken' '
	lock_in_another_client &&
	test_when_finished cleanup_but &&
	test_when_finished "cd \"$cli\" && p4 revert file2 && rm -f file2" &&
	but p4 clone --dest="$but" //depot &&
	(
		cd "$but" &&
		cp file1 file2 &&
		but add file2 &&
		but cummit -m "cp file1 to file2" &&
		but config but-p4.skipSubmitEdit true &&
		but config but-p4.detectCopies true &&
		but p4 submit --verbose
	)
'

test_expect_failure 'move with lock taken' '
	lock_in_another_client &&
	test_when_finished cleanup_but &&
	test_when_finished "cd \"$cli\" && p4 sync file1 && rm -f file2" &&
	but p4 clone --dest="$but" //depot &&
	(
		cd "$but" &&
		but mv file1 file3 &&
		but cummit -m "mv file1 to file3" &&
		but config but-p4.skipSubmitEdit true &&
		but config but-p4.detectRenames true &&
		but p4 submit --verbose
	)
'

test_done
