#!/bin/sh

test_description='but p4 wildcards'

. ./lib-but-p4.sh

test_expect_success 'start p4d' '
	start_p4d
'

test_expect_success 'add p4 files with wildcards in the names' '
	(
		cd "$cli" &&
		printf "file2\nhas\nsome\nrandom\ntext\n" >file2 &&
		p4 add file2 &&
		echo file-wild-hash >file-wild#hash &&
		if test_have_prereq !MINGW,!CYGWIN
		then
			echo file-wild-star >file-wild\*star
		fi &&
		echo file-wild-at >file-wild@at &&
		echo file-wild-percent >file-wild%percent &&
		p4 add -f file-wild* &&
		p4 submit -d "file wildcards"
	)
'

test_expect_success 'wildcard files but p4 clone' '
	but p4 clone --dest="$but" //depot &&
	test_when_finished cleanup_but &&
	(
		cd "$but" &&
		test -f file-wild#hash &&
		if test_have_prereq !MINGW,!CYGWIN
		then
			test -f file-wild\*star
		fi &&
		test -f file-wild@at &&
		test -f file-wild%percent
	)
'

test_expect_success 'wildcard files submit back to p4, add' '
	test_when_finished cleanup_but &&
	but p4 clone --dest="$but" //depot &&
	(
		cd "$but" &&
		echo but-wild-hash >but-wild#hash &&
		if test_have_prereq !MINGW,!CYGWIN
		then
			echo but-wild-star >but-wild\*star
		fi &&
		echo but-wild-at >but-wild@at &&
		echo but-wild-percent >but-wild%percent &&
		but add but-wild* &&
		but cummit -m "add some wildcard filenames" &&
		but config but-p4.skipSubmitEdit true &&
		but p4 submit
	) &&
	(
		cd "$cli" &&
		test_path_is_file but-wild#hash &&
		if test_have_prereq !MINGW,!CYGWIN
		then
			test_path_is_file but-wild\*star
		fi &&
		test_path_is_file but-wild@at &&
		test_path_is_file but-wild%percent
	)
'

test_expect_success 'wildcard files submit back to p4, modify' '
	test_when_finished cleanup_but &&
	but p4 clone --dest="$but" //depot &&
	(
		cd "$but" &&
		echo new-line >>but-wild#hash &&
		if test_have_prereq !MINGW,!CYGWIN
		then
			echo new-line >>but-wild\*star
		fi &&
		echo new-line >>but-wild@at &&
		echo new-line >>but-wild%percent &&
		but add but-wild* &&
		but cummit -m "modify the wildcard files" &&
		but config but-p4.skipSubmitEdit true &&
		but p4 submit
	) &&
	(
		cd "$cli" &&
		test_line_count = 2 but-wild#hash &&
		if test_have_prereq !MINGW,!CYGWIN
		then
			test_line_count = 2 but-wild\*star
		fi &&
		test_line_count = 2 but-wild@at &&
		test_line_count = 2 but-wild%percent
	)
'

test_expect_success 'wildcard files submit back to p4, copy' '
	test_when_finished cleanup_but &&
	but p4 clone --dest="$but" //depot &&
	(
		cd "$but" &&
		cp file2 but-wild-cp#hash &&
		but add but-wild-cp#hash &&
		cp but-wild#hash file-wild-3 &&
		but add file-wild-3 &&
		but cummit -m "wildcard copies" &&
		but config but-p4.detectCopies true &&
		but config but-p4.detectCopiesHarder true &&
		but config but-p4.skipSubmitEdit true &&
		but p4 submit
	) &&
	(
		cd "$cli" &&
		test_path_is_file but-wild-cp#hash &&
		test_path_is_file file-wild-3
	)
'

test_expect_success 'wildcard files submit back to p4, rename' '
	test_when_finished cleanup_but &&
	but p4 clone --dest="$but" //depot &&
	(
		cd "$but" &&
		but mv but-wild@at file-wild-4 &&
		but mv file-wild-3 but-wild-cp%percent &&
		but cummit -m "wildcard renames" &&
		but config but-p4.detectRenames true &&
		but config but-p4.skipSubmitEdit true &&
		but p4 submit
	) &&
	(
		cd "$cli" &&
		test_path_is_missing but-wild@at &&
		test_path_is_file but-wild-cp%percent
	)
'

test_expect_success 'wildcard files submit back to p4, delete' '
	test_when_finished cleanup_but &&
	but p4 clone --dest="$but" //depot &&
	(
		cd "$but" &&
		but rm but-wild* &&
		but cummit -m "delete the wildcard files" &&
		but config but-p4.skipSubmitEdit true &&
		but p4 submit
	) &&
	(
		cd "$cli" &&
		test_path_is_missing but-wild#hash &&
		if test_have_prereq !MINGW,!CYGWIN
		then
			test_path_is_missing but-wild\*star
		fi &&
		test_path_is_missing but-wild@at &&
		test_path_is_missing but-wild%percent
	)
'

test_expect_success 'p4 deleted a wildcard file' '
	(
		cd "$cli" &&
		echo "wild delete test" >wild@delete &&
		p4 add -f wild@delete &&
		p4 submit -d "add wild@delete"
	) &&
	test_when_finished cleanup_but &&
	but p4 clone --dest="$but" //depot &&
	(
		cd "$but" &&
		test_path_is_file wild@delete
	) &&
	(
		cd "$cli" &&
		# must use its encoded name
		p4 delete wild%40delete &&
		p4 submit -d "delete wild@delete"
	) &&
	(
		cd "$but" &&
		but p4 sync &&
		but merge --ff-only p4/master &&
		test_path_is_missing wild@delete
	)
'

test_expect_success 'wildcard files requiring keyword scrub' '
	(
		cd "$cli" &&
		cat <<-\EOF >scrub@wild &&
		$Id$
		line2
		EOF
		p4 add -t text+k -f scrub@wild &&
		p4 submit -d "scrub at wild"
	) &&
	test_when_finished cleanup_but &&
	but p4 clone --dest="$but" //depot &&
	(
		cd "$but" &&
		but config but-p4.skipSubmitEdit true &&
		but config but-p4.attemptRCSCleanup true &&
		sed "s/^line2/line2 edit/" <scrub@wild >scrub@wild.tmp &&
		mv -f scrub@wild.tmp scrub@wild &&
		but cummit -m "scrub at wild line2 edit" scrub@wild &&
		but p4 submit
	)
'

test_done
