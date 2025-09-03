git_show_ref_exists=${git_show_ref_exists:-git show-ref --exists}

test_expect_success setup '
	test_commit --annotate A &&
	git checkout -b side &&
	test_commit --annotate B &&
	git checkout main &&
	test_commit C &&
	git branch B A^0
'

test_expect_success '--exists with existing reference' '
	${git_show_ref_exists} refs/heads/$GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME
'

test_expect_success '--exists with missing reference' '
	test_expect_code 2 ${git_show_ref_exists} refs/heads/does-not-exist
'

test_expect_success '--exists does not use DWIM' '
	test_expect_code 2 ${git_show_ref_exists} $GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME 2>err &&
	grep "reference does not exist" err
'

test_expect_success '--exists with HEAD' '
	${git_show_ref_exists} HEAD
'

test_expect_success '--exists with bad reference name' '
	test_when_finished "git update-ref -d refs/heads/bad...name" &&
	new_oid=$(git rev-parse HEAD) &&
	test-tool ref-store main update-ref msg refs/heads/bad...name $new_oid $ZERO_OID REF_SKIP_REFNAME_VERIFICATION &&
	${git_show_ref_exists} refs/heads/bad...name
'

test_expect_success '--exists with arbitrary symref' '
	test_when_finished "git symbolic-ref -d refs/symref" &&
	git symbolic-ref refs/symref refs/heads/$GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME &&
	${git_show_ref_exists} refs/symref
'

test_expect_success '--exists with dangling symref' '
	test_when_finished "git symbolic-ref -d refs/heads/dangling" &&
	git symbolic-ref refs/heads/dangling refs/heads/does-not-exist &&
	${git_show_ref_exists} refs/heads/dangling
'

test_expect_success '--exists with nonexistent object ID' '
	test-tool ref-store main update-ref msg refs/heads/missing-oid $(test_oid 001) $ZERO_OID REF_SKIP_OID_VERIFICATION &&
	${git_show_ref_exists} refs/heads/missing-oid
'

test_expect_success '--exists with non-commit object' '
	tree_oid=$(git rev-parse HEAD^{tree}) &&
	test-tool ref-store main update-ref msg refs/heads/tree ${tree_oid} $ZERO_OID REF_SKIP_OID_VERIFICATION &&
	${git_show_ref_exists} refs/heads/tree
'

test_expect_success '--exists with directory fails with generic error' '
	cat >expect <<-EOF &&
	error: reference does not exist
	EOF
	test_expect_code 2 ${git_show_ref_exists} refs/heads 2>err &&
	test_cmp expect err
'

test_expect_success '--exists with non-existent special ref' '
	test_expect_code 2 ${git_show_ref_exists} FETCH_HEAD
'

test_expect_success '--exists with existing special ref' '
	test_when_finished "rm .git/FETCH_HEAD" &&
	git rev-parse HEAD >.git/FETCH_HEAD &&
	${git_show_ref_exists} FETCH_HEAD
'

test_done
