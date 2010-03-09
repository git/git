#!/bin/sh

test_description='Various filesystem issues'

. ./test-lib.sh

auml=`printf '\xc3\xa4'`
aumlcdiar=`printf '\x61\xcc\x88'`

case_insensitive=
unibad=
no_symlinks=
test_expect_success 'see what we expect' '

	test_case=test_expect_success
	test_unicode=test_expect_success
	mkdir junk &&
	echo good >junk/CamelCase &&
	echo bad >junk/camelcase &&
	if test "$(cat junk/CamelCase)" != good
	then
		test_case=test_expect_failure
		case_insensitive=t
	fi &&
	rm -fr junk &&
	mkdir junk &&
	>junk/"$auml" &&
	case "$(cd junk && echo *)" in
	"$aumlcdiar")
		test_unicode=test_expect_failure
		unibad=t
		;;
	*)	;;
	esac &&
	rm -fr junk &&
	{
		ln -s x y 2> /dev/null &&
		test -h y 2> /dev/null ||
		no_symlinks=1
		rm -f y
	}
'

test "$case_insensitive" &&
	say "will test on a case insensitive filesystem"
test "$unibad" &&
	say "will test on a unicode corrupting filesystem"
test "$no_symlinks" &&
	say "will test on a filesystem lacking symbolic links"

if test "$case_insensitive"
then
test_expect_success "detection of case insensitive filesystem during repo init" '

	test $(git config --bool core.ignorecase) = true
'
else
test_expect_success "detection of case insensitive filesystem during repo init" '

	test_must_fail git config --bool core.ignorecase >/dev/null ||
	test $(git config --bool core.ignorecase) = false
'
fi

if test "$no_symlinks"
then
test_expect_success "detection of filesystem w/o symlink support during repo init" '

	v=$(git config --bool core.symlinks) &&
	test "$v" = false
'
else
test_expect_success "detection of filesystem w/o symlink support during repo init" '

	test_must_fail git config --bool core.symlinks ||
	test "$(git config --bool core.symlinks)" = true
'
fi

test_expect_success "setup case tests" '

	git config core.ignorecase true &&
	touch camelcase &&
	git add camelcase &&
	git commit -m "initial" &&
	git tag initial &&
	git checkout -b topic &&
	git mv camelcase tmp &&
	git mv tmp CamelCase &&
	git commit -m "rename" &&
	git checkout -f master

'

$test_case 'rename (case change)' '

	git mv camelcase CamelCase &&
	git commit -m "rename"

'

$test_case 'merge (case change)' '

	rm -f CamelCase &&
	rm -f camelcase &&
	git reset --hard initial &&
	git merge topic

'



test_expect_failure 'add (with different case)' '

	git reset --hard initial &&
	rm camelcase &&
	echo 1 >CamelCase &&
	git add CamelCase &&
	camel=$(git ls-files | grep -i camelcase) &&
	test $(echo "$camel" | wc -l) = 1 &&
	test "z$(git cat-file blob :$camel)" = z1

'

test_expect_success "setup unicode normalization tests" '

  test_create_repo unicode &&
  cd unicode &&
  touch "$aumlcdiar" &&
  git add "$aumlcdiar" &&
  git commit -m initial
  git tag initial &&
  git checkout -b topic &&
  git mv $aumlcdiar tmp &&
  git mv tmp "$auml" &&
  git commit -m rename &&
  git checkout -f master

'

$test_unicode 'rename (silent unicode normalization)' '

 git mv "$aumlcdiar" "$auml" &&
 git commit -m rename

'

$test_unicode 'merge (silent unicode normalization)' '

 git reset --hard initial &&
 git merge topic

'

test_done
