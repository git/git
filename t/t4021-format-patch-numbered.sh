#!/bin/sh
#
# Copyright (c) 2006 Brian C Gernhardt
#

test_description='Format-patch numbering options'

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

test_single_numbered() {
	grep "^Subject: \[PATCH 1/1\]" $1
}

test_numbered() {
	grep "^Subject: \[PATCH 1/2\]" $1 &&
	grep "^Subject: \[PATCH 2/2\]" $1
}

test_expect_success 'Default: no numbered' '

	git format-patch --stdout HEAD~2 >patch0 &&
	test_no_numbered patch0

'

test_expect_success 'Use --numbered' '

	git format-patch --numbered --stdout HEAD~2 >patch1 &&
	test_numbered patch1

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

test_expect_success 'format.numbered = auto' '

	git config format.numbered auto
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

test_done
