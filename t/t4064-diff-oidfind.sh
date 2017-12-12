#!/bin/sh

test_description='test finding specific blobs in the revision walking'
. ./test-lib.sh

test_expect_success 'setup ' '
	git commit --allow-empty -m "empty initial commit" &&

	echo "Hello, world!" >greeting &&
	git add greeting &&
	git commit -m "add the greeting blob" && # borrowed from Git from the Bottom Up
	git tag -m "the blob" greeting $(git rev-parse HEAD:greeting) &&

	echo asdf >unrelated &&
	git add unrelated &&
	git commit -m "unrelated history" &&

	git revert HEAD^ &&

	git commit --allow-empty -m "another unrelated commit"
'

test_expect_success 'find the greeting blob' '
	cat >expect <<-EOF &&
	Revert "add the greeting blob"
	add the greeting blob
	EOF

	git log --format=%s --find-object=greeting^{blob} >actual &&

	test_cmp expect actual
'

test_expect_success 'setup a tree' '
	mkdir a &&
	echo asdf >a/file &&
	git add a/file &&
	git commit -m "add a file in a subdirectory"
'

test_expect_success 'find a tree' '
	cat >expect <<-EOF &&
	add a file in a subdirectory
	EOF

	git log --format=%s -t --find-object=HEAD:a >actual &&

	test_cmp expect actual
'

test_expect_success 'setup a submodule' '
	test_create_repo sub &&
	test_commit -C sub sub &&
	git submodule add ./sub sub &&
	git commit -a -m "add sub"
'

test_expect_success 'find a submodule' '
	cat >expect <<-EOF &&
	add sub
	EOF

	git log --format=%s --find-object=HEAD:sub >actual &&

	test_cmp expect actual
'

test_done
