#!/bin/sh

test_description='test quickfetch from local'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

test_expect_success setup '

	test_tick &&
	echo ichi >file &&
	but add file &&
	but cummit -m initial &&

	cnt=$( (
		but count-objects | sed -e "s/ *objects,.*//"
	) ) &&
	test $cnt -eq 3
'

test_expect_success 'clone without alternate' '

	(
		mkdir cloned &&
		cd cloned &&
		but init-db &&
		but remote add -f origin ..
	) &&
	cnt=$( (
		cd cloned &&
		but count-objects | sed -e "s/ *objects,.*//"
	) ) &&
	test $cnt -eq 3
'

test_expect_success 'further cummits in the original' '

	test_tick &&
	echo ni >file &&
	but cummit -a -m second &&

	cnt=$( (
		but count-objects | sed -e "s/ *objects,.*//"
	) ) &&
	test $cnt -eq 6
'

test_expect_success 'copy cummit and tree but not blob by hand' '

	but rev-list --objects HEAD |
	but pack-objects --stdout |
	(
		cd cloned &&
		but unpack-objects
	) &&

	cnt=$( (
		cd cloned &&
		but count-objects | sed -e "s/ *objects,.*//"
	) ) &&
	test $cnt -eq 6 &&

	blob=$(but rev-parse HEAD:file | sed -e "s|..|&/|") &&
	test -f "cloned/.but/objects/$blob" &&
	rm -f "cloned/.but/objects/$blob" &&

	cnt=$( (
		cd cloned &&
		but count-objects | sed -e "s/ *objects,.*//"
	) ) &&
	test $cnt -eq 5

'

test_expect_success 'quickfetch should not leave a corrupted repository' '

	(
		cd cloned &&
		but fetch
	) &&

	cnt=$( (
		cd cloned &&
		but count-objects | sed -e "s/ *objects,.*//"
	) ) &&
	test $cnt -eq 6

'

test_expect_success 'quickfetch should not copy from alternate' '

	(
		mkdir quickclone &&
		cd quickclone &&
		but init-db &&
		(cd ../.but/objects && pwd) >.but/objects/info/alternates &&
		but remote add origin .. &&
		but fetch -k -k
	) &&
	obj_cnt=$( (
		cd quickclone &&
		but count-objects | sed -e "s/ *objects,.*//"
	) ) &&
	pck_cnt=$( (
		cd quickclone &&
		but count-objects -v | sed -n -e "/packs:/{
				s/packs://
				p
				q
			}"
	) ) &&
	origin_main=$( (
		cd quickclone &&
		but rev-parse origin/main
	) ) &&
	echo "loose objects: $obj_cnt, packfiles: $pck_cnt" &&
	test $obj_cnt -eq 0 &&
	test $pck_cnt -eq 0 &&
	test z$origin_main = z$(but rev-parse main)

'

test_expect_success 'quickfetch should handle ~1000 refs (on Windows)' '

	but gc &&
	head=$(but rev-parse HEAD) &&
	branchprefix="$head refs/heads/branch" &&
	for i in 0 1 2 3 4 5 6 7 8 9; do
		for j in 0 1 2 3 4 5 6 7 8 9; do
			for k in 0 1 2 3 4 5 6 7 8 9; do
				echo "$branchprefix$i$j$k" >> .but/packed-refs || return 1
			done
		done
	done &&
	(
		cd cloned &&
		but fetch &&
		but fetch
	)

'

test_done
