#!/bin/sh

test_description='git p4 handling of UTF-16 files without BOM'

. ./lib-git-p4.sh

UTF16="\227\000\227\000"

test_expect_success 'start p4d' '
	start_p4d
'

test_expect_success 'init depot with UTF-16 encoded file and artificially remove BOM' '
	(
		cd "$cli" &&
		printf "$UTF16" >file1 &&
		p4 add -t utf16 file1 &&
		p4 submit -d "file1"
	) &&

	(
		cd db &&
		p4d -jc &&
		# P4D automatically adds a BOM. Remove it here to make the file invalid.
		sed -e "\$d" depot/file1,v >depot/file1,v.new &&
		mv depot/file1,v.new depot/file1,v &&
		printf "@$UTF16@" >>depot/file1,v &&
		p4d -jrF checkpoint.1
	)
'

test_expect_success 'clone depot with invalid UTF-16 file in verbose mode' '
	git p4 clone --dest="$git" --verbose //depot &&
	test_when_finished cleanup_git &&
	(
		cd "$git" &&
		printf "$UTF16" >expect &&
		test_cmp_bin expect file1
	)
'

test_expect_failure 'clone depot with invalid UTF-16 file in non-verbose mode' '
	git p4 clone --dest="$git" //depot
'

test_done
