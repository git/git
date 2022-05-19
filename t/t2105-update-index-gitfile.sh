#!/bin/sh
#
# Copyright (c) 2010 Brad King
#

test_description='but update-index for butlink to .but file.
'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

test_expect_success 'submodule with absolute .but file' '
	mkdir sub1 &&
	(cd sub1 &&
	 but init &&
	 REAL="$(pwd)/.real" &&
	 mv .but "$REAL" &&
	 echo "butdir: $REAL" >.but &&
	 test_cummit first)
'

test_expect_success 'add butlink to absolute .but file' '
	but update-index --add -- sub1
'

test_expect_success 'submodule with relative .but file' '
	mkdir sub2 &&
	(cd sub2 &&
	 but init &&
	 mv .but .real &&
	 echo "butdir: .real" >.but &&
	 test_cummit first)
'

test_expect_success 'add butlink to relative .but file' '
	but update-index --add -- sub2
'

test_done
