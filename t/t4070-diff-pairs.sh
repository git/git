#!/bin/sh

test_description='basic diff-pairs tests'
. ./test-lib.sh

# This creates a diff with added, modified, deleted, renamed, copied, and
# typechange entries. This includes a submodule to test submodule diff support.
test_expect_success 'setup' '
	test_config_global protocol.file.allow always &&
	git init sub &&
	test_commit -C sub initial &&

	git init main &&
	cd main &&
	echo to-be-gone >deleted &&
	echo original >modified &&
	echo now-a-file >symlink &&
	test_seq 200 >two-hundred &&
	test_seq 201 500 >five-hundred &&
	git add . &&
	test_tick &&
	git commit -m base &&
	git tag base &&

	git submodule add ../sub &&
	echo now-here >added &&
	echo new >modified &&
	rm deleted &&
	mkdir subdir &&
	echo content >subdir/file &&
	mv two-hundred renamed &&
	test_seq 201 500 | sed s/300/modified/ >copied &&
	rm symlink &&
	git add -A . &&
	test_ln_s_add dest symlink &&
	test_tick &&
	git commit -m new &&
	git tag new
'

test_expect_success 'diff-pairs recreates --raw' '
	git diff-tree -r -M -C -C -z base new >expect &&
	git diff-pairs --raw -z >actual <expect &&
	test_cmp expect actual
'

test_expect_success 'diff-pairs can create -p output' '
	git diff-tree -p -M -C -C base new >expect &&
	git diff-tree -r -M -C -C -z base new |
	git diff-pairs -p -z >actual &&
	test_cmp expect actual
'

test_expect_success 'diff-pairs does not support normal raw diff input' '
	git diff-tree -r base new |
	test_must_fail git diff-pairs >out 2>err &&

	echo "usage: working without -z is not supported" >expect &&
	test_must_be_empty out &&
	test_cmp expect err
'

test_expect_success 'diff-pairs does not support tree objects as input' '
	git diff-tree -z base new |
	test_must_fail git diff-pairs -z >out 2>err &&

	echo "fatal: tree objects not supported" >expect &&
	test_must_be_empty out &&
	test_cmp expect err
'

test_expect_success 'diff-pairs does not support pathspec arguments' '
	git diff-tree -r -z base new |
	test_must_fail git diff-pairs -z -- new >out 2>err &&

	echo "usage: pathspec arguments not supported" >expect &&
	test_must_be_empty out &&
	test_cmp expect err
'

test_expect_success 'diff-pairs explicit queue flush' '
	git diff-tree -r -M -C -C -z base new >expect &&
	printf "\0" >>expect &&
	git diff-tree -r -M -C -C -z base new >>expect &&

	git diff-pairs --raw -z <expect >actual &&
	test_cmp expect actual
'

test_done
