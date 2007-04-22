#!/bin/sh

test_description='blob conversion via gitattributes'

. ./test-lib.sh

test_expect_success setup '
	{
	    echo "*.i ident"
	} >.gitattributes &&

	{
	    echo a b c d e f g h i j k l m
	    echo n o p q r s t u v w x y z
	    echo '\''$ident$'\''
	} >test &&
	cat test >test.t &&
	cat test >test.o &&
	cat test >test.i &&
	git add test test.t test.i &&
	rm -f test test.t test.i &&
	git checkout -- test test.t test.i
'

script='s/^\$ident: \([0-9a-f]*\) \$/\1/p'

test_expect_success check '

	cmp test.o test &&
	cmp test.o test.t &&

	# ident should be stripped in the repository
	git diff --raw --exit-code :test :test.i &&
	id=$(git rev-parse --verify :test) &&
	embedded=$(sed -ne "$script" test.i) &&
	test "z$id" = "z$embedded"
'

test_done
