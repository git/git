#!/bin/sh

test_description='log --grep/--author/--regexp-ignore-case/-S/-G'
. ./test-lib.sh

test_log () {
	expect=$1
	kind=$2
	needle=$3
	shift 3
	rest=$@

	case $kind in
	--*)
		opt=$kind=$needle
		;;
	*)
		opt=$kind$needle
		;;
	esac
	case $expect in
	expect_nomatch)
		match=nomatch
		;;
	*)
		match=match
		;;
	esac

	test_expect_success "log $kind${rest:+ $rest} ($match)" "
		git log $rest $opt --format=%H >actual &&
		test_cmp $expect actual
	"
}

test_expect_success setup '
	>expect_nomatch &&

	>file &&
	git add file &&
	test_tick &&
	git commit -m initial &&
	git rev-parse --verify HEAD >expect_initial &&

	echo Picked >file &&
	git add file &&
	test_tick &&
	git commit --author="Another Person <another@example.com>" -m second &&
	git rev-parse --verify HEAD >expect_second
'

test_expect_success 'log --grep' '
	git log --grep=initial --format=%H >actual &&
	test_cmp expect_initial actual
'

test_expect_success 'log --grep --regexp-ignore-case' '
	git log --regexp-ignore-case --grep=InItial --format=%H >actual &&
	test_cmp expect_initial actual
'

test_expect_success 'log --grep -i' '
	git log -i --grep=InItial --format=%H >actual &&
	test_cmp expect_initial actual
'

test_expect_success 'log --author --regexp-ignore-case' '
	git log --regexp-ignore-case --author=person --format=%H >actual &&
	test_cmp expect_second actual
'

test_expect_success 'log --author -i' '
	git log -i --author=person --format=%H >actual &&
	test_cmp expect_second actual
'

test_log expect_nomatch -G picked
test_log expect_second  -G Picked
test_log expect_nomatch -G pickle --regexp-ignore-case
test_log expect_nomatch -G pickle -i
test_log expect_second  -G picked --regexp-ignore-case
test_log expect_second  -G picked -i

test_expect_success 'log -G --textconv (missing textconv tool)' '
	echo "* diff=test" >.gitattributes &&
	test_must_fail git -c diff.test.textconv=missing log -Gfoo &&
	rm .gitattributes
'

test_expect_success 'log -G --no-textconv (missing textconv tool)' '
	echo "* diff=test" >.gitattributes &&
	git -c diff.test.textconv=missing log -Gfoo --no-textconv >actual &&
	test_cmp expect_nomatch actual &&
	rm .gitattributes
'

test_log expect_nomatch -S picked
test_log expect_second  -S Picked
test_log expect_second  -S picked --regexp-ignore-case
test_log expect_second  -S picked -i
test_log expect_nomatch -S pickle --regexp-ignore-case
test_log expect_nomatch -S pickle -i

test_expect_success 'log -S --textconv (missing textconv tool)' '
	echo "* diff=test" >.gitattributes &&
	test_must_fail git -c diff.test.textconv=missing log -Sfoo &&
	rm .gitattributes
'

test_expect_success 'log -S --no-textconv (missing textconv tool)' '
	echo "* diff=test" >.gitattributes &&
	git -c diff.test.textconv=missing log -Sfoo --no-textconv >actual &&
	test_cmp expect_nomatch actual &&
	rm .gitattributes
'

test_done
