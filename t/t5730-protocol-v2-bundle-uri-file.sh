#!/bin/sh

test_description="Test bundle-uri with protocol v2 and 'file://' transport"

TEST_NO_CREATE_REPO=1

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

# Test protocol v2 with 'file://' transport
#
T5730_PROTOCOL=file
. "$TEST_DIRECTORY"/lib-t5730-protocol-v2-bundle-uri.sh

test_expect_success "unknown capability value with $T5730_PROTOCOL:// using protocol v2" '
	test_when_finished "rm -f log" &&

	test_config -C "$T5730_PARENT" \
		uploadpack.bundleURI "$T5730_BUNDLE_URI_ESCAPED" &&

	GIT_TRACE_PACKET="$PWD/log" \
	GIT_TEST_BUNDLE_URI_UNKNOWN_CAPABILITY_VALUE=true \
	git \
		-c protocol.version=2 \
		ls-remote --symref "$T5730_URI" \
		>actual 2>err &&

	# Server responded using protocol v2
	grep "< version 2" log &&

	grep "> bundle-uri=test-unknown-capability-value" log
'

test_done
