#!/bin/sh

test_description='Test handling of ref names that check-ref-format rejects'
GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

test_expect_success setup '
	test_commit one &&
	test_commit two &&
	main_sha1=$(git rev-parse refs/heads/main)
'

test_expect_success 'fast-import: fail on invalid branch name ".badbranchname"' '
	test_when_finished "rm -f .git/objects/pack_* .git/objects/index_*" &&
	cat >input <<-INPUT_END &&
		commit .badbranchname
		committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
		data <<COMMIT
		corrupt
		COMMIT

		from refs/heads/main

	INPUT_END
	test_must_fail git fast-import <input
'

test_expect_success 'fast-import: fail on invalid branch name "bad[branch]name"' '
	test_when_finished "rm -f .git/objects/pack_* .git/objects/index_*" &&
	cat >input <<-INPUT_END &&
		commit bad[branch]name
		committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
		data <<COMMIT
		corrupt
		COMMIT

		from refs/heads/main

	INPUT_END
	test_must_fail git fast-import <input
'

test_expect_success 'git branch shows badly named ref as warning' '
	test-tool ref-store main update-ref msg "refs/heads/broken...ref" $main_sha1 $ZERO_OID REF_SKIP_REFNAME_VERIFICATION &&
	test_when_finished "test-tool ref-store main delete-refs REF_NO_DEREF msg refs/heads/broken...ref" &&
	git branch >output 2>error &&
	test_i18ngrep -e "ignoring ref with broken name refs/heads/broken\.\.\.ref" error &&
	! grep -e "broken\.\.\.ref" output
'

test_expect_success 'branch -d can delete badly named ref' '
	test-tool ref-store main update-ref msg "refs/heads/broken...ref" $main_sha1 $ZERO_OID REF_SKIP_REFNAME_VERIFICATION &&
	test_when_finished "test-tool ref-store main delete-refs REF_NO_DEREF msg refs/heads/broken...ref" &&
	git branch -d broken...ref &&
	git branch >output 2>error &&
	! grep -e "broken\.\.\.ref" error &&
	! grep -e "broken\.\.\.ref" output
'

test_expect_success 'branch -D can delete badly named ref' '
	test-tool ref-store main update-ref msg "refs/heads/broken...ref" $main_sha1 $ZERO_OID REF_SKIP_REFNAME_VERIFICATION &&
	test_when_finished "test-tool ref-store main delete-refs REF_NO_DEREF msg refs/heads/broken...ref" &&
	git branch -D broken...ref &&
	git branch >output 2>error &&
	! grep -e "broken\.\.\.ref" error &&
	! grep -e "broken\.\.\.ref" output
'

test_expect_success 'branch -D cannot delete non-ref in .git dir' '
	echo precious >.git/my-private-file &&
	echo precious >expect &&
	test_must_fail git branch -D ../../my-private-file &&
	test_cmp expect .git/my-private-file
'

test_expect_success 'branch -D cannot delete ref in .git dir' '
	git rev-parse HEAD >.git/my-private-file &&
	git rev-parse HEAD >expect &&
	git branch foo/legit &&
	test_must_fail git branch -D foo////./././../../../my-private-file &&
	test_cmp expect .git/my-private-file
'

test_expect_success 'branch -D cannot delete absolute path' '
	git branch -f extra &&
	test_must_fail git branch -D "$(pwd)/.git/refs/heads/extra" &&
	test_cmp_rev HEAD extra
'

test_expect_success 'git branch cannot create a badly named ref' '
	test_when_finished "test-tool ref-store main delete-refs REF_NO_DEREF msg refs/heads/broken...ref" &&
	test_must_fail git branch broken...ref &&
	git branch >output 2>error &&
	! grep -e "broken\.\.\.ref" error &&
	! grep -e "broken\.\.\.ref" output
'

test_expect_success 'branch -m cannot rename to a bad ref name' '
	test_when_finished "test-tool ref-store main delete-refs REF_NO_DEREF msg refs/heads/broken...ref" &&
	test_might_fail git branch -D goodref &&
	git branch goodref &&
	test_must_fail git branch -m goodref broken...ref &&
	test_cmp_rev main goodref &&
	git branch >output 2>error &&
	! grep -e "broken\.\.\.ref" error &&
	! grep -e "broken\.\.\.ref" output
'

test_expect_failure 'branch -m can rename from a bad ref name' '
	test-tool ref-store main update-ref msg "refs/heads/broken...ref" $main_sha1 $ZERO_OID REF_SKIP_REFNAME_VERIFICATION &&

	test_when_finished "test-tool ref-store main delete-refs REF_NO_DEREF msg refs/heads/broken...ref" &&
	git branch -m broken...ref renamed &&
	test_cmp_rev main renamed &&
	git branch >output 2>error &&
	! grep -e "broken\.\.\.ref" error &&
	! grep -e "broken\.\.\.ref" output
