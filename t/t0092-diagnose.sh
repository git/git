#!/bin/sh

test_description='git diagnose'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

test_expect_success UNZIP 'creates diagnostics zip archive' '
	test_when_finished rm -rf report &&

	git diagnose -o report -s test >out &&
	grep "Available space" out &&

	zip_path=report/git-diagnostics-test.zip &&
	test_path_is_file "$zip_path" &&

	# Check zipped archive content
	"$GIT_UNZIP" -p "$zip_path" diagnostics.log >out &&
	test_file_not_empty out &&

	"$GIT_UNZIP" -p "$zip_path" packs-local.txt >out &&
	grep ".git/objects" out &&

	"$GIT_UNZIP" -p "$zip_path" objects-local.txt >out &&
	grep "^Total: [0-9][0-9]*" out &&

	# Should not include .git directory contents by default
	! "$GIT_UNZIP" -l "$zip_path" | grep ".git/"
	grep "^Total: [0-9][0-9]*" out
'

test_done
