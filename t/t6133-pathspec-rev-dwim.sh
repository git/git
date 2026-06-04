#!/bin/sh

test_description='test dwim of revs versus pathspecs in revision parser'

. ./test-lib.sh

test_expect_success 'setup' '
	test_commit base &&
	echo content >"br[ack]ets" &&
	git add . &&
	test_tick &&
	git commit -m brackets
'

test_expect_success 'non-rev wildcard dwims to pathspec' '
	git log -- "*.t" >expect &&
	git log    "*.t" >actual &&
	test_cmp expect actual
'

test_expect_success 'tree:path with metacharacters dwims to rev' '
	git show "HEAD:br[ack]ets" -- >expect &&
	git show "HEAD:br[ack]ets"    >actual &&
	test_cmp expect actual
'

test_expect_success '^{foo} with metacharacters dwims to rev' '
	git log "HEAD^{/b.*}" -- >expect &&
	git log "HEAD^{/b.*}"    >actual &&
	test_cmp expect actual
'

test_expect_success '@{foo} with metacharacters dwims to rev' '
	git log "HEAD@{now [or thereabouts]}" -- >expect &&
	git log "HEAD@{now [or thereabouts]}"    >actual &&
	test_cmp expect actual
'

test_expect_success ':/*.t from a subdir dwims to a pathspec' '
	mkdir subdir &&
	(
		cd subdir &&
		git log -- ":/*.t" >expect &&
		git log    ":/*.t" >actual &&
		test_cmp expect actual
	)
'

test_done
