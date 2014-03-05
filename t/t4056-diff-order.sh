#!/bin/sh

test_description='diff order'

. ./test-lib.sh

create_files () {
	echo "$1" >a.h &&
	echo "$1" >b.c &&
	echo "$1" >c/Makefile &&
	echo "$1" >d.txt &&
	git add a.h b.c c/Makefile d.txt &&
	git commit -m"$1"
}

test_expect_success 'setup' '
	mkdir c &&
	create_files 1 &&
	create_files 2 &&

	cat >order_file_1 <<-\EOF &&
	*Makefile
	*.txt
	*.h
	EOF

	cat >order_file_2 <<-\EOF &&
	*Makefile
	*.h
	*.c
	EOF

	cat >expect_none <<-\EOF &&
	a.h
	b.c
	c/Makefile
	d.txt
	EOF

	cat >expect_1 <<-\EOF &&
	c/Makefile
	d.txt
	a.h
	b.c
	EOF

	cat >expect_2 <<-\EOF
	c/Makefile
	a.h
	b.c
	d.txt
	EOF
'

test_expect_success "no order (=tree object order)" '
	git diff --name-only HEAD^..HEAD >actual &&
	test_cmp expect_none actual
'

test_expect_success 'missing orderfile' '
	rm -f bogus_file &&
	test_must_fail git diff -Obogus_file --name-only HEAD^..HEAD
'

test_expect_success POSIXPERM,SANITY 'unreadable orderfile' '
	>unreadable_file &&
	chmod -r unreadable_file &&
	test_must_fail git diff -Ounreadable_file --name-only HEAD^..HEAD
'

for i in 1 2
do
	test_expect_success "orderfile using option ($i)" '
		git diff -Oorder_file_$i --name-only HEAD^..HEAD >actual &&
		test_cmp expect_$i actual
	'

	test_expect_success PIPE "orderfile is fifo ($i)" '
		rm -f order_fifo &&
		mkfifo order_fifo &&
		{
			cat order_file_$i >order_fifo &
		} &&
		git diff -O order_fifo --name-only HEAD^..HEAD >actual &&
		wait &&
		test_cmp expect_$i actual
	'

	test_expect_success "orderfile using config ($i)" '
		git -c diff.orderfile=order_file_$i diff --name-only HEAD^..HEAD >actual &&
		test_cmp expect_$i actual
	'

	test_expect_success "cancelling configured orderfile ($i)" '
		git -c diff.orderfile=order_file_$i diff -O/dev/null --name-only HEAD^..HEAD >actual &&
		test_cmp expect_none actual
	'
done

test_expect_success 'setup for testing combine-diff order' '
	git checkout -b tmp HEAD~ &&
	create_files 3 &&
	git checkout master &&
	git merge --no-commit -s ours tmp &&
	create_files 5
'

test_expect_success "combine-diff: no order (=tree object order)" '
	git diff --name-only HEAD HEAD^ HEAD^2 >actual &&
	test_cmp expect_none actual
'

for i in 1 2
do
	test_expect_success "combine-diff: orderfile using option ($i)" '
		git diff -Oorder_file_$i --name-only HEAD HEAD^ HEAD^2 >actual &&
		test_cmp expect_$i actual
	'
done

test_done
