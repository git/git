#!/bin/sh

test_description='Test different reference backend URIs'

. ./test-lib.sh

test_expect_success 'empty uri provided' '
	test_when_finished "rm -rf repo" &&
	git init --ref-format=files repo &&
	(
		cd repo &&
		GIT_REF_URI="" &&
		export GIT_REF_URI &&
		test_must_fail git refs list 2>err &&
		test_grep "invalid reference backend uri format" err
	)
'

test_expect_success 'invalid uri provided' '
	test_when_finished "rm -rf repo" &&
	git init --ref-format=files repo &&
	(
		cd repo &&
		GIT_REF_URI="reftable@/home/reftable" &&
		export GIT_REF_URI &&
		test_must_fail git refs list 2>err &&
		test_grep "invalid reference backend uri format" err
	)
'

test_expect_success 'empty path in uri' '
	test_when_finished "rm -rf repo" &&
	git init --ref-format=files repo &&
	(
		cd repo &&
		GIT_REF_URI="reftable://" &&
		export GIT_REF_URI &&
		test_must_fail git refs list 2>err &&
		test_grep "invalid path in uri" err
	)
'

test_expect_success 'uri ends at colon' '
	test_when_finished "rm -rf repo" &&
	git init --ref-format=files repo &&
	(
		cd repo &&
		GIT_REF_URI="reftable:" &&
		export GIT_REF_URI &&
		test_must_fail git refs list 2>err &&
		test_grep "invalid reference backend uri format" err
	)
'

test_expect_success 'unknown reference backend' '
	test_when_finished "rm -rf repo" &&
	git init --ref-format=files repo &&
	(
		cd repo &&
		GIT_REF_URI="db://.git" &&
		export GIT_REF_URI &&
		test_must_fail git refs list 2>err &&
		test_grep "unknown reference backend" err
	)
'

ref_formats="files reftable"
for from_format in $ref_formats
do
	for to_format in $ref_formats
	do
		if test "$from_format" = "$to_format"
		then
			continue
		fi

		test_expect_success "read from $to_format backend" '
			test_when_finished "rm -rf repo" &&
			git init --ref-format=$from_format repo &&
			(
				cd repo &&
				test_commit 1 &&
				test_commit 2 &&
				test_commit 3 &&

				git refs migrate --dry-run --ref-format=$to_format >out &&
				BACKEND_PATH=$(cat out | sed "s/.* ${SQ}\(.*\)${SQ}/\1/") &&
				git refs list >expect &&
				GIT_REF_URI="$to_format://$BACKEND_PATH" git refs list >actual &&
				test_cmp expect actual
			)
		'

		test_expect_success "write to $to_format backend" '
			test_when_finished "rm -rf repo" &&
			git init --ref-format=$from_format repo &&
			(
				cd repo &&
				test_commit 1 &&
				test_commit 2 &&
				test_commit 3 &&

				git refs migrate --dry-run --ref-format=$to_format >out &&
				git refs list >expect &&

				BACKEND_PATH=$(cat out | sed "s/.* ${SQ}\(.*\)${SQ}/\1/") &&
				GIT_REF_URI="$to_format://$BACKEND_PATH" git tag -d 1 &&

				git refs list >actual &&
				test_cmp expect actual &&

				GIT_REF_URI="$to_format://$BACKEND_PATH" git refs list >expect &&
				git refs list >out &&
				cat out | grep -v "refs/tags/1" >actual &&
				test_cmp expect actual
			)
		'
	done
done

test_done
