#!/bin/sh

test_description='Test handling of ref names that check-ref-format rejects'
. ./test-lib.sh

test_expect_success setup '
	test_commit one &&
	test_commit two
'

test_expect_success 'fast-import: fail on invalid branch name ".badbranchname"' '
	test_when_finished "rm -f .git/objects/pack_* .git/objects/index_*" &&
	cat >input <<-INPUT_END &&
		commit .badbranchname
		committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
		data <<COMMIT
		corrupt
		COMMIT

		from refs/heads/master

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

		from refs/heads/master

	INPUT_END
	test_must_fail git fast-import <input
'

test_expect_success 'git branch shows badly named ref' '
	cp .git/refs/heads/master .git/refs/heads/broken...ref &&
	test_when_finished "rm -f .git/refs/heads/broken...ref" &&
	git branch >output &&
	grep -e "broken\.\.\.ref" output
'

test_expect_success 'branch -d can delete badly named ref' '
	cp .git/refs/heads/master .git/refs/heads/broken...ref &&
	test_when_finished "rm -f .git/refs/heads/broken...ref" &&
	git branch -d broken...ref &&
	git branch >output &&
	! grep -e "broken\.\.\.ref" output
'

test_expect_success 'branch -D can delete badly named ref' '
	cp .git/refs/heads/master .git/refs/heads/broken...ref &&
	test_when_finished "rm -f .git/refs/heads/broken...ref" &&
	git branch -D broken...ref &&
	git branch >output &&
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
	test_when_finished "rm -f .git/refs/heads/broken...ref" &&
	test_must_fail git branch broken...ref &&
	git branch >output &&
	! grep -e "broken\.\.\.ref" output
'

test_expect_success 'branch -m cannot rename to a bad ref name' '
	test_when_finished "rm -f .git/refs/heads/broken...ref" &&
	test_might_fail git branch -D goodref &&
	git branch goodref &&
	test_must_fail git branch -m goodref broken...ref &&
	test_cmp_rev master goodref &&
	git branch >output &&
	! grep -e "broken\.\.\.ref" output
'

test_expect_failure 'branch -m can rename from a bad ref name' '
	cp .git/refs/heads/master .git/refs/heads/broken...ref &&
	test_when_finished "rm -f .git/refs/heads/broken...ref" &&
	git branch -m broken...ref renamed &&
	test_cmp_rev master renamed &&
	git branch >output &&
	! grep -e "broken\.\.\.ref" output
'

test_expect_success 'push cannot create a badly named ref' '
	test_when_finished "rm -f .git/refs/heads/broken...ref" &&
	test_must_fail git push "file://$(pwd)" HEAD:refs/heads/broken...ref &&
	git branch >output &&
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
		cp .git/refs/heads/master .git/refs/heads/broken...ref
	) &&
	git -C src push --mirror "file://$top/dest" &&
	git -C dest branch >output &&
	! grep -e "broken\.\.\.ref" output
'

test_expect_success 'rev-parse skips symref pointing to broken name' '
	test_when_finished "rm -f .git/refs/heads/broken...ref" &&
	git branch shadow one &&
	cp .git/refs/heads/master .git/refs/heads/broken...ref &&
	git symbolic-ref refs/tags/shadow refs/heads/broken...ref &&

	git rev-parse --verify one >expect &&
	git rev-parse --verify shadow >actual 2>err &&
	test_cmp expect actual &&
	test_i18ngrep "ignoring.*refs/tags/shadow" err
'

test_expect_success 'update-ref --no-deref -d can delete reference to broken name' '
	git symbolic-ref refs/heads/badname refs/heads/broken...ref &&
	test_when_finished "rm -f .git/refs/heads/badname" &&
	test_path_is_file .git/refs/heads/badname &&
	git update-ref --no-deref -d refs/heads/badname &&
	test_path_is_missing .git/refs/heads/badname
'

test_expect_success 'update-ref -d can delete broken name' '
	cp .git/refs/heads/master .git/refs/heads/broken...ref &&
	test_when_finished "rm -f .git/refs/heads/broken...ref" &&
	git update-ref -d refs/heads/broken...ref &&
	git branch >output &&
	! grep -e "broken\.\.\.ref" output
'

test_expect_success 'update-ref -d cannot delete non-ref in .git dir' '
	echo precious >.git/my-private-file &&
	echo precious >expect &&
	test_must_fail git update-ref -d my-private-file &&
	test_cmp expect .git/my-private-file
'

test_expect_success 'update-ref -d cannot delete absolute path' '
	git branch -f extra &&
	test_must_fail git update-ref -d "$(pwd)/.git/refs/heads/extra" &&
	test_cmp_rev HEAD extra
'

test_expect_success 'update-ref --stdin fails create with bad ref name' '
	echo "create ~a refs/heads/master" >stdin &&
	test_must_fail git update-ref --stdin <stdin 2>err &&
	grep "fatal: invalid ref format: ~a" err
'

test_expect_success 'update-ref --stdin fails update with bad ref name' '
	echo "update ~a refs/heads/master" >stdin &&
	test_must_fail git update-ref --stdin <stdin 2>err &&
	grep "fatal: invalid ref format: ~a" err
'

test_expect_success 'update-ref --stdin fails delete with bad ref name' '
	echo "delete ~a refs/heads/master" >stdin &&
	test_must_fail git update-ref --stdin <stdin 2>err &&
	grep "fatal: invalid ref format: ~a" err
'

test_expect_success 'update-ref --stdin -z fails create with bad ref name' '
	printf "%s\0" "create ~a " refs/heads/master >stdin &&
	test_must_fail git update-ref -z --stdin <stdin 2>err &&
	grep "fatal: invalid ref format: ~a " err
'

test_expect_success 'update-ref --stdin -z fails update with bad ref name' '
	printf "%s\0" "update ~a" refs/heads/master "" >stdin &&
	test_must_fail git update-ref -z --stdin <stdin 2>err &&
	grep "fatal: invalid ref format: ~a" err
'

test_expect_success 'update-ref --stdin -z fails delete with bad ref name' '
	printf "%s\0" "delete ~a" refs/heads/master >stdin &&
	test_must_fail git update-ref -z --stdin <stdin 2>err &&
	grep "fatal: invalid ref format: ~a" err
'

test_done
