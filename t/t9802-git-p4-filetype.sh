#!/bin/sh

test_description='git p4 filetype tests'

. ./lib-git-p4.sh

test_expect_success 'start p4d' '
	start_p4d
'

#
# This series of tests checks newline handling  Both p4 and
# git store newlines as \n, and have options to choose how
# newlines appear in checked-out files.
#
test_expect_success 'p4 client newlines, unix' '
	(
		cd "$cli" &&
		p4 client -o | sed "/LineEnd/s/:.*/:unix/" | p4 client -i &&
		printf "unix\ncrlf\n" >f-unix &&
		printf "unix\r\ncrlf\r\n" >f-unix-as-crlf &&
		p4 add -t text f-unix &&
		p4 submit -d f-unix &&

		# LineEnd: unix; should be no change after sync
		cp f-unix f-unix-orig &&
		p4 sync -f &&
		test_cmp f-unix-orig f-unix &&

		# make sure stored in repo as unix newlines
		# use sed to eat python-appended newline
		p4 -G print //depot/f-unix | marshal_dump data 2 |\
		    sed \$d >f-unix-p4-print &&
		test_cmp f-unix-orig f-unix-p4-print &&

		# switch to win, make sure lf -> crlf
		p4 client -o | sed "/LineEnd/s/:.*/:win/" | p4 client -i &&
		p4 sync -f &&
		test_cmp f-unix-as-crlf f-unix
	)
'

test_expect_success 'p4 client newlines, win' '
	(
		cd "$cli" &&
		p4 client -o | sed "/LineEnd/s/:.*/:win/" | p4 client -i &&
		printf "win\r\ncrlf\r\n" >f-win &&
		printf "win\ncrlf\n" >f-win-as-lf &&
		p4 add -t text f-win &&
		p4 submit -d f-win &&

		# LineEnd: win; should be no change after sync
		cp f-win f-win-orig &&
		p4 sync -f &&
		test_cmp f-win-orig f-win &&

		# make sure stored in repo as unix newlines
		# use sed to eat python-appened newline
		p4 -G print //depot/f-win | marshal_dump data 2 |\
		    sed \$d >f-win-p4-print &&
		test_cmp f-win-as-lf f-win-p4-print &&

		# switch to unix, make sure lf -> crlf
		p4 client -o | sed "/LineEnd/s/:.*/:unix/" | p4 client -i &&
		p4 sync -f &&
		test_cmp f-win-as-lf f-win
	)
'

test_expect_success 'ensure blobs store only lf newlines' '
	test_when_finished cleanup_git &&
	(
		cd "$git" &&
		git init &&
		git p4 sync //depot@all &&

		# verify the files in .git are stored only with newlines
		o=$(git ls-tree p4/master -- f-unix | cut -f1 | cut -d\  -f3) &&
		git cat-file blob $o >f-unix-blob &&
		test_cmp "$cli"/f-unix-orig f-unix-blob &&

		o=$(git ls-tree p4/master -- f-win | cut -f1 | cut -d\  -f3) &&
		git cat-file blob $o >f-win-blob &&
		test_cmp "$cli"/f-win-as-lf f-win-blob &&

		rm f-unix-blob f-win-blob
	)
'

test_expect_success 'gitattributes setting eol=lf produces lf newlines' '
	test_when_finished cleanup_git &&
	(
		# checkout the files and make sure core.eol works as planned
		cd "$git" &&
		git init &&
		echo "* eol=lf" >.gitattributes &&
		git p4 sync //depot@all &&
		git checkout -b master p4/master &&
		test_cmp "$cli"/f-unix-orig f-unix &&
		test_cmp "$cli"/f-win-as-lf f-win
	)
'

test_expect_success 'gitattributes setting eol=crlf produces crlf newlines' '
	test_when_finished cleanup_git &&
	(
		# checkout the files and make sure core.eol works as planned
		cd "$git" &&
		git init &&
		echo "* eol=crlf" >.gitattributes &&
		git p4 sync //depot@all &&
		git checkout -b master p4/master &&
		test_cmp "$cli"/f-unix-as-crlf f-unix &&
		test_cmp "$cli"/f-win-orig f-win
	)
'

