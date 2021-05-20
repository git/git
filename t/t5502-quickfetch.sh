#!/bin/sh

test_description='test quickfetch from local'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

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
	test $cnt -eq 6 &&

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

test_expect_success 'quickfetch should not copy from alternate' '

	(
		mkdir quickclone &&
		cd quickclone &&
		git init-db &&
		(cd ../.git/objects && pwd) >.git/objects/info/alternates &&
		git remote add origin .. &&
		git fetch -k -k
	) &&
	obj_cnt=$( (
		cd quickclone &&
		git count-objects | sed -e "s/ *objects,.*//"
	) ) &&
	pck_cnt=$( (
		cd quickclone &&
		git count-objects -v | sed -n -e "/packs:/{
				s/packs://
				p
				q
			}"
	) ) &&
	origin_main=$( (
		cd quickclone &&
		git rev-parse origin/main
	) ) &&
	echo "loose objects: $obj_cnt, packfiles: $pck_cnt" &&
	test $obj_cnt -eq 0 &&
	test $pck_cnt -eq 0 &&
	test z$origin_main = z$(git rev-parse main)

'

test_expect_success 'quickfetch should handle ~1000 refs (on Windows)' '

	git gc &&
	head=$(git rev-parse HEAD) &&
	branchprefix="$head refs/heads/branch" &&
	for i in 0 1 2 3 4 5 6 7 8 9; do
		for j in 0 1 2 3 4 5 6 7 8 9; do
			for k in 0 1 2 3 4 5 6 7 8 9; do
				echo "$branchprefix$i$j$k" >> .git/packed-refs
			done
		done
	done &&
	(
		cd cloned &&
		git fetch &&
		git fetch
	)

'

test_done
