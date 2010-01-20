#!/bin/sh
#
# Copyright (c) 2010 Brad King
#

test_description='git update-index for gitlink to .git file.
'

. ./test-lib.sh

test_expect_success 'submodule with absolute .git file' '
	mkdir sub1 &&
	(cd sub1 &&
	 git init &&
	 REAL="$(pwd)/.real" &&
	 mv .git "$REAL"
	 echo "gitdir: $REAL" >.git &&
	 test_commit first)
'

test_expect_success 'add gitlink to absolute .git file' '
	git update-index --add -- sub1
'

test_expect_success 'submodule with relative .git file' '
	mkdir sub2 &&
	(cd sub2 &&
	 git init &&
	 mv .git .real &&
	 echo "gitdir: .real" >.git &&
	 test_commit first)
'

test_expect_success 'add gitlink to relative .git file' '
	git update-index --add -- sub2
'

test_done
