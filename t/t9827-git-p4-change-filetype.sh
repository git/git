#!/bin/sh

test_description='but p4 support for file type change'

. ./lib-but-p4.sh

test_expect_success 'start p4d' '
	start_p4d
'

test_expect_success 'create files' '
	(
		cd "$cli" &&
		p4 client -o | sed "/LineEnd/s/:.*/:unix/" | p4 client -i &&
		cat >file1 <<-EOF &&
		text without any funny substitution business
		EOF
		cat >file2 <<-EOF &&
		second file whose type will change
		EOF
		p4 add file1 file2 &&
		p4 submit -d "add files"
	)
'

test_expect_success SYMLINKS 'change file to symbolic link' '
	but p4 clone --dest="$but" //depot@all &&
	test_when_finished cleanup_but &&
	(
		cd "$but" &&
		but config but-p4.skipSubmitEdit true &&

		rm file2 &&
		ln -s file1 file2 &&
		but add file2 &&
		but cummit -m "symlink file1 to file2" &&
		but p4 submit &&
		p4 filelog -m 1 //depot/file2 >filelog &&
		grep "(symlink)" filelog
	)
'

test_expect_success SYMLINKS 'change symbolic link to file' '
	but p4 clone --dest="$but" //depot@all &&
	test_when_finished cleanup_but &&
	(
		cd "$but" &&
		but config but-p4.skipSubmitEdit true &&

		rm file2 &&
		cat >file2 <<-EOF &&
		This is another content for the second file.
		EOF
		but add file2 &&
		but cummit -m "re-write file2" &&
		but p4 submit &&
		p4 filelog -m 1 //depot/file2 >filelog &&
		grep "(text)" filelog
	)
'

test_done
