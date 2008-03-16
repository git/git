#!/bin/sh

test_description=gitattributes

. ./test-lib.sh

attr_check () {

	path="$1"
	expect="$2"

	git check-attr test -- "$path" >actual &&
	echo "$path: test: $2" >expect &&
	test_cmp expect actual

}


test_expect_success 'setup' '

	mkdir -p a/b/d a/c &&
	(
		echo "f	test=f"
	) >.gitattributes &&
	(
		echo "g test=a/g" &&
		echo "b/g test=a/b/g"
	) >a/.gitattributes &&
	(
		echo "h test=a/b/h" &&
		echo "d/* test=a/b/d/*"
	) >a/b/.gitattributes

'

test_expect_success 'attribute test' '

	attr_check f f &&
	attr_check a/f f &&
	attr_check a/c/f f &&
	attr_check a/g a/g &&
	attr_check a/b/g a/b/g &&
	attr_check b/g unspecified &&
	attr_check a/b/h a/b/h &&
	attr_check a/b/d/g "a/b/d/*"

'

test_done
