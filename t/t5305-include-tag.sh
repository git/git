#!/bin/sh

test_description='git pack-object --include-tag'
. ./test-lib.sh

TRASH=$(pwd)

test_expect_success setup '
	echo c >d &&
	git update-index --add d &&
	tree=$(git write-tree) &&
	commit=$(git commit-tree $tree </dev/null) &&
	echo "object $commit" >sig &&
	echo "type commit" >>sig &&
	echo "tag mytag" >>sig &&
	echo "tagger $(git var GIT_COMMITTER_IDENT)" >>sig &&
	echo >>sig &&
	echo "our test tag" >>sig &&
	tag=$(git mktag <sig) &&
	rm d sig &&
	git update-ref refs/tags/mytag $tag && {
		echo $tree &&
		echo $commit &&
		git ls-tree $tree | sed -e "s/.* \\([0-9a-f]*\\)	.*/\\1/"
	} >obj-list
'

test_expect_success 'pack without --include-tag' '
	packname=$(git pack-objects \
		--window=0 \
		test-no-include <obj-list)
'

test_expect_success 'unpack objects' '
	rm -rf clone.git &&
	git init clone.git &&
	git -C clone.git unpack-objects <test-no-include-${packname}.pack
'

test_expect_success 'check unpacked result (have commit, no tag)' '
	git rev-list --objects $commit >list.expect &&
	test_must_fail git -C clone.git cat-file -e $tag &&
	git -C clone.git rev-list --objects $commit >list.actual &&
	test_cmp list.expect list.actual
'

test_expect_success 'pack with --include-tag' '
	packname=$(git pack-objects \
		--window=0 \
		--include-tag \
		test-include <obj-list)
'

test_expect_success 'unpack objects' '
	rm -rf clone.git &&
	git init clone.git &&
	git -C clone.git unpack-objects <test-include-${packname}.pack
'

test_expect_success 'check unpacked result (have commit, have tag)' '
	git rev-list --objects mytag >list.expect &&
	git -C clone.git rev-list --objects $tag >list.actual &&
	test_cmp list.expect list.actual
'

test_done
