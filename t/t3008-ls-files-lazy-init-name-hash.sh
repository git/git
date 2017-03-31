#!/bin/sh

test_description='Test the lazy init name hash with various folder structures'

. ./test-lib.sh

test_expect_success 'no buffer overflow in lazy_init_name_hash' '
	(
	    test_seq 2000 | sed "s/^/a_/"
	    echo b/b/b
	    test_seq 2000 | sed "s/^/c_/"
	    test_seq 50 | sed "s/^/d_/" | tr "\n" "/"; echo d
	) |
	sed -e "s/^/100644 $EMPTY_BLOB	/" |
	git update-index --index-info &&
	test-lazy-init-name-hash -m
'

test_done
