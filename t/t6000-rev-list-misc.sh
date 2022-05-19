#!/bin/sh

test_description='miscellaneous rev-list tests'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

test_expect_success setup '
	echo content1 >wanted_file &&
	echo content2 >unwanted_file &&
	but add wanted_file unwanted_file &&
	test_tick &&
	but cummit -m one
'

test_expect_success 'rev-list --objects heeds pathspecs' '
	but rev-list --objects HEAD -- wanted_file >output &&
	grep wanted_file output &&
	! grep unwanted_file output
'

test_expect_success 'rev-list --objects with pathspecs and deeper paths' '
	mkdir foo &&
	>foo/file &&
	but add foo/file &&
	test_tick &&
	but cummit -m two &&

	but rev-list --objects HEAD -- foo >output &&
	grep foo/file output &&

	but rev-list --objects HEAD -- foo/file >output &&
	grep foo/file output &&
	! grep unwanted_file output
'

test_expect_success 'rev-list --objects with pathspecs and copied files' '
	but checkout --orphan junio-testcase &&
	but rm -rf . &&

	mkdir two &&
	echo frotz >one &&
	cp one two/three &&
	but add one two/three &&
	test_tick &&
	but cummit -m that &&

	ONE=$(but rev-parse HEAD:one) &&
	but rev-list --objects HEAD two >output &&
	grep "$ONE two/three" output &&
	! grep one output
'

test_expect_success 'rev-list --objects --no-object-names has no space/names' '
	but rev-list --objects --no-object-names HEAD >output &&
	! grep wanted_file output &&
	! grep unwanted_file output &&
	! grep " " output
'

test_expect_success 'rev-list --objects --no-object-names works with cat-file' '
	but rev-list --objects --no-object-names --all >list-output &&
	but cat-file --batch-check <list-output >cat-output &&
	! grep missing cat-output
'

test_expect_success '--no-object-names and --object-names are last-one-wins' '
	but rev-list --objects --no-object-names --object-names --all >output &&
	grep wanted_file output &&
	but rev-list --objects --object-names --no-object-names --all >output &&
	! grep wanted_file output
'

test_expect_success 'rev-list A..B and rev-list ^A B are the same' '
	test_tick &&
	but cummit --allow-empty -m another &&
	but tag -a -m "annotated" v1.0 &&
	but rev-list --objects ^v1.0^ v1.0 >expect &&
	but rev-list --objects v1.0^..v1.0 >actual &&
	test_cmp expect actual
'

test_expect_success 'propagate uninteresting flag down correctly' '
	but rev-list --objects ^HEAD^{tree} HEAD^{tree} >actual &&
	test_must_be_empty actual
'

test_expect_success 'symleft flag bit is propagated down from tag' '
	but log --format="%m %s" --left-right v1.0...main >actual &&
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
	#     from the last cummit
	#
	#   - we do not show the root tree; since we updated the index, it
	#     does not have a valid cache tree
	#
	echo only-in-index >only-in-index &&
	test_when_finished "but reset --hard" &&
	rev1=$(but rev-parse HEAD:one) &&
	rev2=$(but rev-parse HEAD:two) &&
	revi=$(but hash-object only-in-index) &&
	cat >expect <<-EOF &&
	$rev1 one
	$revi only-in-index
	$rev2 two
	EOF
	but add only-in-index &&
	but rev-list --objects --indexed-objects >actual &&
	test_cmp expect actual
'

test_expect_success 'rev-list can negate index objects' '
	but rev-parse HEAD >expect &&
	but rev-list -1 --objects HEAD --not --indexed-objects >actual &&
	test_cmp expect actual
'

test_expect_success '--bisect and --first-parent can be combined' '
	but rev-list --bisect --first-parent HEAD
'

test_expect_success '--header shows a NUL after each cummit' '
	# We know that there is no Q in the true payload; names and
	# addresses of the authors and the cummitters do not have
	# any, and object names or header names do not, either.
	but rev-list --header --max-count=2 HEAD |
	nul_to_q |
	grep "^Q" >actual &&
	cat >expect <<-EOF &&
	Q$(but rev-parse HEAD~1)
	Q
	EOF
	test_cmp expect actual
'

test_expect_success 'rev-list --end-of-options' '
	but update-ref refs/heads/--output=yikes HEAD &&
	but rev-list --end-of-options --output=yikes >actual &&
	test_path_is_missing yikes &&
	but rev-list HEAD >expect &&
	test_cmp expect actual
'

test_expect_success 'rev-list --count' '
	count=$(but rev-list --count HEAD) &&
	but rev-list HEAD >actual &&
	test_line_count = $count actual
'

test_expect_success 'rev-list --count --objects' '
	count=$(but rev-list --count --objects HEAD) &&
	but rev-list --objects HEAD >actual &&
	test_line_count = $count actual
'

test_done