'

test_expect_success 'push cannot create a badly named ref' '
	test_when_finished "test-tool ref-store main delete-refs REF_NO_DEREF msg refs/heads/broken...ref" &&
	test_must_fail git push "file://$(pwd)" HEAD:refs/heads/broken...ref &&
	git branch >output 2>error &&
	! grep -e "broken\.\.\.ref" error &&
	! grep -e "broken\.\.\.ref" output
'

test_expect_failure 'push --mirror can delete badly named ref' '
	top=$(pwd) &&
	git init src &&
	git init dest &&

	(
		cd src &&
		test_commit one
	) &&
	(
		cd dest &&
		test_commit two &&
		git checkout --detach &&
		test-tool ref-store main update-ref msg "refs/heads/broken...ref" $main_sha1 $ZERO_OID REF_SKIP_REFNAME_VERIFICATION
	) &&
	git -C src push --mirror "file://$top/dest" &&
	git -C dest branch >output 2>error &&
	! grep -e "broken\.\.\.ref" error &&
	! grep -e "broken\.\.\.ref" output
'

test_expect_success 'rev-parse skips symref pointing to broken name' '
	test_when_finished "test-tool ref-store main delete-refs REF_NO_DEREF msg refs/heads/broken...ref" &&
	git branch shadow one &&
	test-tool ref-store main update-ref msg "refs/heads/broken...ref" $main_sha1 $ZERO_OID REF_SKIP_REFNAME_VERIFICATION &&
	test-tool ref-store main create-symref refs/tags/shadow refs/heads/broken...ref msg &&
	test_when_finished "test-tool ref-store main delete-refs REF_NO_DEREF msg refs/tags/shadow" &&
	git rev-parse --verify one >expect &&
	git rev-parse --verify shadow >actual 2>err &&
	test_cmp expect actual &&
	test_i18ngrep "ignoring dangling symref refs/tags/shadow" err
'

test_expect_success 'for-each-ref emits warnings for broken names' '
	test-tool ref-store main update-ref msg "refs/heads/broken...ref" $main_sha1 $ZERO_OID REF_SKIP_REFNAME_VERIFICATION &&
	test_when_finished "test-tool ref-store main delete-refs REF_NO_DEREF msg refs/heads/broken...ref" &&
	printf "ref: refs/heads/broken...ref\n" >.git/refs/heads/badname &&
	test_when_finished "test-tool ref-store main delete-refs REF_NO_DEREF msg refs/heads/badname" &&
	printf "ref: refs/heads/main\n" >.git/refs/heads/broken...symref &&
	test_when_finished "test-tool ref-store main delete-refs REF_NO_DEREF msg refs/heads/broken...symref" &&
	git for-each-ref >output 2>error &&
	! grep -e "broken\.\.\.ref" output &&
	! grep -e "badname" output &&
	! grep -e "broken\.\.\.symref" output &&
	test_i18ngrep "ignoring ref with broken name refs/heads/broken\.\.\.ref" error &&
	test_i18ngrep ! "ignoring broken ref refs/heads/badname" error &&
	test_i18ngrep "ignoring ref with broken name refs/heads/broken\.\.\.symref" error
'

test_expect_success 'update-ref -d can delete broken name' '
	test-tool ref-store main update-ref msg "refs/heads/broken...ref" $main_sha1 $ZERO_OID REF_SKIP_REFNAME_VERIFICATION &&
	test_when_finished "test-tool ref-store main delete-refs REF_NO_DEREF msg refs/heads/broken...ref" &&
	git update-ref -d refs/heads/broken...ref >output 2>error &&
	test_must_be_empty output &&
	test_must_be_empty error &&
	git branch >output 2>error &&
	! grep -e "broken\.\.\.ref" error &&
	! grep -e "broken\.\.\.ref" output
'

test_expect_success 'branch -d can delete broken name' '
	test-tool ref-store main update-ref msg "refs/heads/broken...ref" $main_sha1 $ZERO_OID REF_SKIP_REFNAME_VERIFICATION &&
	test_when_finished "test-tool ref-store main delete-refs REF_NO_DEREF msg refs/heads/broken...ref" &&
	git branch -d broken...ref >output 2>error &&
	test_i18ngrep "Deleted branch broken...ref (was broken)" output &&
	test_must_be_empty error &&
	git branch >output 2>error &&
	! grep -e "broken\.\.\.ref" error &&
	! grep -e "broken\.\.\.ref" output
'