test_expect_success 'crlf cleanup' '
	(
		cd "$cli" &&
		rm f-unix-orig f-unix-as-crlf &&
		rm f-win-orig f-win-as-lf &&
		p4 client -o | sed "/LineEnd/s/:.*/:unix/" | p4 client -i &&
		p4 sync -f
	)
'

test_expect_success 'utf-16 file create' '
	(
		cd "$cli" &&

		# p4 saves this verbatim
		printf "three\nline\ntext\n" >f-ascii &&
		p4 add -t text f-ascii &&

		# p4 adds \377\376 header
		cp f-ascii f-ascii-as-utf16 &&
		p4 add -t utf16 f-ascii-as-utf16 &&

		# p4 saves this exactly as iconv produced it
		printf "three\nline\ntext\n" | iconv -f ascii -t utf-16 >f-utf16 &&
		p4 add -t utf16 f-utf16 &&

		# this also is unchanged
		cp f-utf16 f-utf16-as-text &&
		p4 add -t text f-utf16-as-text &&

		p4 submit -d "f files" &&

		# force update of client files
		p4 sync -f
	)
'

test_expect_success 'utf-16 file test' '
	test_when_finished cleanup_git &&
	git p4 clone --dest="$git" //depot@all &&
	(
		cd "$git" &&

		test_cmp "$cli/f-ascii" f-ascii &&
		test_cmp "$cli/f-ascii-as-utf16" f-ascii-as-utf16 &&
		test_cmp "$cli/f-utf16" f-utf16 &&
		test_cmp "$cli/f-utf16-as-text" f-utf16-as-text
	)
'

test_expect_success 'keyword file create' '
	(
		cd "$cli" &&

		printf "id\n\$Id\$\n\$Author\$\ntext\n" >k-text-k &&
		p4 add -t text+k k-text-k &&

		cp k-text-k k-text-ko &&
		p4 add -t text+ko k-text-ko &&

		cat k-text-k | iconv -f ascii -t utf-16 >k-utf16-k &&
		p4 add -t utf16+k k-utf16-k &&

		cp k-utf16-k k-utf16-ko &&
		p4 add -t utf16+ko k-utf16-ko &&

		p4 submit -d "k files" &&
		p4 sync -f
	)
'

build_smush() {
	cat >k_smush.py <<-\EOF &&
	import re, sys
	sys.stdout.write(re.sub(r'(?i)\$(Id|Header|Author|Date|DateTime|Change|File|Revision):[^$]*\$', r'$\1$', sys.stdin.read()))
	EOF
	cat >ko_smush.py <<-\EOF
	import re, sys
	sys.stdout.write(re.sub(r'(?i)\$(Id|Header):[^$]*\$', r'$\1$', sys.stdin.read()))
	EOF
}

test_expect_success 'keyword file test' '
	build_smush &&
	test_when_finished rm -f k_smush.py ko_smush.py &&
	test_when_finished cleanup_git &&
	git p4 clone --dest="$git" //depot@all &&
	(
		cd "$git" &&

		# text, ensure unexpanded
		"$PYTHON_PATH" "$TRASH_DIRECTORY/k_smush.py" <"$cli/k-text-k" >cli-k-text-k-smush &&
		test_cmp cli-k-text-k-smush k-text-k &&
		"$PYTHON_PATH" "$TRASH_DIRECTORY/ko_smush.py" <"$cli/k-text-ko" >cli-k-text-ko-smush &&
		test_cmp cli-k-text-ko-smush k-text-ko &&

		# utf16, even though p4 expands keywords, git p4 does not
		# try to undo that
		test_cmp "$cli/k-utf16-k" k-utf16-k &&
		test_cmp "$cli/k-utf16-ko" k-utf16-ko
	)
'

build_gendouble() {
	cat >gendouble.py <<-\EOF
	import sys
	import struct

	s = struct.pack(b">LL18s",
			0x00051607,  # AppleDouble
			0x00020000,  # version 2
			b""          # pad to 26 bytes
	)
	getattr(sys.stdout, 'buffer', sys.stdout).write(s)
	EOF
}

