#!/bin/sh
#
# Copyright (c) 2020 Google LLC
#

test_description='reftable unittests'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

test_expect_success 'unittests' '
	TMPDIR=$(pwd) && export TMPDIR &&
	test-tool reftable
'

test_done
