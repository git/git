#!/bin/sh
#
# Copyright (c) 2012 Heiko Voigt
#

test_description='Test revision walking api'

. ./test-lib.sh

cat >run_twice_expected <<-EOF
1st
 > add b
 > add a
2nd
 > add b
 > add a
EOF

test_expect_success 'setup' '
	echo a > a &&
	but add a &&
	but cummit -m "add a" &&
	echo b > b &&
	but add b &&
	but cummit -m "add b"
'

test_expect_success 'revision walking can be done twice' '
	test-tool revision-walking run-twice >run_twice_actual &&
	test_cmp run_twice_expected run_twice_actual
'

test_done
