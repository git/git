#!/bin/sh

test_description='racy GIT'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

# This test can give false success if your machine is sufficiently
# slow or your trial happened to happen on second boundary.

for trial in 0 1 2 3 4
do
	rm -f .but/index
	echo frotz >infocom
	but update-index --add infocom
	echo xyzzy >infocom

	files=$(but diff-files -p)
	test_expect_success \
	"Racy GIT trial #$trial part A" \
	'test "" != "$files"'

	sleep 1
	echo xyzzy >cornerstone
	but update-index --add cornerstone

	files=$(but diff-files -p)
	test_expect_success \
	"Racy GIT trial #$trial part B" \
	'test "" != "$files"'

done

test_done
