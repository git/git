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

for i in 1 2
do
	test_expect_success "orderfile using option ($i)" '
		git diff -Oorder_file_$i --name-only HEAD^..HEAD >actual &&
		test_cmp expect_$i actual
	'
done

test_done
