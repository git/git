#!/bin/sh

test_description='test dwim of revs versus pathspecs in revision parser'
. ./test-lib.sh

test_expect_success 'setup' '
	test_cummit base &&
	echo content >"br[ack]ets" &&
	but add . &&
	test_tick &&
	but cummit -m brackets
'

test_expect_success 'non-rev wildcard dwims to pathspec' '
	but log -- "*.t" >expect &&
	but log    "*.t" >actual &&
	test_cmp expect actual
'

test_expect_success 'tree:path with metacharacters dwims to rev' '
	but show "HEAD:br[ack]ets" -- >expect &&
	but show "HEAD:br[ack]ets"    >actual &&
	test_cmp expect actual
'

test_expect_success '^{foo} with metacharacters dwims to rev' '
	but log "HEAD^{/b.*}" -- >expect &&
	but log "HEAD^{/b.*}"    >actual &&
	test_cmp expect actual
'

test_expect_success '@{foo} with metacharacters dwims to rev' '
	but log "HEAD@{now [or thereabouts]}" -- >expect &&
	but log "HEAD@{now [or thereabouts]}"    >actual &&
	test_cmp expect actual
'

test_expect_success ':/*.t from a subdir dwims to a pathspec' '
	mkdir subdir &&
	(
		cd subdir &&
		but log -- ":/*.t" >expect &&
		but log    ":/*.t" >actual &&
		test_cmp expect actual
	)
'

test_done
