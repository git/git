#!/bin/sh

test_description=gitattributes

. ./test-lib.sh

attr_check () {

	path="$1"
	expect="$2"

	git check-attr test -- "$path" >actual 2>err &&
	echo "$path: test: $2" >expect &&
	test_cmp expect actual &&
	test_line_count = 0 err

}


test_expect_success 'setup' '

	mkdir -p a/b/d a/c &&
	(
		echo "[attr]notest !test"
		echo "f	test=f"
		echo "a/i test=a/i"
		echo "onoff test -test"
		echo "offon -test test"
		echo "no notest"
	) >.gitattributes &&
	(
		echo "g test=a/g" &&
		echo "b/g test=a/b/g"
	) >a/.gitattributes &&
	(
		echo "h test=a/b/h" &&
		echo "d/* test=a/b/d/*"
		echo "d/yes notest"
	) >a/b/.gitattributes &&
	(
		echo "global test=global"
	) >"$HOME"/global-gitattributes &&
	cat <<EOF >expect-all
f: test: f
a/f: test: f
a/c/f: test: f
a/g: test: a/g
a/b/g: test: a/b/g
b/g: test: unspecified
a/b/h: test: a/b/h
a/b/d/g: test: a/b/d/*
onoff: test: unset
offon: test: set
no: notest: set
no: test: unspecified
a/b/d/no: notest: set
a/b/d/no: test: a/b/d/*
a/b/d/yes: notest: set
a/b/d/yes: test: unspecified
EOF

'

test_expect_success 'command line checks' '

	test_must_fail git check-attr &&
	test_must_fail git check-attr -- &&
	test_must_fail git check-attr test &&
	test_must_fail git check-attr test -- &&
	test_must_fail git check-attr -- f &&
	echo "f" | test_must_fail git check-attr --stdin &&
	echo "f" | test_must_fail git check-attr --stdin -- f &&
	echo "f" | test_must_fail git check-attr --stdin test -- f &&
	test_must_fail git check-attr "" -- f

'

test_expect_success 'attribute test' '

	attr_check f f &&
	attr_check a/f f &&
	attr_check a/c/f f &&
	attr_check a/g a/g &&
	attr_check a/b/g a/b/g &&
	attr_check b/g unspecified &&
	attr_check a/b/h a/b/h &&
	attr_check a/b/d/g "a/b/d/*" &&
	attr_check onoff unset &&
	attr_check offon set &&
	attr_check no unspecified &&
	attr_check a/b/d/no "a/b/d/*" &&
	attr_check a/b/d/yes unspecified

'

test_expect_failure 'unnormalized paths' '

	attr_check ./f f &&
	attr_check ./a/g a/g &&
	attr_check a/./g a/g &&
	attr_check a/c/../b/g a/b/g

'

test_expect_success 'core.attributesfile' '
	attr_check global unspecified &&
	git config core.attributesfile "$HOME/global-gitattributes" &&
	attr_check global global &&
	git config core.attributesfile "~/global-gitattributes" &&
	attr_check global global &&
	echo "global test=precedence" >> .gitattributes &&
	attr_check global precedence
'

test_expect_success 'attribute test: read paths from stdin' '

	grep -v notest < expect-all > expect &&
	sed -e "s/:.*//" < expect | git check-attr --stdin test > actual &&
	test_cmp expect actual
'

test_expect_success 'attribute test: --all option' '

	grep -v unspecified < expect-all | sort > expect &&
	sed -e "s/:.*//" < expect-all | uniq |
		git check-attr --stdin --all | sort > actual &&
	test_cmp expect actual
'

test_expect_success 'root subdir attribute test' '

	attr_check a/i a/i &&
	attr_check subdir/a/i unspecified

'

test_expect_success 'setup bare' '

	git clone --bare . bare.git &&
	cd bare.git

'

test_expect_success 'bare repository: check that .gitattribute is ignored' '

	(
		echo "f	test=f"
		echo "a/i test=a/i"
	) >.gitattributes &&
	attr_check f unspecified &&
	attr_check a/f unspecified &&
	attr_check a/c/f unspecified &&
	attr_check a/i unspecified &&
	attr_check subdir/a/i unspecified

'

test_expect_success 'bare repository: test info/attributes' '

	(
		echo "f	test=f"
		echo "a/i test=a/i"
	) >info/attributes &&
	attr_check f f &&
	attr_check a/f f &&
	attr_check a/c/f f &&
	attr_check a/i a/i &&
	attr_check subdir/a/i unspecified

'

test_done
