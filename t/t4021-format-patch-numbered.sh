#!/bin/sh
#
# Copyright (c) 2006 Brian C Gernhardt
#

test_description='Format-patch numbering options'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

test_expect_success setup '

	echo A > file &&
	git add file &&
	git commit -m First &&

	echo B >> file &&
	git commit -a -m Second &&

	echo C >> file &&
	git commit -a -m Third

'

# Each of these gets used multiple times.

test_num_no_numbered() {
	cnt=$(grep "^Subject: \[PATCH\]" $1 | wc -l) &&
	test $cnt = $2
}

test_single_no_numbered() {
	test_num_no_numbered $1 1
}

test_no_numbered() {
	test_num_no_numbered $1 2
}

test_single_cover_letter_numbered() {
	grep "^Subject: \[PATCH 0/1\]" $1 &&
	grep "^Subject: \[PATCH 1/1\]" $1
}

test_single_numbered() {
	grep "^Subject: \[PATCH 1/1\]" $1
}

test_numbered() {
	grep "^Subject: \[PATCH 1/2\]" $1 &&
	grep "^Subject: \[PATCH 2/2\]" $1
}

test_expect_success 'single patch defaults to no numbers' '
	git format-patch --stdout HEAD~1 >patch0.single &&
	test_single_no_numbered patch0.single
'

test_expect_success 'multiple patch defaults to numbered' '

	git format-patch --stdout HEAD~2 >patch0.multiple &&
	test_numbered patch0.multiple

'

test_expect_success 'Use --numbered' '

	git format-patch --numbered --stdout HEAD~1 >patch1 &&
	test_single_numbered patch1

'

test_expect_success 'format.numbered = true' '

	git config format.numbered true &&
	git format-patch --stdout HEAD~2 >patch2 &&
	test_numbered patch2

'

test_expect_success 'format.numbered && single patch' '

	git format-patch --stdout HEAD^ > patch3 &&
	test_single_numbered patch3

'

test_expect_success 'format.numbered && --no-numbered' '

	git format-patch --no-numbered --stdout HEAD~2 >patch4 &&
	test_no_numbered patch4

'

test_expect_success 'format.numbered && --keep-subject' '

	git format-patch --keep-subject --stdout HEAD^ >patch4a &&
	grep "^Subject: Third" patch4a

'

test_expect_success 'format.numbered = auto' '

	git config format.numbered auto &&
	git format-patch --stdout HEAD~2 > patch5 &&
	test_numbered patch5

'

test_expect_success 'format.numbered = auto && single patch' '

	git format-patch --stdout HEAD^ > patch6 &&
	test_single_no_numbered patch6

'

test_expect_success 'format.numbered = auto && --no-numbered' '

	git format-patch --no-numbered --stdout HEAD~2 > patch7 &&
	test_no_numbered patch7

'

test_expect_success '--start-number && --numbered' '

	git format-patch --start-number 3 --numbered --stdout HEAD~1 > patch8 &&
	grep "^Subject: \[PATCH 3/3\]" patch8
'

test_expect_success 'single patch with cover-letter defaults to numbers' '
	git format-patch --cover-letter --stdout HEAD~1 >patch9.single &&
	test_single_cover_letter_numbered patch9.single
'

test_expect_success 'Use --no-numbered and --cover-letter single patch' '
	git format-patch --no-numbered --stdout --cover-letter HEAD~1 >patch10 &&
	test_no_numbered patch10
'



test_done
