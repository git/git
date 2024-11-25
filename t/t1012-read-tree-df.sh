#!/bin/sh

test_description='read-tree D/F conflict corner cases'

. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-read-tree.sh

maketree () {
	(
		rm -f .git/index .git/index.lock &&
		git clean -d -f -f -q -x &&
		name="$1" &&
		shift &&
		for it
		do
			path=$(expr "$it" : '\([^:]*\)') &&
			mkdir -p $(dirname "$path") &&
			echo "$it" >"$path" &&
			git update-index --add "$path" || exit
		done &&
		git tag "$name" $(git write-tree)
	)
}

settree () {
	rm -f .git/index .git/index.lock &&
	git clean -d -f -f -q -x &&
	git read-tree "$1" &&
	git checkout-index -f -q -u -a &&
	git update-index --refresh
}

checkindex () {
	git ls-files -s |
	sed "s|^[0-7][0-7]* $OID_REGEX \([0-3]\)	|\1 |" >current &&
	cat >expect &&
	test_cmp expect current
}

test_expect_success setup '
	maketree O-000 a/b-2/c/d a/b/c/d a/x &&
	maketree A-000 a/b-2/c/d a/b/c/d a/x &&
	maketree A-001 a/b-2/c/d a/b/c/d a/b/c/e a/x &&
	maketree B-000 a/b-2/c/d a/b     a/x &&

	maketree O-010 t-0     t/1  t/2 t=3 &&
	maketree A-010 t-0 t            t=3 &&
	maketree B-010         t/1:     t=3: &&

	maketree O-020 ds/dma/ioat.c ds/dma/ioat_dca.c &&
	maketree A-020 ds/dma/ioat/Makefile ds/dma/ioat/registers.h &&
	:
'

test_expect_success '3-way (1)' '
	settree A-000 &&
	read_tree_u_must_succeed -m -u O-000 A-000 B-000 &&
	checkindex <<-EOF
	3 a/b
	0 a/b-2/c/d
	1 a/b/c/d
	2 a/b/c/d
	0 a/x
	EOF
'

test_expect_success '3-way (2)' '
	settree A-001 &&
	read_tree_u_must_succeed -m -u O-000 A-001 B-000 &&
	checkindex <<-EOF
	3 a/b
	0 a/b-2/c/d
	1 a/b/c/d
	2 a/b/c/d
	2 a/b/c/e
	0 a/x
	EOF
'

test_expect_success '3-way (3)' '
	settree A-010 &&
	read_tree_u_must_succeed -m -u O-010 A-010 B-010 &&
	checkindex <<-EOF
	2 t
	1 t-0
	2 t-0
	1 t/1
	3 t/1
	1 t/2
	0 t=3
	EOF
'

test_expect_success '2-way (1)' '
	settree O-020 &&
	read_tree_u_must_succeed -m -u O-020 A-020 &&
	checkindex <<-EOF
	0 ds/dma/ioat/Makefile
	0 ds/dma/ioat/registers.h
	EOF
'

test_done
