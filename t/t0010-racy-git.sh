#!/bin/sh

test_description='racy GIT'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

# This test can give false success if your machine is sufficiently
# slow or your trial happened to happen on second boundary.

for trial in 0 1 2 3 4
do
	test_expect_success "Racy git trial #$trial part A" '
		rm -f .git/index &&
		echo frotz >infocom &&
		git update-index --add infocom &&
		echo xyzzy >infocom &&

		git diff-files -p >out &&
		test_file_not_empty out
	'
	sleep 1

	test_expect_success "Racy git trial #$trial part B" '
		echo xyzzy >cornerstone &&
		git update-index --add cornerstone &&

		git diff-files -p >out &&
		test_file_not_empty out
	'
done

test_done
