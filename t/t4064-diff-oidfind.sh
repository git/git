#!/bin/sh

test_description='test finding specific blobs in the revision walking'
. ./test-lib.sh

test_expect_success 'setup ' '
	but cummit --allow-empty -m "empty initial cummit" &&

	echo "Hello, world!" >greeting &&
	but add greeting &&
	but cummit -m "add the greeting blob" && # borrowed from Git from the Bottom Up
	but tag -m "the blob" greeting $(but rev-parse HEAD:greeting) &&

	echo asdf >unrelated &&
	but add unrelated &&
	but cummit -m "unrelated history" &&

	but revert HEAD^ &&

	but cummit --allow-empty -m "another unrelated cummit"
'

test_expect_success 'find the greeting blob' '
	cat >expect <<-EOF &&
	Revert "add the greeting blob"
	add the greeting blob
	EOF

	but log --format=%s --find-object=greeting^{blob} >actual &&

	test_cmp expect actual
'

test_expect_success 'setup a tree' '
	mkdir a &&
	echo asdf >a/file &&
	but add a/file &&
	but cummit -m "add a file in a subdirectory"
'

test_expect_success 'find a tree' '
	cat >expect <<-EOF &&
	add a file in a subdirectory
	EOF

	but log --format=%s -t --find-object=HEAD:a >actual &&

	test_cmp expect actual
'

test_expect_success 'setup a submodule' '
	test_create_repo sub &&
	test_cummit -C sub sub &&
	but submodule add ./sub sub &&
	but cummit -a -m "add sub"
'

test_expect_success 'find a submodule' '
	cat >expect <<-EOF &&
	add sub
	EOF

	but log --format=%s --find-object=HEAD:sub >actual &&

	test_cmp expect actual
'

test_expect_success 'set up merge tests' '
	test_cummit base &&

	but checkout -b boring base^ &&
	echo boring >file &&
	but add file &&
	but cummit -m boring &&

	but checkout -b interesting base^ &&
	echo interesting >file &&
	but add file &&
	but cummit -m interesting &&

	blob=$(but rev-parse interesting:file)
'

test_expect_success 'detect merge which introduces blob' '
	but checkout -B merge base &&
	but merge --no-cummit boring &&
	echo interesting >file &&
	but cummit -am "introduce blob" &&
	but diff-tree --format=%s --find-object=$blob -c --name-status HEAD >actual &&
	cat >expect <<-\EOF &&
	introduce blob

	AM	file
	EOF
	test_cmp expect actual
'

test_expect_success 'detect merge which removes blob' '
	but checkout -B merge interesting &&
	but merge --no-cummit base &&
	echo boring >file &&
	but cummit -am "remove blob" &&
	but diff-tree --format=%s --find-object=$blob -c --name-status HEAD >actual &&
	cat >expect <<-\EOF &&
	remove blob

	MA	file
	EOF
	test_cmp expect actual
'

test_expect_success 'do not detect merge that does not touch blob' '
	but checkout -B merge interesting &&
	but merge -m "untouched blob" base &&
	but diff-tree --format=%s --find-object=$blob -c --name-status HEAD >actual &&
	cat >expect <<-\EOF &&
	untouched blob

	EOF
	test_cmp expect actual
'

test_done
