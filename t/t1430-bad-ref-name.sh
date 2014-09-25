#!/bin/sh

test_description='Test handling of ref names that check-ref-format rejects'
. ./test-lib.sh

test_expect_success setup '
	test_commit one
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

test_expect_success 'update-ref --no-deref -d can delete reference to broken name' '
	git symbolic-ref refs/heads/badname refs/heads/broken...ref &&
	test_when_finished "rm -f .git/refs/heads/badname" &&
	test_path_is_file .git/refs/heads/badname &&
	git update-ref --no-deref -d refs/heads/badname &&
	test_path_is_missing .git/refs/heads/badname
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
