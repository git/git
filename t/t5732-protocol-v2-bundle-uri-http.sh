#!/bin/sh

test_description="Test bundle-uri with protocol v2 and 'http://' transport"

TEST_NO_CREATE_REPO=1

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

# Test protocol v2 with 'http://' transport
#
BUNDLE_URI_PROTOCOL=http
. "$TEST_DIRECTORY"/lib-bundle-uri-protocol.sh

test_done
