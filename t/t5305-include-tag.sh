#!/bin/sh

test_description='but pack-object --include-tag'
GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

TRASH=$(pwd)

test_expect_success setup '
	echo c >d &&
	but update-index --add d &&
	tree=$(but write-tree) &&
	cummit=$(but cummit-tree $tree </dev/null) &&
	echo "object $cummit" >sig &&
	echo "type cummit" >>sig &&
	echo "tag mytag" >>sig &&
	echo "tagger $(but var GIT_CUMMITTER_IDENT)" >>sig &&
	echo >>sig &&
	echo "our test tag" >>sig &&
	tag=$(but mktag <sig) &&
	rm d sig &&
	but update-ref refs/tags/mytag $tag && {
		echo $tree &&
		echo $cummit &&
		but ls-tree $tree | sed -e "s/.* \\([0-9a-f]*\\)	.*/\\1/"
	} >obj-list
'

test_expect_success 'pack without --include-tag' '
	packname=$(but pack-objects \
		--window=0 \
		test-no-include <obj-list)
'

test_expect_success 'unpack objects' '
	rm -rf clone.but &&
	but init clone.but &&
	but -C clone.but unpack-objects <test-no-include-${packname}.pack
'

test_expect_success 'check unpacked result (have cummit, no tag)' '
	but rev-list --objects $cummit >list.expect &&
	test_must_fail but -C clone.but cat-file -e $tag &&
	but -C clone.but rev-list --objects $cummit >list.actual &&
	test_cmp list.expect list.actual
'

test_expect_success 'pack with --include-tag' '
	packname=$(but pack-objects \
		--window=0 \
		--include-tag \
		test-include <obj-list)
'

test_expect_success 'unpack objects' '
	rm -rf clone.but &&
	but init clone.but &&
	but -C clone.but unpack-objects <test-include-${packname}.pack
'

test_expect_success 'check unpacked result (have cummit, have tag)' '
	but rev-list --objects mytag >list.expect &&
	but -C clone.but rev-list --objects $tag >list.actual &&
	test_cmp list.expect list.actual
'

# A tag of a tag, where the "inner" tag is not otherwise
# reachable, and a full peel points to a cummit reachable from HEAD.
test_expect_success 'create hidden inner tag' '
	test_cummit cummit &&
	but tag -m inner inner HEAD &&
	but tag -m outer outer inner &&
	but tag -d inner
'

test_expect_success 'pack explicit outer tag' '
	packname=$(
		{
			echo HEAD &&
			echo outer
		} |
		but pack-objects --revs test-hidden-explicit
	)
'

test_expect_success 'unpack objects' '
	rm -rf clone.but &&
	but init clone.but &&
	but -C clone.but unpack-objects <test-hidden-explicit-${packname}.pack
'

test_expect_success 'check unpacked result (have all objects)' '
	but -C clone.but rev-list --objects $(but rev-parse outer HEAD)
'

test_expect_success 'pack implied outer tag' '
	packname=$(
		echo HEAD |
		but pack-objects --revs --include-tag test-hidden-implied
	)
'

test_expect_success 'unpack objects' '
	rm -rf clone.but &&
	but init clone.but &&
	but -C clone.but unpack-objects <test-hidden-implied-${packname}.pack
'

test_expect_success 'check unpacked result (have all objects)' '
	but -C clone.but rev-list --objects $(but rev-parse outer HEAD)
'

test_expect_success 'single-branch clone can transfer tag' '
	rm -rf clone.but &&
	but clone --no-local --single-branch -b main . clone.but &&
	but -C clone.but fsck
'

test_done
