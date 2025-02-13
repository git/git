#!/bin/sh

test_description='tests for git clone --revision'
GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

test_expect_success 'setup' '
	test_commit --no-tag "initial commit" README "Hello" &&
	test_commit --annotate "second commit" README "Hello world" v1.0 &&
	test_commit --no-tag "third commit" README "Hello world!" &&
	git switch -c feature v1.0 &&
	test_commit --no-tag "feature commit" README "Hello world!" &&
	git switch main
'

test_expect_success 'clone with --revision being a branch' '
	test_when_finished "rm -rf dst" &&
	git clone --revision=refs/heads/feature . dst &&
	git rev-parse refs/heads/feature >expect &&
	git -C dst rev-parse HEAD >actual &&
	test_must_fail git -C dst symbolic-ref -q HEAD >/dev/null &&
	test_cmp expect actual &&
	git -C dst for-each-ref refs >expect &&
	test_must_be_empty expect &&
	test_must_fail git -C dst config remote.origin.fetch
'

test_expect_success 'clone with --depth and --revision being a branch' '
	test_when_finished "rm -rf dst" &&
	git clone --no-local --depth=1 --revision=refs/heads/feature . dst &&
	git rev-parse refs/heads/feature >expect &&
	git -C dst rev-parse HEAD >actual &&
	test_must_fail git -C dst symbolic-ref -q HEAD >/dev/null &&
	test_cmp expect actual &&
	git -C dst for-each-ref refs >expect &&
	test_must_be_empty expect &&
	test_must_fail git -C dst config remote.origin.fetch &&
	git -C dst rev-list HEAD >actual &&
	test_line_count = 1 actual
'

test_expect_success 'clone with --revision being a tag' '
	test_when_finished "rm -rf dst" &&
	git clone --revision=refs/tags/v1.0 . dst &&
	git rev-parse refs/tags/v1.0^{} >expect &&
	git -C dst rev-parse HEAD >actual &&
	test_must_fail git -C dst symbolic-ref -q HEAD >/dev/null &&
	test_cmp expect actual &&
	git -C dst for-each-ref refs >expect &&
	test_must_be_empty expect &&
	test_must_fail git -C dst config remote.origin.fetch
'

test_expect_success 'clone with --revision being HEAD' '
	test_when_finished "rm -rf dst" &&
	git clone --revision=HEAD . dst &&
	git rev-parse HEAD >expect &&
	git -C dst rev-parse HEAD >actual &&
	test_must_fail git -C dst symbolic-ref -q HEAD >/dev/null &&
	test_cmp expect actual &&
	git -C dst for-each-ref refs >expect &&
	test_must_be_empty expect &&
	test_must_fail git -C dst config remote.origin.fetch
'

test_expect_success 'clone with --revision being a raw commit hash' '
	test_when_finished "rm -rf dst" &&
	oid=$(git rev-parse refs/heads/feature) &&
	git clone --revision=$oid . dst &&
	echo $oid >expect &&
	git -C dst rev-parse HEAD >actual &&
	test_must_fail git -C dst symbolic-ref -q HEAD >/dev/null &&
	test_cmp expect actual &&
	git -C dst for-each-ref refs >expect &&
	test_must_be_empty expect &&
	test_must_fail git -C dst config remote.origin.fetch
'

test_expect_success 'clone with --revision and --bare' '
	test_when_finished "rm -rf dst" &&
	git clone --revision=refs/heads/main --bare . dst &&
	oid=$(git rev-parse refs/heads/main) &&
	git -C dst cat-file -t $oid >actual &&
	echo "commit" >expect &&
	test_cmp expect actual &&
	git -C dst for-each-ref refs >expect &&
	test_must_be_empty expect &&
	test_must_fail git -C dst config remote.origin.fetch
'

test_expect_success 'clone with --revision being a short raw commit hash' '
	test_when_finished "rm -rf dst" &&
	oid=$(git rev-parse --short refs/heads/feature) &&
	test_must_fail git clone --revision=$oid . dst 2>err &&
	test_grep "fatal: Remote revision $oid not found in upstream origin" err
'

test_expect_success 'clone with --revision being a tree hash' '
	test_when_finished "rm -rf dst" &&
	oid=$(git rev-parse refs/heads/feature^{tree}) &&
	test_must_fail git clone --revision=$oid . dst 2>err &&
	test_grep "error: object $oid is a tree, not a commit" err
'

test_expect_success 'clone with --revision being the parent of a ref fails' '
	test_when_finished "rm -rf dst" &&
	test_must_fail git clone --revision=refs/heads/main^ . dst
'

test_expect_success 'clone with --revision and --branch fails' '
	test_when_finished "rm -rf dst" &&
	test_must_fail git clone --revision=refs/heads/main --branch=main . dst
'

test_expect_success 'clone with --revision and --mirror fails' '
	test_when_finished "rm -rf dst" &&
	test_must_fail git clone --revision=refs/heads/main --mirror . dst
'

test_done
