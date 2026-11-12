#!/bin/sh

test_description='test fast-import ref updates'
GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

setup_two_commit_repo () {
	test_when_finished "rm -rf repo" &&
	git init repo &&
	test_commit -C repo one &&
	test_commit -C repo two
}

test_expect_success 'non-fast-forward update skips' '
	setup_two_commit_repo &&
	(
		cd repo &&
		old_branch=$(git rev-parse --verify main^0) &&
		test_tick &&
		cat >input <<-INPUT_END &&
		commit refs/heads/main
		committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
		data <<COMMIT
		losing things already?
		COMMIT

		from refs/heads/main~1

		reset refs/heads/other
		from refs/heads/main

		INPUT_END

		test_must_fail git -c fastimport.unpackLimit=0 fast-import <input &&
		# branch must remain unaffected
		test $old_branch = $(git rev-parse --verify main^0) &&

		for p in .git/objects/pack/*.pack
		do
			git verify-pack "$p" || return 1
		done &&

		cat >expect <<-EOF &&
		tree $(git rev-parse main~1^{tree})
		parent $(git rev-parse main~1)
		author $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
		committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE

		losing things already?
		EOF
		git cat-file commit other >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'non-fast-forward update forced' '
	setup_two_commit_repo &&
	(
		cd repo &&
		old_branch=$(git rev-parse --verify main^0) &&
		test_tick &&
		cat >input <<-INPUT_END &&
		commit refs/heads/main
		committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
		data <<COMMIT
		losing things already?
		COMMIT

		from refs/heads/main~1

		INPUT_END
		git -c fastimport.unpackLimit=0 fast-import --force <input &&
		# branch changed, but logged
		test $old_branch != $(git rev-parse --verify main^0) &&
		test $old_branch = $(git rev-parse --verify main@{1}) &&

		for p in .git/objects/pack/*.pack
		do
			git verify-pack "$p" || return 1
		done
	)
'

test_expect_success 'reset existing branch creates empty commit' '
	setup_two_commit_repo &&
	(
		cd repo &&
		cat >input <<-INPUT_END &&
		commit refs/heads/J
		committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
		data <<COMMIT
		create J
		COMMIT

		from refs/heads/main

		reset refs/heads/J

		commit refs/heads/J
		committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
		data <<COMMIT
		initialize J
		COMMIT

		INPUT_END
		git fast-import <input &&
		# branch has 1 commit, empty tree
		test 1 = $(git rev-list J | wc -l) &&
		test 0 = $(git ls-tree J | wc -l)
	)
'

test_expect_success 'tag must fail on empty branch' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&
		cat >input <<-INPUT_END &&
		reset refs/heads/J2

		tag wrong_tag
		from refs/heads/J2
		data <<EOF
		Tag branch that was reset.
		EOF
		INPUT_END
		test_must_fail git fast-import <input
	)
'

test_expect_success 'reinit branch with from' '
	setup_two_commit_repo &&
	(
		cd repo &&
		cat >input <<-INPUT_END &&
		commit refs/heads/K
		committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
		data <<COMMIT
		create K
		COMMIT

		from refs/heads/main

		commit refs/heads/K
		committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
		data <<COMMIT
		redo K
		COMMIT

		from refs/heads/main^1

		INPUT_END
		git fast-import <input &&
		# K^1 = main^1
		test $(git rev-parse --verify main^1) \
			= $(git rev-parse --verify K^1)
	)
'

test_expect_success 'delete branch' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&
		test_commit one &&

		git branch to-delete &&
		git fast-import <<-EOF &&
		reset refs/heads/to-delete
		from $ZERO_OID
		EOF
		test_must_fail git rev-parse --verify refs/heads/to-delete
	)
'

test_expect_success 'empty reset doesnt delete branch' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&
		test_commit one &&

		git branch not-to-delete &&
		git fast-import <<-EOF &&
		reset refs/heads/not-to-delete
		EOF
		git show-ref &&
		git rev-parse --verify refs/heads/not-to-delete
	)
'

test_done
