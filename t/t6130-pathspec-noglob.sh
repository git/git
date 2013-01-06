#!/bin/sh

test_description='test globbing (and noglob) of pathspec limiting'
. ./test-lib.sh

test_expect_success 'create commits with glob characters' '
	test_commit unrelated bar &&
	test_commit vanilla foo &&
	# insert file "f*" in the commit, but in a way that avoids
	# the name "f*" in the worktree, because it is not allowed
	# on Windows (the tests below do not depend on the presence
	# of the file in the worktree)
	git update-index --add --cacheinfo 100644 "$(git rev-parse HEAD:foo)" "f*" &&
	test_tick &&
	git commit -m star &&
	test_commit bracket "f[o][o]"
'

test_expect_success 'vanilla pathspec matches literally' '
	echo vanilla >expect &&
	git log --format=%s -- foo >actual &&
	test_cmp expect actual
'

test_expect_success 'star pathspec globs' '
	cat >expect <<-\EOF &&
	bracket
	star
	vanilla
	EOF
	git log --format=%s -- "f*" >actual &&
	test_cmp expect actual
'

test_expect_success 'bracket pathspec globs and matches literal brackets' '
	cat >expect <<-\EOF &&
	bracket
	vanilla
	EOF
	git log --format=%s -- "f[o][o]" >actual &&
	test_cmp expect actual
'

test_expect_success 'no-glob option matches literally (vanilla)' '
	echo vanilla >expect &&
	git --literal-pathspecs log --format=%s -- foo >actual &&
	test_cmp expect actual
'

test_expect_success 'no-glob option matches literally (star)' '
	echo star >expect &&
	git --literal-pathspecs log --format=%s -- "f*" >actual &&
	test_cmp expect actual
'

test_expect_success 'no-glob option matches literally (bracket)' '
	echo bracket >expect &&
	git --literal-pathspecs log --format=%s -- "f[o][o]" >actual &&
	test_cmp expect actual
'

test_expect_success 'no-glob environment variable works' '
	echo star >expect &&
	GIT_LITERAL_PATHSPECS=1 git log --format=%s -- "f*" >actual &&
	test_cmp expect actual
'

test_done
