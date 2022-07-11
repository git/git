#!/bin/sh

test_description='Revision traversal vs grafts and path limiter'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

test_expect_success setup '
	mkdir subdir &&
	echo >fileA fileA &&
	echo >subdir/fileB fileB &&
	git add fileA subdir/fileB &&
	git commit -a -m "Initial in one history." &&
	A0=$(git rev-parse --verify HEAD) &&

	echo >fileA fileA modified &&
	git commit -a -m "Second in one history." &&
	A1=$(git rev-parse --verify HEAD) &&

	echo >subdir/fileB fileB modified &&
	git commit -a -m "Third in one history." &&
	A2=$(git rev-parse --verify HEAD) &&

	git update-ref -d refs/heads/main &&
	rm -f .git/index &&

	echo >fileA fileA again &&
	echo >subdir/fileB fileB again &&
	git add fileA subdir/fileB &&
	git commit -a -m "Initial in alternate history." &&
	B0=$(git rev-parse --verify HEAD) &&

	echo >fileA fileA modified in alternate history &&
	git commit -a -m "Second in alternate history." &&
	B1=$(git rev-parse --verify HEAD) &&

	echo >subdir/fileB fileB modified in alternate history &&
	git commit -a -m "Third in alternate history." &&
	B2=$(git rev-parse --verify HEAD) &&
	: done
'

check () {
	type=$1
	shift

	arg=
	which=arg
	rm -f test.expect
	for a
	do
		if test "z$a" = z--
		then
			which=expect
			child=
			continue
		fi
		if test "$which" = arg
		then
			arg="$arg$a "
			continue
		fi
		if test "$type" = basic
		then
			echo "$a"
		else
			if test "z$child" != z
			then
				echo "$child $a"
			fi
			child="$a"
		fi
	done >test.expect
	if test "$type" != basic && test "z$child" != z
	then
		echo >>test.expect $child
	fi
	if test $type = basic
	then
		git rev-list $arg >test.actual
	elif test $type = parents
	then
		git rev-list --parents $arg >test.actual
	elif test $type = parents-raw
	then
		git rev-list --parents --pretty=raw $arg |
		sed -n -e 's/^commit //p' >test.actual
	fi
	test_cmp test.expect test.actual
}

for type in basic parents parents-raw
do
	test_expect_success 'without grafts' "
		rm -f .git/info/grafts &&
		check $type $B2 -- $B2 $B1 $B0
	"

	test_expect_success 'with grafts' "
		mkdir -p .git/info &&
		echo '$B0 $A2' >.git/info/grafts &&
		check $type $B2 -- $B2 $B1 $B0 $A2 $A1 $A0
	"

	test_expect_success 'without grafts, with pathlimit' "
		rm -f .git/info/grafts &&
		check $type $B2 subdir -- $B2 $B0
	"

	test_expect_success 'with grafts, with pathlimit' "
		echo '$B0 $A2' >.git/info/grafts &&
		check $type $B2 subdir -- $B2 $B0 $A2 $A0
	"

done

test_expect_success 'show advice that grafts are deprecated' '
	git show HEAD 2>err &&
	test_i18ngrep "git replace" err &&
	test_config advice.graftFileDeprecated false &&
	git show HEAD 2>err &&
	test_i18ngrep ! "git replace" err
'

test_done
