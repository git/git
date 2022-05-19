#!/bin/sh

test_description='test globbing (and noglob) of pathspec limiting'
. ./test-lib.sh

test_expect_success 'create cummits with glob characters' '
	test_cummit unrelated bar &&
	test_cummit vanilla foo &&
	# insert file "f*" in the cummit, but in a way that avoids
	# the name "f*" in the worktree, because it is not allowed
	# on Windows (the tests below do not depend on the presence
	# of the file in the worktree)
	but config core.protectNTFS false &&
	but update-index --add --cacheinfo 100644 "$(but rev-parse HEAD:foo)" "f*" &&
	test_tick &&
	but cummit -m star &&
	test_cummit bracket "f[o][o]"
'

test_expect_success 'vanilla pathspec matches literally' '
	echo vanilla >expect &&
	but log --format=%s -- foo >actual &&
	test_cmp expect actual
'

test_expect_success 'star pathspec globs' '
	cat >expect <<-\EOF &&
	bracket
	star
	vanilla
	EOF
	but log --format=%s -- "f*" >actual &&
	test_cmp expect actual
'

test_expect_success 'star pathspec globs' '
	cat >expect <<-\EOF &&
	bracket
	star
	vanilla
	EOF
	but log --format=%s -- ":(glob)f*" >actual &&
	test_cmp expect actual
'

test_expect_success 'bracket pathspec globs and matches literal brackets' '
	cat >expect <<-\EOF &&
	bracket
	vanilla
	EOF
	but log --format=%s -- "f[o][o]" >actual &&
	test_cmp expect actual
'

test_expect_success 'bracket pathspec globs and matches literal brackets' '
	cat >expect <<-\EOF &&
	bracket
	vanilla
	EOF
	but log --format=%s -- ":(glob)f[o][o]" >actual &&
	test_cmp expect actual
'

test_expect_success 'no-glob option matches literally (vanilla)' '
	echo vanilla >expect &&
	but --literal-pathspecs log --format=%s -- foo >actual &&
	test_cmp expect actual
'

test_expect_success 'no-glob option matches literally (vanilla)' '
	echo vanilla >expect &&
	but log --format=%s -- ":(literal)foo" >actual &&
	test_cmp expect actual
'

test_expect_success 'no-glob option matches literally (star)' '
	echo star >expect &&
	but --literal-pathspecs log --format=%s -- "f*" >actual &&
	test_cmp expect actual
'

test_expect_success 'no-glob option matches literally (star)' '
	echo star >expect &&
	but log --format=%s -- ":(literal)f*" >actual &&
	test_cmp expect actual
'

test_expect_success 'no-glob option matches literally (bracket)' '
	echo bracket >expect &&
	but --literal-pathspecs log --format=%s -- "f[o][o]" >actual &&
	test_cmp expect actual
'

test_expect_success 'no-glob option matches literally (bracket)' '
	echo bracket >expect &&
	but log --format=%s -- ":(literal)f[o][o]" >actual &&
	test_cmp expect actual
'

test_expect_success 'no-glob option disables :(literal)' '
	but --literal-pathspecs log --format=%s -- ":(literal)foo" >actual &&
	test_must_be_empty actual
'

test_expect_success 'no-glob environment variable works' '
	echo star >expect &&
	BUT_LITERAL_PATHSPECS=1 but log --format=%s -- "f*" >actual &&
	test_cmp expect actual
'

test_expect_success 'blame takes global pathspec flags' '
	but --literal-pathspecs blame -- foo &&
	but --icase-pathspecs   blame -- foo &&
	but --glob-pathspecs    blame -- foo &&
	but --noglob-pathspecs  blame -- foo
'

test_expect_success 'setup xxx/bar' '
	mkdir xxx &&
	test_cummit xxx xxx/bar
'

test_expect_success '**/ works with :(glob)' '
	cat >expect <<-\EOF &&
	xxx
	unrelated
	EOF
	but log --format=%s -- ":(glob)**/bar" >actual &&
	test_cmp expect actual
'

test_expect_success '**/ does not work with --noglob-pathspecs' '
	but --noglob-pathspecs log --format=%s -- "**/bar" >actual &&
	test_must_be_empty actual
'

test_expect_success '**/ works with :(glob) and --noglob-pathspecs' '
	cat >expect <<-\EOF &&
	xxx
	unrelated
	EOF
	but --noglob-pathspecs log --format=%s -- ":(glob)**/bar" >actual &&
	test_cmp expect actual
'

test_expect_success '**/ works with --glob-pathspecs' '
	cat >expect <<-\EOF &&
	xxx
	unrelated
	EOF
	but --glob-pathspecs log --format=%s -- "**/bar" >actual &&
	test_cmp expect actual
'

test_expect_success '**/ does not work with :(literal) and --glob-pathspecs' '
	but --glob-pathspecs log --format=%s -- ":(literal)**/bar" >actual &&
	test_must_be_empty actual
'

test_done
