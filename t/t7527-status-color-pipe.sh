#!/bin/sh

test_description='git status color option'

. ./test-lib.sh

test_expect_success setup '
	echo 1 >original &&
	git add .
'

# Normal git status does not pipe colors
test_expect_success 'git status' '
	git status >raw &&
	test_decode_color <raw >out &&
	grep "original$" out
'

# Test color.status=never (expect same as above)
test_expect_success 'git -c color.status=never status' '
	git -c color.status=never status >raw &&
	test_decode_color <raw >out &&
	grep "original$" out
'

# Test color.status=always
test_expect_success 'git -c color.status=always status' '
	git -c color.status=always status >raw &&
	test_decode_color <raw >out &&
	grep "original<RESET>$" out
'

# Test verbose (default)
test_expect_success 'git status -v' '
	git status -v >raw &&
	test_decode_color <raw >out &&
	grep "+1" out
'

# Test verbose color.status=never
test_expect_success 'git -c color.status=never status -v' '
	git -c color.status=never status -v >raw &&
	test_decode_color <raw >out &&
	grep "+1" out
'

# Test verbose color.status=always
test_expect_success 'git -c color.status=always status -v' '
	git -c color.status=always status -v >raw &&
	test_decode_color <raw >out &&
	grep "<CYAN>@@ -0,0 +1 @@<RESET>" out &&
	grep "GREEN>+<RESET><GREEN>1<RESET>" out
'

test_done
