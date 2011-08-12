#!/bin/sh

test_description="recursive merge corner cases w/ renames but not criss-crosses"
# t6036 has corner cases that involve both criss-cross merges and renames

. ./test-lib.sh

test_expect_success 'setup rename/delete + untracked file' '
	echo "A pretty inscription" >ring &&
	git add ring &&
	test_tick &&
	git commit -m beginning &&

	git branch people &&
	git checkout -b rename-the-ring &&
	git mv ring one-ring-to-rule-them-all &&
	test_tick &&
	git commit -m fullname &&

	git checkout people &&
	git rm ring &&
	echo gollum >owner &&
	git add owner &&
	test_tick &&
	git commit -m track-people-instead-of-objects &&
	echo "Myyy PRECIOUSSS" >ring
'

test_expect_failure "Does git preserve Gollum's precious artifact?" '
	test_must_fail git merge -s recursive rename-the-ring &&

	# Make sure git did not delete an untracked file
	test -f ring
'

test_done
