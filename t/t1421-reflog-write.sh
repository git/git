#!/bin/sh

test_description='Manually write reflog entries'

. ./test-lib.sh

SIGNATURE="C O Mitter <committer@example.com> 1112911993 -0700"

test_reflog_matches () {
	repo="$1" &&
	refname="$2" &&
	cat >actual &&
	test-tool -C "$repo" ref-store main for-each-reflog-ent "$refname" >expected &&
	test_cmp expected actual
}

test_expect_success 'invalid number of arguments' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&
		for args in "" "1" "1 2" "1 2 3" "1 2 3 4 5"
		do
			test_must_fail git reflog write $args 2>err &&
			test_grep "usage: git reflog write" err || return 1
		done
	)
'

test_expect_success 'invalid refname' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&
		test_must_fail git reflog write "refs/heads/ invalid" $ZERO_OID $ZERO_OID first 2>err &&
		test_grep "invalid reference name: " err
	)
'

test_expect_success 'unqualified refname is rejected' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&
		test_must_fail git reflog write unqualified $ZERO_OID $ZERO_OID first 2>err &&
		test_grep "invalid reference name: " err
	)
'

test_expect_success 'nonexistent object IDs' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&
		test_must_fail git reflog write refs/heads/something $(test_oid deadbeef) $ZERO_OID old-object-id 2>err &&
		test_grep "old object .* does not exist" err &&
		test_must_fail git reflog write refs/heads/something $ZERO_OID $(test_oid deadbeef) new-object-id 2>err &&
		test_grep "new object .* does not exist" err
	)
'

test_expect_success 'abbreviated object IDs' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&
		test_commit initial &&
		abbreviated_oid=$(git rev-parse HEAD | test_copy_bytes 8) &&
		test_must_fail git reflog write refs/heads/something $abbreviated_oid $ZERO_OID old-object-id 2>err &&
		test_grep "invalid old object ID" err &&
		test_must_fail git reflog write refs/heads/something $ZERO_OID $abbreviated_oid new-object-id 2>err &&
		test_grep "invalid new object ID" err
	)
'

test_expect_success 'reflog message gets normalized' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&
		test_commit initial &&
		COMMIT_OID=$(git rev-parse HEAD) &&
		git reflog write HEAD $COMMIT_OID $COMMIT_OID "$(printf "message\nwith\nnewlines")" &&
		git reflog show -1 --format=%gs HEAD >actual &&
		echo "message with newlines" >expected &&
		test_cmp expected actual
	)
'

test_expect_success 'simple writes' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&
		test_commit initial &&
		COMMIT_OID=$(git rev-parse HEAD) &&

		git reflog write refs/heads/something $ZERO_OID $COMMIT_OID first &&
		test_reflog_matches . refs/heads/something <<-EOF &&
		$ZERO_OID $COMMIT_OID $SIGNATURE	first
		EOF

		git reflog write refs/heads/something $COMMIT_OID $COMMIT_OID second &&
		test_reflog_matches . refs/heads/something <<-EOF
		$ZERO_OID $COMMIT_OID $SIGNATURE	first
		$COMMIT_OID $COMMIT_OID $SIGNATURE	second
		EOF
	)
'

test_expect_success 'uses user.name and user.email config' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&
		test_commit initial &&
		COMMIT_OID=$(git rev-parse HEAD) &&

		sane_unset GIT_COMMITTER_NAME &&
		sane_unset GIT_COMMITTER_EMAIL &&
		git config --local user.name "Author" &&
		git config --local user.email "a@uth.or" &&
		git reflog write refs/heads/something $ZERO_OID $COMMIT_OID first &&
		test_reflog_matches . refs/heads/something <<-EOF
		$ZERO_OID $COMMIT_OID Author <a@uth.or> $GIT_COMMITTER_DATE	first
		EOF
	)
'

test_expect_success 'environment variables take precedence over config' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&
		test_commit initial &&
		COMMIT_OID=$(git rev-parse HEAD) &&

		git config --local user.name "Author" &&
		git config --local user.email "a@uth.or" &&
		git reflog write refs/heads/something $ZERO_OID $COMMIT_OID first &&
		test_reflog_matches . refs/heads/something <<-EOF
		$ZERO_OID $COMMIT_OID $SIGNATURE	first
		EOF
	)
'

test_expect_success 'can write to root ref' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&
		test_commit initial &&
		COMMIT_OID=$(git rev-parse HEAD) &&

		git reflog write ROOT_REF_HEAD $ZERO_OID $COMMIT_OID first &&
		test_reflog_matches . ROOT_REF_HEAD <<-EOF
		$ZERO_OID $COMMIT_OID $SIGNATURE	first
		EOF
	)
'

test_done
