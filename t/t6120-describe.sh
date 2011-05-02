#!/bin/sh

test_description='test describe

                       B
        .--------------o----o----o----x
       /                   /    /
 o----o----o----o----o----.    /
       \        A    c        /
        .------------o---o---o
                   D,R   e
'
. ./test-lib.sh

check_describe () {
	expect="$1"
	shift
	R=$(git describe "$@" 2>err.actual)
	S=$?
	cat err.actual >&3
	test_expect_success "describe $*" '
	test $S = 0 &&
	case "$R" in
	$expect)	echo happy ;;
	*)	echo "Oops - $R is not $expect";
		false ;;
	esac
	'
}

test_expect_success setup '

	test_tick &&
	echo one >file && git add file && git commit -m initial &&
	one=$(git rev-parse HEAD) &&

	git describe --always HEAD &&

	test_tick &&
	echo two >file && git add file && git commit -m second &&
	two=$(git rev-parse HEAD) &&

	test_tick &&
	echo three >file && git add file && git commit -m third &&

	test_tick &&
	echo A >file && git add file && git commit -m A &&
	test_tick &&
	git tag -a -m A A &&

	test_tick &&
	echo c >file && git add file && git commit -m c &&
	test_tick &&
	git tag c &&

	git reset --hard $two &&
	test_tick &&
	echo B >side && git add side && git commit -m B &&
	test_tick &&
	git tag -a -m B B &&

	test_tick &&
	git merge -m Merged c &&
	merged=$(git rev-parse HEAD) &&

	git reset --hard $two &&
	test_tick &&
	echo D >another && git add another && git commit -m D &&
	test_tick &&
	git tag -a -m D D &&
	test_tick &&
	git tag -a -m R R &&

	test_tick &&
	echo DD >another && git commit -a -m another &&

	test_tick &&
	git tag e &&

	test_tick &&
	echo DDD >another && git commit -a -m "yet another" &&

	test_tick &&
	git merge -m Merged $merged &&

	test_tick &&
	echo X >file && echo X >side && git add file side &&
	git commit -m x

'

check_describe A-* HEAD
check_describe A-* HEAD^
check_describe R-* HEAD^^
check_describe A-* HEAD^^2
check_describe B HEAD^^2^
check_describe R-* HEAD^^^

check_describe c-* --tags HEAD
check_describe c-* --tags HEAD^
check_describe e-* --tags HEAD^^
check_describe c-* --tags HEAD^^2
check_describe B --tags HEAD^^2^
check_describe e --tags HEAD^^^

check_describe heads/master --all HEAD
check_describe tags/c-* --all HEAD^
check_describe tags/e --all HEAD^^^

check_describe B-0-* --long HEAD^^2^
check_describe A-3-* --long HEAD^^2

: >err.expect
check_describe A --all A^0
test_expect_success 'no warning was displayed for A' '
	test_cmp err.expect err.actual
'

test_expect_success 'rename tag A to Q locally' '
	mv .git/refs/tags/A .git/refs/tags/Q
'
cat - >err.expect <<EOF
warning: tag 'A' is really 'Q' here
EOF
check_describe A-* HEAD
test_expect_success 'warning was displayed for Q' '
	test_i18ncmp err.expect err.actual
'
test_expect_success 'rename tag Q back to A' '
	mv .git/refs/tags/Q .git/refs/tags/A
'

test_expect_success 'pack tag refs' 'git pack-refs'
check_describe A-* HEAD

check_describe "A-*[0-9a-f]" --dirty

test_expect_success 'set-up dirty work tree' '
	echo >>file
'

check_describe "A-*[0-9a-f]-dirty" --dirty

check_describe "A-*[0-9a-f].mod" --dirty=.mod

test_expect_success 'describe --dirty HEAD' '
	test_must_fail git describe --dirty HEAD
'

test_expect_success 'set-up matching pattern tests' '
	git tag -a -m test-annotated test-annotated &&
	echo >>file &&
	test_tick &&
	git commit -a -m "one more" &&
	git tag test1-lightweight &&
	echo >>file &&
	test_tick &&
	git commit -a -m "yet another" &&
	git tag test2-lightweight &&
	echo >>file &&
	test_tick &&
	git commit -a -m "even more"

'

check_describe "test-annotated-*" --match="test-*"

check_describe "test1-lightweight-*" --tags --match="test1-*"

check_describe "test2-lightweight-*" --tags --match="test2-*"

check_describe "test2-lightweight-*" --long --tags --match="test2-*" HEAD^

test_done
