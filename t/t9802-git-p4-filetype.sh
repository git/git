#!/bin/sh

test_description='git-p4 p4 filetype tests'

. ./lib-git-p4.sh

test_expect_success 'start p4d' '
	start_p4d
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
	"$GITP4" clone --dest="$git" //depot@all &&
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
	"$GITP4" clone --dest="$git" //depot@all &&
	(
		cd "$git" &&

		# text, ensure unexpanded
		"$PYTHON_PATH" "$TRASH_DIRECTORY/k_smush.py" <"$cli/k-text-k" >cli-k-text-k-smush &&
		test_cmp cli-k-text-k-smush k-text-k &&
		"$PYTHON_PATH" "$TRASH_DIRECTORY/ko_smush.py" <"$cli/k-text-ko" >cli-k-text-ko-smush &&
		test_cmp cli-k-text-ko-smush k-text-ko &&

		# utf16, even though p4 expands keywords, git-p4 does not
		# try to undo that
		test_cmp "$cli/k-utf16-k" k-utf16-k &&
		test_cmp "$cli/k-utf16-ko" k-utf16-ko
	)
'

test_expect_success 'kill p4d' '
	kill_p4d
'

test_done