test_expect_success 'ignore apple' '
	test_when_finished rm -f gendouble.py &&
	build_gendouble &&
	(
		cd "$cli" &&
		test-tool genrandom apple 1024 >double.png &&
		"$PYTHON_PATH" "$TRASH_DIRECTORY/gendouble.py" >%double.png &&
		p4 add -t apple double.png &&
		p4 submit -d appledouble
	) &&
	test_when_finished cleanup_git &&
	git p4 clone --dest="$git" //depot@all &&
	(
		cd "$git" &&
		test ! -f double.png
	)
'

test_expect_success SYMLINKS 'create p4 symlink' '
	cd "$cli" &&
	ln -s symlink-target symlink &&
	p4 add symlink &&
	p4 submit -d "add symlink"
'

test_expect_success SYMLINKS 'ensure p4 symlink parsed correctly' '
	test_when_finished cleanup_git &&
	git p4 clone --dest="$git" //depot@all &&
	(
		cd "$git" &&
		test -L symlink &&
		test $(test_readlink symlink) = symlink-target
	)
'

test_expect_success SYMLINKS 'empty symlink target' '
	(
		# first create the file as a file
		cd "$cli" &&
		>empty-symlink &&
		p4 add empty-symlink &&
		p4 submit -d "add empty-symlink as a file"
	) &&
	(
		# now change it to be a symlink to "target1"
		cd "$cli" &&
		p4 edit empty-symlink &&
		p4 reopen -t symlink empty-symlink &&
		rm empty-symlink &&
		ln -s target1 empty-symlink &&
		p4 add empty-symlink &&
		p4 submit -d "make empty-symlink point to target1"
	) &&
	(
		# Hack the p4 depot to make the symlink point to nothing;
		# this should not happen in reality, but shows up
		# in p4 repos in the wild.
		#
		# The sed expression changes this:
		#     @@
		#     text
		#     @target1
		#     @
		# to this:
		#     @@
		#     text
		#     @@
		#
		cd "$db/depot" &&
		sed "/@target1/{; s/target1/@/; n; d; }" \
		    empty-symlink,v >empty-symlink,v.tmp &&
		mv empty-symlink,v.tmp empty-symlink,v
	) &&
	(
		# Make sure symlink really is empty.  Asking
		# p4 to sync here will make it generate errors.
		cd "$cli" &&
		p4 print -q //depot/empty-symlink#2 >out &&
		test_must_be_empty out
	) &&
	test_when_finished cleanup_git &&

	# make sure git p4 handles it without error
	git p4 clone --dest="$git" //depot@all &&

	# fix the symlink, make it point to "target2"
	(
		cd "$cli" &&
		p4 open empty-symlink &&
		rm empty-symlink &&
		ln -s target2 empty-symlink &&
		p4 submit -d "make empty-symlink point to target2"
	) &&
	cleanup_git &&
	git p4 clone --dest="$git" //depot@all &&
	(
		cd "$git" &&
		test $(test_readlink empty-symlink) = target2
	)
'

test_expect_success SYMLINKS 'utf-8 with and without BOM in text file' '
	(
		cd "$cli" &&

		# some utf8 content
		echo some tǣxt >utf8-nobom-test &&

		# same utf8 content as before but with bom
		echo some tǣxt | sed '\''s/^/\xef\xbb\xbf/'\'' >utf8-bom-test &&

		# bom only
		dd bs=1 count=3 if=utf8-bom-test of=utf8-bom-empty-test &&

		p4 add utf8-nobom-test utf8-bom-test utf8-bom-empty-test &&
		p4 submit -d "add utf8 test files"
	) &&
	test_when_finished cleanup_git &&

	git p4 clone --dest="$git" //depot@all &&
	(
		cd "$git" &&
		git checkout refs/remotes/p4/master &&

		echo some tǣxt >utf8-nobom-check &&
		test_cmp utf8-nobom-check utf8-nobom-test &&

		echo some tǣxt | sed '\''s/^/\xef\xbb\xbf/'\'' >utf8-bom-check &&
		test_cmp utf8-bom-check utf8-bom-test &&

		dd bs=1 count=3 if=utf8-bom-check of=utf8-bom-empty-check &&
		test_cmp utf8-bom-empty-check utf8-bom-empty-test
	)
'

test_done
