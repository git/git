#!/bin/sh

test_description='test handling of bogus index entries'

. ./test-lib.sh

test_expect_success 'create tree with null sha1' '
	tree=$(printf "160000 commit $ZERO_OID\\tbroken\\n" | git mktree)
'

test_expect_success 'read-tree refuses to read null sha1' '
	test_must_fail git read-tree $tree
'

test_expect_success 'GIT_ALLOW_NULL_SHA1 overrides refusal' '
	GIT_ALLOW_NULL_SHA1=1 git read-tree $tree
'

test_expect_success 'git write-tree refuses to write null sha1' '
	test_must_fail git write-tree
'

test_done
