#!/bin/sh

test_description='Test the output of the unit test framework'

. ./test-lib.sh

test_expect_success 'TAP output from unit tests' - <<\EOT
	cat >expect <<-EOF &&
	# BUG: check outside of test at t/helper/test-example-tap.c:75
	ok 1 - passing test
	ok 2 - passing test and assertion return 1
	# check "1 == 2" failed at t/helper/test-example-tap.c:79
	#    left: 1
	#   right: 2
	not ok 3 - failing test
	ok 4 - failing test and assertion return 0
	not ok 5 - passing TEST_TODO() # TODO
	ok 6 - passing TEST_TODO() returns 1
	# todo check 'check(x)' succeeded at t/helper/test-example-tap.c:26
	not ok 7 - failing TEST_TODO()
	ok 8 - failing TEST_TODO() returns 0
	# check "0" failed at t/helper/test-example-tap.c:31
	# skipping test - missing prerequisite
	# skipping check '1' at t/helper/test-example-tap.c:33
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
	# check "'a' == '\n'" failed at t/helper/test-example-tap.c:64
	#    left: 'a'
	#   right: '\012'
	# check "'\\\\' == '\\''" failed at t/helper/test-example-tap.c:65
	#    left: '\\\\'
	#   right: '\\''
	not ok 17 - messages from failing string and char comparison
	# BUG: test has no checks at t/helper/test-example-tap.c:94
	not ok 18 - test with no checks
	ok 19 - test with no checks returns 0
	ok 20 - if_test passing test
	# check "1 == 2" failed at t/helper/test-example-tap.c:100
	#    left: 1
	#   right: 2
	not ok 21 - if_test failing test
	not ok 22 - if_test passing TEST_TODO() # TODO
	# todo check 'check(1)' succeeded at t/helper/test-example-tap.c:104
	not ok 23 - if_test failing TEST_TODO()
	# check "0" failed at t/helper/test-example-tap.c:106
	# skipping test - missing prerequisite
	# skipping check '1' at t/helper/test-example-tap.c:108
	ok 24 - if_test test_skip() # SKIP
	# skipping test - missing prerequisite
	ok 25 - if_test test_skip() inside TEST_TODO() # SKIP
	# check "0" failed at t/helper/test-example-tap.c:113
	not ok 26 - if_test TEST_TODO() after failing check
	# check "0" failed at t/helper/test-example-tap.c:119
	not ok 27 - if_test failing check after TEST_TODO()
	# check "!strcmp("\thello\\\\", "there\"\n")" failed at t/helper/test-example-tap.c:122
	#    left: "\011hello\\\\"
	#   right: "there\"\012"
	# check "!strcmp("NULL", NULL)" failed at t/helper/test-example-tap.c:123
	#    left: "NULL"
	#   right: NULL
	# check "'a' == '\n'" failed at t/helper/test-example-tap.c:124
	#    left: 'a'
	#   right: '\012'
	# check "'\\\\' == '\\''" failed at t/helper/test-example-tap.c:125
	#    left: '\\\\'
	#   right: '\\''
	not ok 28 - if_test messages from failing string and char comparison
	# BUG: test has no checks at t/helper/test-example-tap.c:127
	not ok 29 - if_test test with no checks
	1..29
	EOF

	! test-tool example-tap >actual &&
	test_cmp expect actual
EOT

test_done
