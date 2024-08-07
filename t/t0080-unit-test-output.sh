#!/bin/sh

test_description='Test the output of the unit test framework'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

test_expect_success 'TAP output from unit tests' '
	cat >expect <<-EOF &&
	ok 1 - passing test
	ok 2 - passing test and assertion return 1
	# check "1 == 2" failed at t/helper/test-example-tap.c:77
	#    left: 1
	#   right: 2
	not ok 3 - failing test
	ok 4 - failing test and assertion return 0
	not ok 5 - passing TEST_TODO() # TODO
	ok 6 - passing TEST_TODO() returns 1
	# todo check ${SQ}check(x)${SQ} succeeded at t/helper/test-example-tap.c:26
	not ok 7 - failing TEST_TODO()
	ok 8 - failing TEST_TODO() returns 0
	# check "0" failed at t/helper/test-example-tap.c:31
	# skipping test - missing prerequisite
	# skipping check ${SQ}1${SQ} at t/helper/test-example-tap.c:33
	ok 9 - test_skip() # SKIP
	ok 10 - skipped test returns 1
	# skipping test - missing prerequisite
	ok 11 - test_skip() inside TEST_TODO() # SKIP
	ok 12 - test_skip() inside TEST_TODO() returns 1
	# check "0" failed at t/helper/test-example-tap.c:49
	not ok 13 - TEST_TODO() after failing check
	ok 14 - TEST_TODO() after failing check returns 0
	# check "0" failed at t/helper/test-example-tap.c:57
	not ok 15 - failing check after TEST_TODO()
	ok 16 - failing check after TEST_TODO() returns 0
	# check "!strcmp("\thello\\\\", "there\"\n")" failed at t/helper/test-example-tap.c:62
	#    left: "\011hello\\\\"
	#   right: "there\"\012"
	# check "!strcmp("NULL", NULL)" failed at t/helper/test-example-tap.c:63
	#    left: "NULL"
	#   right: NULL
	# check "${SQ}a${SQ} == ${SQ}\n${SQ}" failed at t/helper/test-example-tap.c:64
	#    left: ${SQ}a${SQ}
	#   right: ${SQ}\012${SQ}
	# check "${SQ}\\\\${SQ} == ${SQ}\\${SQ}${SQ}" failed at t/helper/test-example-tap.c:65
	#    left: ${SQ}\\\\${SQ}
	#   right: ${SQ}\\${SQ}${SQ}
	not ok 17 - messages from failing string and char comparison
	# BUG: test has no checks at t/helper/test-example-tap.c:92
	not ok 18 - test with no checks
	ok 19 - test with no checks returns 0
	1..19
	EOF

	! test-tool example-tap >actual &&
	test_cmp expect actual
'

test_done
