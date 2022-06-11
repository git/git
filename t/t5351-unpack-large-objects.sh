#!/bin/sh
#
# Copyright (c) 2022 Han Xin
#

test_description='git unpack-objects with large objects'

. ./test-lib.sh

prepare_dest () {
	test_when_finished "rm -rf dest.git" &&
	git init --bare dest.git
}

test_expect_success "create large objects (1.5 MB) and PACK" '
	test-tool genrandom foo 1500000 >big-blob &&
	test_commit --append foo big-blob &&
	test-tool genrandom bar 1500000 >big-blob &&
	test_commit --append bar big-blob &&
	PACK=$(echo HEAD | git pack-objects --revs pack)
'

test_expect_success 'set memory limitation to 1MB' '
	GIT_ALLOC_LIMIT=1m &&
	export GIT_ALLOC_LIMIT
'

test_expect_success 'unpack-objects failed under memory limitation' '
	prepare_dest &&
	test_must_fail git -C dest.git unpack-objects <pack-$PACK.pack 2>err &&
	grep "fatal: attempting to allocate" err
'

test_expect_success 'unpack-objects works with memory limitation in dry-run mode' '
	prepare_dest &&
	git -C dest.git unpack-objects -n <pack-$PACK.pack &&
	test_stdout_line_count = 0 find dest.git/objects -type f &&
	test_dir_is_empty dest.git/objects/pack
'

test_done
