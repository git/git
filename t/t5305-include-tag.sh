#!/bin/sh

test_description='git pack-object --include-tag'
. ./test-lib.sh

TRASH=`pwd`

test_expect_success setup '
	echo c >d &&
	git update-index --add d &&
	tree=`git write-tree` &&
	commit=`git commit-tree $tree </dev/null` &&
	echo "object $commit" >sig &&
	echo "type commit" >>sig &&
	echo "tag mytag" >>sig &&
	echo "tagger $(git var GIT_COMMITTER_IDENT)" >>sig &&
	echo >>sig &&
	echo "our test tag" >>sig &&
	tag=`git mktag <sig` &&
	rm d sig &&
	git update-ref refs/tags/mytag $tag && {
		echo $tree &&
		echo $commit &&
		git ls-tree $tree | sed -e "s/.* \\([0-9a-f]*\\)	.*/\\1/"
	} >obj-list
'

rm -rf clone.git
test_expect_success 'pack without --include-tag' '
	packname_1=$(git pack-objects \
		--window=0 \
		test-1 <obj-list)
'

test_expect_success 'unpack objects' '
	(
		GIT_DIR=clone.git &&
		export GIT_DIR &&
		git init &&
		git unpack-objects -n <test-1-${packname_1}.pack &&
		git unpack-objects <test-1-${packname_1}.pack
	)
'

test_expect_success 'check unpacked result (have commit, no tag)' '
	git rev-list --objects $commit >list.expect &&
	(
		GIT_DIR=clone.git &&
		export GIT_DIR &&
		test_must_fail git cat-file -e $tag &&
		git rev-list --objects $commit
	) >list.actual &&
	test_cmp list.expect list.actual
'

rm -rf clone.git
test_expect_success 'pack with --include-tag' '
	packname_1=$(git pack-objects \
		--window=0 \
		--include-tag \
		test-2 <obj-list)
'

test_expect_success 'unpack objects' '
	(
		GIT_DIR=clone.git &&
		export GIT_DIR &&
		git init &&
		git unpack-objects -n <test-2-${packname_1}.pack &&
		git unpack-objects <test-2-${packname_1}.pack
	)
'

test_expect_success 'check unpacked result (have commit, have tag)' '
	git rev-list --objects mytag >list.expect &&
	(
		GIT_DIR=clone.git &&
		export GIT_DIR &&
		git rev-list --objects $tag
	) >list.actual &&
	test_cmp list.expect list.actual
'

test_done