test_expect_success 'update-ref --no-deref -d can delete symref to broken name' '
	test-tool ref-store main update-ref msg "refs/heads/broken...ref" $main_sha1 $ZERO_OID REF_SKIP_REFNAME_VERIFICATION &&

	test_when_finished "test-tool ref-store main delete-refs REF_NO_DEREF msg refs/heads/broken...ref" &&
	test-tool ref-store main create-symref refs/heads/badname refs/heads/broken...ref msg &&
	test_when_finished "test-tool ref-store main delete-refs REF_NO_DEREF msg refs/heads/badname" &&
	test_ref_exists refs/heads/badname &&
	git update-ref --no-deref -d refs/heads/badname >output 2>error &&
	test_ref_missing refs/heads/badname &&
	test_must_be_empty output &&
	test_must_be_empty error
'

test_expect_success 'branch -d can delete symref to broken name' '
	test-tool ref-store main update-ref msg "refs/heads/broken...ref" $main_sha1 $ZERO_OID REF_SKIP_REFNAME_VERIFICATION &&
	test_when_finished "test-tool ref-store main delete-refs REF_NO_DEREF msg refs/heads/broken...ref" &&
	test-tool ref-store main create-symref refs/heads/badname refs/heads/broken...ref msg &&
	test_when_finished "test-tool ref-store main delete-refs REF_NO_DEREF msg refs/heads/badname" &&
	test_ref_exists refs/heads/badname &&
	git branch -d badname >output 2>error &&
	test_ref_missing refs/heads/badname &&
	test_i18ngrep "Deleted branch badname (was refs/heads/broken\.\.\.ref)" output &&
	test_must_be_empty error
'

test_expect_success 'update-ref --no-deref -d can delete dangling symref to broken name' '
	test-tool ref-store main create-symref refs/heads/badname refs/heads/broken...ref msg &&
	test_when_finished "test-tool ref-store main delete-refs REF_NO_DEREF msg refs/heads/badname" &&
	test_ref_exists refs/heads/badname &&
	git update-ref --no-deref -d refs/heads/badname >output 2>error &&
	test_ref_missing refs/heads/badname &&
	test_must_be_empty output &&
	test_must_be_empty error
'

test_expect_success 'branch -d can delete dangling symref to broken name' '
	test-tool ref-store main create-symref refs/heads/badname refs/heads/broken...ref msg &&
	test_when_finished "test-tool ref-store main delete-refs REF_NO_DEREF msg refs/heads/badname" &&
	test_ref_exists refs/heads/badname &&
	git branch -d badname >output 2>error &&
	test_ref_missing refs/heads/badname &&
	test_i18ngrep "Deleted branch badname (was refs/heads/broken\.\.\.ref)" output &&
	test_must_be_empty error
'

test_expect_success 'update-ref -d can delete broken name through symref' '
	test-tool ref-store main update-ref msg "refs/heads/broken...ref" $main_sha1 $ZERO_OID REF_SKIP_REFNAME_VERIFICATION &&
	test_when_finished "test-tool ref-store main delete-refs REF_NO_DEREF msg refs/heads/broken...ref" &&
	test-tool ref-store main create-symref refs/heads/badname refs/heads/broken...ref msg &&
	test_when_finished "test-tool ref-store main delete-refs REF_NO_DEREF msg refs/heads/badname" &&
	test_ref_exists refs/heads/broken...ref &&
	git update-ref -d refs/heads/badname >output 2>error &&
	test_ref_missing refs/heads/broken...ref &&
	test_must_be_empty output &&
	test_must_be_empty error
'

test_expect_success 'update-ref --no-deref -d can delete symref with broken name' '
	printf "ref: refs/heads/main\n" >.git/refs/heads/broken...symref &&
	test_when_finished "test-tool ref-store main delete-refs REF_NO_DEREF msg refs/heads/broken...symref" &&
	test_ref_exists refs/heads/broken...symref &&
	git update-ref --no-deref -d refs/heads/broken...symref >output 2>error &&
	test_ref_missing refs/heads/broken...symref &&
	test_must_be_empty output &&
	test_must_be_empty error
'

test_expect_success 'branch -d can delete symref with broken name' '
	printf "ref: refs/heads/main\n" >.git/refs/heads/broken...symref &&
	test_when_finished "test-tool ref-store main delete-refs REF_NO_DEREF msg refs/heads/broken...symref" &&
	test_ref_exists refs/heads/broken...symref &&
	git branch -d broken...symref >output 2>error &&
	test_ref_missing refs/heads/broken...symref &&
	test_i18ngrep "Deleted branch broken...symref (was refs/heads/main)" output &&
	test_must_be_empty error
'

test_expect_success 'update-ref --no-deref -d can delete dangling symref with broken name' '
	printf "ref: refs/heads/idonotexist\n" >.git/refs/heads/broken...symref &&
	test_when_finished "test-tool ref-store main delete-refs REF_NO_DEREF msg refs/heads/broken...symref" &&
	test_ref_exists refs/heads/broken...symref &&
	git update-ref --no-deref -d refs/heads/broken...symref >output 2>error &&
	test_ref_missing refs/heads/broken...symref &&
	test_must_be_empty output &&
	test_must_be_empty error
