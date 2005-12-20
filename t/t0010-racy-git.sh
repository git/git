#!/bin/sh

test_description='racy GIT'

. ./test-lib.sh

# This test can give false success if your machine is sufficiently
# slow or your trial happened to happen on second boundary.

for trial in 0 1 2 3 4 5 6 7 8 9
do
	rm -f .git/index
	echo frotz >infocom
	echo xyzzy >activision
	git update-index --add infocom activision
	echo xyzzy >infocom

	files=`git diff-files -p`
	test_expect_success \
	"Racy GIT trial #$trial" \
	'test "" != "$files"'
done

test_done
