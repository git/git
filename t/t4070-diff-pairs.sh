#!/bin/sh

test_description='basic diff-pairs tests'
. ./test-lib.sh

# This creates a diff with added, modified, deleted, renamed, copied, and
# typechange entries. That includes one in a subdirectory for non-recursive
# tests, and both exact and inexact similarity scores.
test_expect_success 'create commit with various diffs' '
	echo to-be-gone >deleted &&
	echo original >modified &&
	echo now-a-file >symlink &&
	test_seq 200 >two-hundred &&
	test_seq 201 500 >five-hundred &&
	git add . &&
	test_tick &&
	git commit -m base &&
	git tag base &&

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
	git diff-tree -r -M -C -C base new >expect &&
	git diff-tree -r -M -C -C -z base new |
	git diff-pairs >actual &&
	test_cmp expect actual
'

test_expect_success 'diff-pairs can create -p output' '
	git diff-tree -p -M -C -C base new >expect &&
	git diff-tree -r -M -C -C -z base new |
	git diff-pairs -p >actual &&
	test_cmp expect actual
'

test_expect_success 'non-recursive --raw retains tree entry' '
	git diff-tree base new >expect &&
	git diff-tree -z base new |
	git diff-pairs >actual &&
	test_cmp expect actual
'

test_expect_success 'split input across multiple diff-pairs' '
	write_script split-raw-diff "$PERL_PATH" <<-\EOF &&
	$/ = "\0";
	while (<>) {
	  my $meta = $_;
	  my $path = <>;
	  # renames have an extra path
	  my $path2 = <> if $meta =~ /[RC]\d+/;

	  open(my $fh, ">", sprintf "diff%03d", $.);
	  print $fh $meta, $path, $path2;
	}
	EOF

	git diff-tree -p -M -C -C base new >expect &&

	git diff-tree -r -z -M -C -C base new |
	./split-raw-diff &&
	for i in diff*; do
		git diff-pairs -p <$i || return 1
	done >actual &&
	test_cmp expect actual
'

test_expect_success 'diff-pairs explicit queue flush' '
	git diff-tree -r -M -C -C -z base new >input &&
	printf "\0" >>input &&
	git diff-tree -r -M -C -C -z base new >>input &&

	git diff-tree -r -M -C -C base new >expect &&
	printf "\n" >>expect &&
	git diff-tree -r -M -C -C base new >>expect &&

	git diff-pairs <input >actual &&
	test_cmp expect actual
'
j
test_expect_success 'diff-pairs explicit queue flush null terminated' '
	git diff-tree -r -M -C -C -z base new >expect &&
	printf "\0" >>expect &&
	git diff-tree -r -M -C -C -z base new >>expect &&

	git diff-pairs -z <expect >actual &&
	test_cmp expect actual
'

test_done