'

test_expect_success 'branch -d can delete dangling symref with broken name' '
	printf "ref: refs/heads/idonotexist\n" >.git/refs/heads/broken...symref &&
	test_when_finished "test-tool ref-store main delete-refs REF_NO_DEREF msg refs/heads/broken...symref" &&
	test_ref_exists refs/heads/broken...symref &&
	git branch -d broken...symref >output 2>error &&
	test_ref_missing refs/heads/broken...symref &&
	test_i18ngrep "Deleted branch broken...symref (was refs/heads/idonotexist)" output &&
	test_must_be_empty error
'

test_expect_success 'update-ref -d cannot delete non-ref in .git dir' '
	echo precious >.git/my-private-file &&
	echo precious >expect &&
	test_must_fail git update-ref -d my-private-file >output 2>error &&
	test_must_be_empty output &&
	test_i18ngrep -e "refusing to update ref with bad name" error &&
	test_cmp expect .git/my-private-file
'

test_expect_success 'update-ref -d cannot delete absolute path' '
	git branch -f extra &&
	test_must_fail git update-ref -d "$(pwd)/.git/refs/heads/extra" &&
	test_cmp_rev HEAD extra
'

test_expect_success 'update-ref --stdin fails create with bad ref name' '
	echo "create ~a refs/heads/main" >stdin &&
	test_must_fail git update-ref --stdin <stdin 2>err &&
	grep "fatal: invalid ref format: ~a" err
'

test_expect_success 'update-ref --stdin fails update with bad ref name' '
	echo "update ~a refs/heads/main" >stdin &&
	test_must_fail git update-ref --stdin <stdin 2>err &&
	grep "fatal: invalid ref format: ~a" err
'

test_expect_success 'update-ref --stdin fails delete with bad ref name' '
	echo "delete ~a refs/heads/main" >stdin &&
	test_must_fail git update-ref --stdin <stdin 2>err &&
	grep "fatal: invalid ref format: ~a" err
'

test_expect_success 'update-ref --stdin -z fails create with bad ref name' '
	printf "%s\0" "create ~a " refs/heads/main >stdin &&
	test_must_fail git update-ref -z --stdin <stdin 2>err &&
	grep "fatal: invalid ref format: ~a " err
'

test_expect_success 'update-ref --stdin -z fails update with bad ref name' '
	printf "%s\0" "update ~a" refs/heads/main "" >stdin &&
	test_must_fail git update-ref -z --stdin <stdin 2>err &&
	grep "fatal: invalid ref format: ~a" err
'

test_expect_success 'update-ref --stdin -z fails delete with bad ref name' '
	printf "%s\0" "delete ~a" refs/heads/main >stdin &&
	test_must_fail git update-ref -z --stdin <stdin 2>err &&
	grep "fatal: invalid ref format: ~a" err
'

test_expect_success 'branch rejects HEAD as a branch name' '
	test_must_fail git branch HEAD HEAD^ &&
	test_must_fail git show-ref refs/heads/HEAD
'

test_expect_success 'checkout -b rejects HEAD as a branch name' '
	test_must_fail git checkout -B HEAD HEAD^ &&
	test_must_fail git show-ref refs/heads/HEAD
'

test_expect_success 'update-ref can operate on refs/heads/HEAD' '
	git update-ref refs/heads/HEAD HEAD^ &&
	git show-ref refs/heads/HEAD &&
	git update-ref -d refs/heads/HEAD &&
	test_must_fail git show-ref refs/heads/HEAD
'

test_expect_success 'branch -d can remove refs/heads/HEAD' '
	git update-ref refs/heads/HEAD HEAD^ &&
	git branch -d HEAD &&
	test_must_fail git show-ref refs/heads/HEAD
'

test_expect_success 'branch -m can rename refs/heads/HEAD' '
	git update-ref refs/heads/HEAD HEAD^ &&
	git branch -m HEAD tail &&
	test_must_fail git show-ref refs/heads/HEAD &&
	git show-ref refs/heads/tail
'

test_expect_success 'branch -d can remove refs/heads/-dash' '
	git update-ref refs/heads/-dash HEAD^ &&
	git branch -d -- -dash &&
	test_must_fail git show-ref refs/heads/-dash
'

test_expect_success 'branch -m can rename refs/heads/-dash' '
	git update-ref refs/heads/-dash HEAD^ &&
	git branch -m -- -dash dash &&
	test_must_fail git show-ref refs/heads/-dash &&
	git show-ref refs/heads/dash
'

test_done
