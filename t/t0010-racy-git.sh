#!/bin/sh

test_description='racy GIT'

. ./test-lib.sh

# This test can give false success if your machine is sufficiently
# slow or your trial happened to happen on second boundary.

for trial in 0 1 2 3 4
do
	rm -f .git/index
	echo frotz >infocom
	git update-index --add infocom
	echo xyzzy >infocom

	files=`git diff-files -p`
	test_expect_success \
	"Racy GIT trial #$trial part A" \
	'test "" != "$files"'

	sleep 1
	echo xyzzy >cornerstone
	git update-index --add cornerstone

	files=`git diff-files -p`
	test_expect_success \
	"Racy GIT trial #$trial part B" \
	'test "" != "$files"'

done

test_done
