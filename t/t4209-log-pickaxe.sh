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
		but log $rest $opt --format=%H >actual &&
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
	but add file &&
	test_tick &&
	but cummit -m initial &&
	but rev-parse --verify HEAD >expect_initial &&

	echo Picked >file &&
	but add file &&
	test_tick &&
	but cummit --author="Another Person <another@example.com>" -m second &&
	but rev-parse --verify HEAD >expect_second
'

test_expect_success 'usage' '
	test_expect_code 129 but log -S 2>err &&
	test_i18ngrep "switch.*requires a value" err &&

	test_expect_code 129 but log -G 2>err &&
	test_i18ngrep "switch.*requires a value" err &&

	test_expect_code 128 but log -Gregex -Sstring 2>err &&
	grep "cannot be used together" err &&

	test_expect_code 128 but log -Gregex --find-object=HEAD 2>err &&
	grep "cannot be used together" err &&

	test_expect_code 128 but log -Sstring --find-object=HEAD 2>err &&
	grep "cannot be used together" err &&

	test_expect_code 128 but log --pickaxe-all --find-object=HEAD 2>err &&
	grep "cannot be used together" err
'

test_expect_success 'usage: --pickaxe-regex' '
	test_expect_code 128 but log -Gregex --pickaxe-regex 2>err &&
	grep "cannot be used together" err
'

test_expect_success 'usage: --no-pickaxe-regex' '
	cat >expect <<-\EOF &&
	fatal: unrecognized argument: --no-pickaxe-regex
	EOF

	test_expect_code 128 but log -Sstring --no-pickaxe-regex 2>actual &&
	test_cmp expect actual &&

	test_expect_code 128 but log -Gstring --no-pickaxe-regex 2>err &&
	test_cmp expect actual
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
	echo "* diff=test" >.butattributes &&
	test_must_fail but -c diff.test.textconv=missing log -Gfoo &&
	rm .butattributes
'

test_expect_success 'log -G --no-textconv (missing textconv tool)' '
	echo "* diff=test" >.butattributes &&
	but -c diff.test.textconv=missing log -Gfoo --no-textconv >actual &&
	test_cmp expect_nomatch actual &&
	rm .butattributes
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
	echo "* diff=test" >.butattributes &&
	test_must_fail but -c diff.test.textconv=missing log -Sfoo &&
	rm .butattributes
'

test_expect_success 'log -S --no-textconv (missing textconv tool)' '
	echo "* diff=test" >.butattributes &&
	but -c diff.test.textconv=missing log -Sfoo --no-textconv >actual &&
	test_cmp expect_nomatch actual &&
	rm .butattributes
'

test_expect_success 'setup log -[GS] plain & regex' '
	test_create_repo GS-plain &&
	test_cummit -C GS-plain --append A data.txt "a" &&
	test_cummit -C GS-plain --append B data.txt "a a" &&
	test_cummit -C GS-plain --append C data.txt "b" &&
	test_cummit -C GS-plain --append D data.txt "[b]" &&
	test_cummit -C GS-plain E data.txt "" &&

	# We also include E, the deletion cummit
	but -C GS-plain log --grep="[ABE]" >A-to-B-then-E-log &&
	but -C GS-plain log --grep="[CDE]" >C-to-D-then-E-log &&
	but -C GS-plain log --grep="[DE]" >D-then-E-log &&
	but -C GS-plain log >full-log
'

test_expect_success 'log -G trims diff new/old [-+]' '
	but -C GS-plain log -G"[+-]a" >log &&
	test_must_be_empty log &&
	but -C GS-plain log -G"^a" >log &&
	test_cmp log A-to-B-then-E-log
'

test_expect_success 'log -S<pat> is not a regex, but -S<pat> --pickaxe-regex is' '
	but -C GS-plain log -S"a" >log &&
	test_cmp log A-to-B-then-E-log &&

	but -C GS-plain log -S"[a]" >log &&
	test_must_be_empty log &&

	but -C GS-plain log -S"[a]" --pickaxe-regex >log &&
	test_cmp log A-to-B-then-E-log &&

	but -C GS-plain log -S"[b]" >log &&
	test_cmp log D-then-E-log &&

	but -C GS-plain log -S"[b]" --pickaxe-regex >log &&
	test_cmp log C-to-D-then-E-log
'

test_expect_success 'setup log -[GS] binary & --text' '
	test_create_repo GS-bin-txt &&
	test_cummit -C GS-bin-txt --printf A data.bin "a\na\0a\n" &&
	test_cummit -C GS-bin-txt --append --printf B data.bin "a\na\0a\n" &&
	test_cummit -C GS-bin-txt C data.bin "" &&
	but -C GS-bin-txt log >full-log
'

test_expect_success 'log -G ignores binary files' '
	but -C GS-bin-txt log -Ga >log &&
	test_must_be_empty log
'

test_expect_success 'log -G looks into binary files with -a' '
	but -C GS-bin-txt log -a -Ga >log &&
	test_cmp log full-log
'

test_expect_success 'log -G looks into binary files with textconv filter' '
	test_when_finished "rm GS-bin-txt/.butattributes" &&
	(
		cd GS-bin-txt &&
		echo "* diff=bin" >.butattributes &&
		but -c diff.bin.textconv=cat log -Ga >../log
	) &&
	test_cmp log full-log
'

test_expect_success 'log -S looks into binary files' '
	but -C GS-bin-txt log -Sa >log &&
	test_cmp log full-log
'

test_expect_success 'log -S --pickaxe-regex looks into binary files' '
	but -C GS-bin-txt log --pickaxe-regex -Sa >log &&
	test_cmp log full-log &&

	but -C GS-bin-txt log --pickaxe-regex -S"[a]" >log &&
	test_cmp log full-log
'

test_done
