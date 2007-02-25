#!/bin/sh
#
# Copyright (c) 2005 Junio C Hamano
#

test_description='git-ls-files test (-- to terminate the path list).

This test runs git-ls-files --others with the following on the
filesystem.

    path0       - a file
    -foo	- a file with a funny name.
    --		- another file with a funny name.
'
. ./test-lib.sh

test_expect_success \
	setup \
	'echo frotz >path0 &&
	echo frotz >./-foo &&
	echo frotz >./--'

test_expect_success \
    'git-ls-files without path restriction.' \
    'git-ls-files --others >output &&
     git diff output - <<EOF
--
-foo
output
path0
EOF
'

test_expect_success \
    'git-ls-files with path restriction.' \
    'git-ls-files --others path0 >output &&
	git diff output - <<EOF
path0
EOF
'

test_expect_success \
    'git-ls-files with path restriction with --.' \
    'git-ls-files --others -- path0 >output &&
	git diff output - <<EOF
path0
EOF
'

test_expect_success \
    'git-ls-files with path restriction with -- --.' \
    'git-ls-files --others -- -- >output &&
	git diff output - <<EOF
--
EOF
'

test_expect_success \
    'git-ls-files with no path restriction.' \
    'git-ls-files --others -- >output &&
	git diff output - <<EOF
--
-foo
output
path0
EOF
'

test_done
