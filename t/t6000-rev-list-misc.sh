#!/bin/sh

test_description='miscellaneous rev-list tests'

. ./test-lib.sh

test_expect_success setup '
	echo content1 >wanted_file &&
	echo content2 >unwanted_file &&
	git add wanted_file unwanted_file &&
	test_tick &&
	git commit -m one
'

test_expect_success 'rev-list --objects heeds pathspecs' '
	git rev-list --objects HEAD -- wanted_file >output &&
	grep wanted_file output &&
	! grep unwanted_file output
'

test_expect_success 'rev-list --objects with pathspecs and deeper paths' '
	mkdir foo &&
	>foo/file &&
	git add foo/file &&
	test_tick &&
	git commit -m two &&

	git rev-list --objects HEAD -- foo >output &&
	grep foo/file output &&

	git rev-list --objects HEAD -- foo/file >output &&
	grep foo/file output &&
	! grep unwanted_file output
'

test_expect_success 'rev-list --objects with pathspecs and copied files' '
	git checkout --orphan junio-testcase &&
	git rm -rf . &&

	mkdir two &&
	echo frotz >one &&
	cp one two/three &&
	git add one two/three &&
	test_tick &&
	git commit -m that &&

	ONE=$(git rev-parse HEAD:one) &&
	git rev-list --objects HEAD two >output &&
	grep "$ONE two/three" output &&
	! grep one output
'

test_expect_success 'rev-list --objects --no-object-names has no space/names' '
	git rev-list --objects --no-object-names HEAD >output &&
	! grep wanted_file output &&
	! grep unwanted_file output &&
	! grep " " output
'

test_expect_success 'rev-list --objects --no-object-names works with cat-file' '
	git rev-list --objects --no-object-names --all >list-output &&
	git cat-file --batch-check <list-output >cat-output &&
	! grep missing cat-output
'

test_expect_success '--no-object-names and --object-names are last-one-wins' '
	git rev-list --objects --no-object-names --object-names --all >output &&
	grep wanted_file output &&
	git rev-list --objects --object-names --no-object-names --all >output &&
	! grep wanted_file output
'

test_expect_success 'rev-list A..B and rev-list ^A B are the same' '
	test_tick &&
	git commit --allow-empty -m another &&
	git tag -a -m "annotated" v1.0 &&
	git rev-list --objects ^v1.0^ v1.0 >expect &&
	git rev-list --objects v1.0^..v1.0 >actual &&
	test_cmp expect actual
'

test_expect_success 'propagate uninteresting flag down correctly' '
	git rev-list --objects ^HEAD^{tree} HEAD^{tree} >actual &&
	test_must_be_empty actual
'

test_expect_success 'symleft flag bit is propagated down from tag' '
	git log --format="%m %s" --left-right v1.0...master >actual &&
	cat >expect <<-\EOF &&
	< another
	< that
	> two
	> one
	EOF
	test_cmp expect actual
'

test_expect_success 'rev-list can show index objects' '
	# Of the blobs and trees in the index, note:
	#
	#   - we do not show two/three, because it is the
	#     same blob as "one", and we show objects only once
	#
	#   - we do show the tree "two", because it has a valid cache tree
	#     from the last commit
	#
	#   - we do not show the root tree; since we updated the index, it
	#     does not have a valid cache tree
	#
	echo only-in-index >only-in-index &&
	test_when_finished "git reset --hard" &&
	rev1=$(git rev-parse HEAD:one) &&
	rev2=$(git rev-parse HEAD:two) &&
	revi=$(git hash-object only-in-index) &&
	cat >expect <<-EOF &&
	$rev1 one
	$revi only-in-index
	$rev2 two
	EOF
	git add only-in-index &&
	git rev-list --objects --indexed-objects >actual &&
	test_cmp expect actual
'

test_expect_success 'rev-list can negate index objects' '
	git rev-parse HEAD >expect &&
	git rev-list -1 --objects HEAD --not --indexed-objects >actual &&
	test_cmp expect actual
'

test_expect_success '--bisect and --first-parent can be combined' '
	git rev-list --bisect --first-parent HEAD
'

test_expect_success '--header shows a NUL after each commit' '
	# We know that there is no Q in the true payload; names and
	# addresses of the authors and the committers do not have
	# any, and object names or header names do not, either.
	git rev-list --header --max-count=2 HEAD |
	nul_to_q |
	grep "^Q" >actual &&
	cat >expect <<-EOF &&
	Q$(git rev-parse HEAD~1)
	Q
	EOF
	test_cmp expect actual
'

test_expect_success 'rev-list --end-of-options' '
	git update-ref refs/heads/--output=yikes HEAD &&
	git rev-list --end-of-options --output=yikes >actual &&
	test_path_is_missing yikes &&
	git rev-list HEAD >expect &&
	test_cmp expect actual
'

test_expect_success 'rev-list --count' '
	count=$(git rev-list --count HEAD) &&
	git rev-list HEAD >actual &&
	test_line_count = $count actual
'

test_expect_success 'rev-list --count --objects' '
	count=$(git rev-list --count --objects HEAD) &&
	git rev-list --objects HEAD >actual &&
	test_line_count = $count actual
'

test_done
