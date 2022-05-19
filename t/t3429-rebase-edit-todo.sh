#!/bin/sh

test_description='rebase should reread the todo file if an exec modifies it'

. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-rebase.sh

test_expect_success 'setup' '
	test_cummit first file &&
	test_cummit second file &&
	test_cummit third file
'

test_expect_success 'rebase exec modifies rebase-todo' '
	todo=.but/rebase-merge/but-rebase-todo &&
	but rebase HEAD~1 -x "echo exec touch F >>$todo" &&
	test -e F
'

test_expect_success 'rebase exec with an empty list does not exec anything' '
	but rebase HEAD -x "true" 2>output &&
	! grep "Executing: true" output
'

test_expect_success 'loose object cache vs re-reading todo list' '
	BUT_REBASE_TODO=.but/rebase-merge/but-rebase-todo &&
	export BUT_REBASE_TODO &&
	write_script append-todo.sh <<-\EOS &&
	# For values 5 and 6, this yields SHA-1s with the same first two dibuts
	echo "pick $(but rev-parse --short \
		$(printf "%s\\n" \
			"tree $EMPTY_TREE" \
			"author A U Thor <author@example.org> $1 +0000" \
			"cummitter A U Thor <author@example.org> $1 +0000" \
			"" \
			"$1" |
		  but hash-object -t cummit -w --stdin))" >>$BUT_REBASE_TODO

	shift
	test -z "$*" ||
	echo "exec $0 $*" >>$BUT_REBASE_TODO
	EOS

	but rebase HEAD -x "./append-todo.sh 5 6"
'

test_expect_success 'todo is re-read after reword and squash' '
	write_script reword-editor.sh <<-\EOS &&
	BUT_SEQUENCE_EDITOR="echo \"exec echo $(cat file) >>actual\" >>" \
		but rebase --edit-todo
	EOS

	test_write_lines first third >expected &&
	set_fake_editor &&
	BUT_SEQUENCE_EDITOR="$EDITOR" FAKE_LINES="reword 1 squash 2 fixup 3" \
		BUT_EDITOR=./reword-editor.sh but rebase -i --root third &&
	test_cmp expected actual
'

test_expect_success 're-reading todo doesnt interfere with revert --edit' '
	but reset --hard third &&

	but revert --edit third second &&

	cat >expect <<-\EOF &&
	Revert "second"
	Revert "third"
	third
	second
	first
	EOF
	but log --format="%s" >actual &&
	test_cmp expect actual
'

test_expect_success 're-reading todo doesnt interfere with cherry-pick --edit' '
	but reset --hard first &&

	but cherry-pick --edit second third &&

	cat >expect <<-\EOF &&
	third
	second
	first
	EOF
	but log --format="%s" >actual &&
	test_cmp expect actual
'

test_done
