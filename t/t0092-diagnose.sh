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
'

test_expect_success UNZIP 'counts loose objects' '
	test_commit A &&

	# After committing, should have non-zero loose objects
	git diagnose -o test-count -s 1 >out &&
	zip_path=test-count/git-diagnostics-1.zip &&
	"$GIT_UNZIP" -p "$zip_path" objects-local.txt >out &&
	grep "^Total: [1-9][0-9]* loose objects" out
'

test_expect_success UNZIP '--mode=stats excludes .git dir contents' '
	test_when_finished rm -rf report &&

	git diagnose -o report -s test --mode=stats >out &&

	# Includes pack quantity/size info
	zip_path=report/git-diagnostics-test.zip &&
	"$GIT_UNZIP" -p "$zip_path" packs-local.txt >out &&
	grep ".git/objects" out &&

	# Does not include .git directory contents
	! "$GIT_UNZIP" -l "$zip_path" | grep ".git/"
'

test_expect_success UNZIP '--mode=all includes .git dir contents' '
	test_when_finished rm -rf report &&

	git diagnose -o report -s test --mode=all >out &&

	# Includes pack quantity/size info
	zip_path=report/git-diagnostics-test.zip &&
	"$GIT_UNZIP" -p "$zip_path" packs-local.txt >out &&
	grep ".git/objects" out &&

	# Includes .git directory contents
	"$GIT_UNZIP" -l "$zip_path" | grep ".git/" &&

	"$GIT_UNZIP" -p "$zip_path" .git/HEAD >out &&
	test_file_not_empty out
'

test_done
