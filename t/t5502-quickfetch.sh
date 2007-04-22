#!/bin/sh

test_description='test quickfetch from local'

. ./test-lib.sh

test_expect_success setup '

	test_tick &&
	echo ichi >file &&
	git add file &&
	git commit -m initial &&

	cnt=$( (
		git count-objects | sed -e "s/ *objects,.*//"
	) ) &&
	test $cnt -eq 3
'

test_expect_success 'clone without alternate' '

	(
		mkdir cloned &&
		cd cloned &&
		git init-db &&
		git remote add -f origin ..
	) &&
	cnt=$( (
		cd cloned &&
		git count-objects | sed -e "s/ *objects,.*//"
	) ) &&
	test $cnt -eq 3
'

test_expect_success 'further commits in the original' '

	test_tick &&
	echo ni >file &&
	git commit -a -m second &&

	cnt=$( (
		git count-objects | sed -e "s/ *objects,.*//"
	) ) &&
	test $cnt -eq 6
'

test_expect_success 'copy commit and tree but not blob by hand' '

	git rev-list --objects HEAD |
	git pack-objects --stdout |
	(
		cd cloned &&
		git unpack-objects
	) &&

	cnt=$( (
		cd cloned &&
		git count-objects | sed -e "s/ *objects,.*//"
	) ) &&
	test $cnt -eq 6

	blob=$(git rev-parse HEAD:file | sed -e "s|..|&/|") &&
	test -f "cloned/.git/objects/$blob" &&
	rm -f "cloned/.git/objects/$blob" &&

	cnt=$( (
		cd cloned &&
		git count-objects | sed -e "s/ *objects,.*//"
	) ) &&
	test $cnt -eq 5

'

test_expect_success 'quickfetch should not leave a corrupted repository' '

	(
		cd cloned &&
		git fetch
	) &&

	cnt=$( (
		cd cloned &&
		git count-objects | sed -e "s/ *objects,.*//"
	) ) &&
	test $cnt -eq 6

'

test_done
