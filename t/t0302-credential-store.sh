#!/bin/sh

test_description='credential-store tests'
. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-credential.sh

helper_test store

test_done
