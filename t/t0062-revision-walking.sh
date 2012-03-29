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
	git add a &&
	git commit -m "add a" &&
	echo b > b &&
	git add b &&
	git commit -m "add b"
'

test_expect_success 'revision walking can be done twice' '
	test-revision-walking run-twice > run_twice_actual
	test_cmp run_twice_expected run_twice_actual
'

test_done
