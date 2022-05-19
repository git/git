#!/bin/sh
#
# Copyright (c) 2005 Junio C Hamano
#

test_description='but ls-files test (-- to terminate the path list).

This test runs but ls-files --others with the following on the
filesystem.

    path0       - a file
    -foo	- a file with a funny name.
    --		- another file with a funny name.
'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

test_expect_success \
	setup \
	'echo frotz >path0 &&
	echo frotz >./-foo &&
	echo frotz >./--'

test_expect_success \
    'but ls-files without path restriction.' \
    'but ls-files --others >output &&
     test_cmp output - <<EOF
--
-foo
output
path0
EOF
'

test_expect_success \
    'but ls-files with path restriction.' \
    'but ls-files --others path0 >output &&
	test_cmp output - <<EOF
path0
EOF
'

test_expect_success \
    'but ls-files with path restriction with --.' \
    'but ls-files --others -- path0 >output &&
	test_cmp output - <<EOF
path0
EOF
'

test_expect_success \
    'but ls-files with path restriction with -- --.' \
    'but ls-files --others -- -- >output &&
	test_cmp output - <<EOF
--
EOF
'

test_expect_success \
    'but ls-files with no path restriction.' \
    'but ls-files --others -- >output &&
	test_cmp output - <<EOF
--
-foo
output
path0
EOF
'

test_done
