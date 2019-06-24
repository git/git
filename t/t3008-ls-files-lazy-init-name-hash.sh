#!/bin/sh

test_description='Test the lazy init name hash with various folder structures'

. ./test-lib.sh

LAZY_THREAD_COST=2000

test_expect_success !SINGLE_CPU 'no buffer overflow in lazy_init_name_hash' '
	(
	    test_seq $LAZY_THREAD_COST | sed "s/^/a_/" &&
	    echo b/b/b &&
	    test_seq $LAZY_THREAD_COST | sed "s/^/c_/" &&
	    test_seq 50 | sed "s/^/d_/" | tr "\n" "/" && echo d
	) |
	sed "s/^/100644 $EMPTY_BLOB	/" |
	git update-index --index-info &&
	test-tool lazy-init-name-hash -m
'

test_done
