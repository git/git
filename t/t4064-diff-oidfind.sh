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

test_expect_success 'set up merge tests' '
	test_commit base &&

	git checkout -b boring base^ &&
	echo boring >file &&
	git add file &&
	git commit -m boring &&

	git checkout -b interesting base^ &&
	echo interesting >file &&
	git add file &&
	git commit -m interesting &&

	blob=$(git rev-parse interesting:file)
'

test_expect_success 'detect merge which introduces blob' '
	git checkout -B merge base &&
	git merge --no-commit boring &&
	echo interesting >file &&
	git commit -am "introduce blob" &&
	git diff-tree --format=%s --find-object=$blob -c --name-status HEAD >actual &&
	cat >expect <<-\EOF &&
	introduce blob

	AM	file
	EOF
	test_cmp expect actual
'

test_expect_success 'detect merge which removes blob' '
	git checkout -B merge interesting &&
	git merge --no-commit base &&
	echo boring >file &&
	git commit -am "remove blob" &&
	git diff-tree --format=%s --find-object=$blob -c --name-status HEAD >actual &&
	cat >expect <<-\EOF &&
	remove blob

	MA	file
	EOF
	test_cmp expect actual
'

test_expect_success 'do not detect merge that does not touch blob' '
	git checkout -B merge interesting &&
	git merge -m "untouched blob" base &&
	git diff-tree --format=%s --find-object=$blob -c --name-status HEAD >actual &&
	cat >expect <<-\EOF &&
	untouched blob

	EOF
	test_cmp expect actual
'

test_done
