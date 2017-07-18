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

# test -i and --regexp-ignore-case and expect both to behave the same way
test_log_icase () {
	test_log $@ --regexp-ignore-case
	test_log $@ -i
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

test_log	expect_initial	--grep initial
test_log	expect_nomatch	--grep InItial
test_log_icase	expect_initial	--grep InItial
test_log_icase	expect_nomatch	--grep initail

test_log	expect_second	--author Person
test_log	expect_nomatch	--author person
test_log_icase	expect_second	--author person
test_log_icase	expect_nomatch	--author spreon

test_log	expect_nomatch	-G picked
test_log	expect_second	-G Picked
test_log_icase	expect_nomatch	-G pickle
test_log_icase	expect_second	-G picked

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

test_log	expect_nomatch	-S picked
test_log	expect_second	-S Picked
test_log_icase	expect_second	-S picked
test_log_icase	expect_nomatch	-S pickle

test_log	expect_nomatch	-S p.cked --pickaxe-regex
test_log	expect_second	-S P.cked --pickaxe-regex
test_log_icase	expect_second	-S p.cked --pickaxe-regex
test_log_icase	expect_nomatch	-S p.ckle --pickaxe-regex

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
