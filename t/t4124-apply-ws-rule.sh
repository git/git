#!/bin/sh

test_description='core.whitespace rules and git apply'

. ./test-lib.sh

prepare_test_file () {

	# A line that has character X is touched iff RULE is in effect:
	#       X  RULE
	#   	!  trailing-space
	#   	@  space-before-tab
	#   	#  indent-with-non-tab (default tab width 8)
	#	=  indent-with-non-tab,tabwidth=16
	#   	%  tab-in-indent
	sed -e "s/_/ /g" -e "s/>/	/" <<-\EOF
		An_SP in an ordinary line>and a HT.
		>A HT (%).
		_>A SP and a HT (@%).
		_>_A SP, a HT and a SP (@%).
		_______Seven SP.
		________Eight SP (#).
		_______>Seven SP and a HT (@%).
		________>Eight SP and a HT (@#%).
		_______>_Seven SP, a HT and a SP (@%).
		________>_Eight SP, a HT and a SP (@#%).
		_______________Fifteen SP (#).
		_______________>Fifteen SP and a HT (@#%).
		________________Sixteen SP (#=).
		________________>Sixteen SP and a HT (@#%=).
		_____a__Five SP, a non WS, two SP.
		A line with a (!) trailing SP_
		A line with a (!) trailing HT>
	EOF
}

apply_patch () {
	>target &&
	sed -e "s|\([ab]\)/file|\1/target|" <patch |
	git apply "$@"
}

test_fix () {
	# fix should not barf
	apply_patch --whitespace=fix || return 1

	# find touched lines
	$DIFF file target | sed -n -e "s/^> //p" >fixed

	# the changed lines are all expeced to change
	fixed_cnt=$(wc -l <fixed)
	case "$1" in
	'') expect_cnt=$fixed_cnt ;;
	?*) expect_cnt=$(grep "[$1]" <fixed | wc -l) ;;
	esac
	test $fixed_cnt -eq $expect_cnt || return 1

	# and we are not missing anything
	case "$1" in
	'') expect_cnt=0 ;;
	?*) expect_cnt=$(grep "[$1]" <file | wc -l) ;;
	esac
	test $fixed_cnt -eq $expect_cnt || return 1

	# Get the patch actually applied
	git diff-files -p target >fixed-patch
	test -s fixed-patch && return 0

	# Make sure it is complaint-free
	>target
	git apply --whitespace=error-all <fixed-patch

}

test_expect_success setup '

	>file &&
	git add file &&
	prepare_test_file >file &&
	git diff-files -p >patch &&
	>target &&
	git add target

'

test_expect_success 'whitespace=nowarn, default rule' '

	apply_patch --whitespace=nowarn &&
	test_cmp file target

'

test_expect_success 'whitespace=warn, default rule' '

	apply_patch --whitespace=warn &&
	test_cmp file target

'

test_expect_success 'whitespace=error-all, default rule' '

	apply_patch --whitespace=error-all && return 1
	test -s target && return 1
	: happy

'

test_expect_success 'whitespace=error-all, no rule' '

	git config core.whitespace -trailing,-space-before,-indent &&
	apply_patch --whitespace=error-all &&
	test_cmp file target

'

test_expect_success 'whitespace=error-all, no rule (attribute)' '

	git config --unset core.whitespace &&
	echo "target -whitespace" >.gitattributes &&
	apply_patch --whitespace=error-all &&
	test_cmp file target

'

test_expect_success 'spaces inserted by tab-in-indent' '

	git config core.whitespace -trailing,-space,-indent,tab &&
	rm -f .gitattributes &&
	test_fix % &&
	sed -e "s/_/ /g" -e "s/>/	/" <<-\EOF >expect &&
		An_SP in an ordinary line>and a HT.
		________A HT (%).
		________A SP and a HT (@%).
		_________A SP, a HT and a SP (@%).
		_______Seven SP.
		________Eight SP (#).
		________Seven SP and a HT (@%).
		________________Eight SP and a HT (@#%).
		_________Seven SP, a HT and a SP (@%).
		_________________Eight SP, a HT and a SP (@#%).
		_______________Fifteen SP (#).
		________________Fifteen SP and a HT (@#%).
		________________Sixteen SP (#=).
		________________________Sixteen SP and a HT (@#%=).
		_____a__Five SP, a non WS, two SP.
		A line with a (!) trailing SP_
		A line with a (!) trailing HT>
	EOF
	test_cmp expect target

'

for t in - ''
do
	case "$t" in '') tt='!' ;; *) tt= ;; esac
	for s in - ''
	do
		case "$s" in '') ts='@' ;; *) ts= ;; esac
		for i in - ''
		do
			case "$i" in '') ti='#' ti16='=';; *) ti= ti16= ;; esac
			for h in - ''
			do
				[ -z "$h$i" ] && continue
				case "$h" in '') th='%' ;; *) th= ;; esac
				rule=${t}trailing,${s}space,${i}indent,${h}tab

				rm -f .gitattributes
				test_expect_success "rule=$rule" '
					git config core.whitespace "$rule" &&
					test_fix "$tt$ts$ti$th"
				'

				test_expect_success "rule=$rule,tabwidth=16" '
					git config core.whitespace "$rule,tabwidth=16" &&
					test_fix "$tt$ts$ti16$th"
				'

				test_expect_success "rule=$rule (attributes)" '
					git config --unset core.whitespace &&
					echo "target whitespace=$rule" >.gitattributes &&
					test_fix "$tt$ts$ti$th"
				'

				test_expect_success "rule=$rule,tabwidth=16 (attributes)" '
					echo "target whitespace=$rule,tabwidth=16" >.gitattributes &&
					test_fix "$tt$ts$ti16$th"
				'

			done
		done
	done
done

create_patch () {
	sed -e "s/_/ /" <<-\EOF
		diff --git a/target b/target
		index e69de29..8bd6648 100644
		--- a/target
		+++ b/target
		@@ -0,0 +1,3 @@
		+An empty line follows
		+
		+A line with trailing whitespace and no newline_
		\ No newline at end of file
	EOF
}

test_expect_success 'trailing whitespace & no newline at the end of file' '
	>target &&
	create_patch >patch-file &&
	git apply --whitespace=fix patch-file &&
	grep "newline$" target &&
	grep "^$" target
'

test_expect_success 'blank at EOF with --whitespace=fix (1)' '
	test_might_fail git config --unset core.whitespace &&
	rm -f .gitattributes &&

	{ echo a; echo b; echo c; } >one &&
	git add one &&
	{ echo a; echo b; echo c; } >expect &&
	{ cat expect; echo; } >one &&
	git diff -- one >patch &&

	git checkout one &&
	git apply --whitespace=fix patch &&
	test_cmp expect one
'

test_expect_success 'blank at EOF with --whitespace=fix (2)' '
	{ echo a; echo b; echo c; } >one &&
	git add one &&
	{ echo a; echo c; } >expect &&
	{ cat expect; echo; echo; } >one &&
	git diff -- one >patch &&

	git checkout one &&
	git apply --whitespace=fix patch &&
	test_cmp expect one
'

test_expect_success 'blank at EOF with --whitespace=fix (3)' '
	{ echo a; echo b; echo; } >one &&
	git add one &&
	{ echo a; echo c; echo; } >expect &&
	{ cat expect; echo; echo; } >one &&
	git diff -- one >patch &&

	git checkout one &&
	git apply --whitespace=fix patch &&
	test_cmp expect one
'

test_expect_success 'blank at end of hunk, not at EOF with --whitespace=fix' '
	{ echo a; echo b; echo; echo; echo; echo; echo; echo d; } >one &&
	git add one &&
	{ echo a; echo c; echo; echo; echo; echo; echo; echo; echo d; } >expect &&
	cp expect one &&
	git diff -- one >patch &&

	git checkout one &&
	git apply --whitespace=fix patch &&
	test_cmp expect one
'

test_expect_success 'blank at EOF with --whitespace=warn' '
	{ echo a; echo b; echo c; } >one &&
	git add one &&
	echo >>one &&
	cat one >expect &&
	git diff -- one >patch &&

	git checkout one &&
	git apply --whitespace=warn patch 2>error &&
	test_cmp expect one &&
	grep "new blank line at EOF" error
'

test_expect_success 'blank at EOF with --whitespace=error' '
	{ echo a; echo b; echo c; } >one &&
	git add one &&
	cat one >expect &&
	echo >>one &&
	git diff -- one >patch &&

	git checkout one &&
	test_must_fail git apply --whitespace=error patch 2>error &&
	test_cmp expect one &&
	grep "new blank line at EOF" error
'

test_expect_success 'blank but not empty at EOF' '
	{ echo a; echo b; echo c; } >one &&
	git add one &&
	echo "   " >>one &&
	cat one >expect &&
	git diff -- one >patch &&

	git checkout one &&
	git apply --whitespace=warn patch 2>error &&
	test_cmp expect one &&
	grep "new blank line at EOF" error
'

test_expect_success 'applying beyond EOF requires one non-blank context line' '
	{ echo; echo; echo; echo; } >one &&
	git add one &&
	{ echo b; } >>one &&
	git diff -- one >patch &&

	git checkout one &&
	{ echo a; echo; } >one &&
	cp one expect &&
	test_must_fail git apply --whitespace=fix patch &&
	test_cmp one expect &&
	test_must_fail git apply --ignore-space-change --whitespace=fix patch &&
	test_cmp one expect
'

test_expect_success 'tons of blanks at EOF should not apply' '
	for i in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16; do
		echo; echo; echo; echo;
	done >one &&
	git add one &&
	echo a >>one &&
	git diff -- one >patch &&

	>one &&
	test_must_fail git apply --whitespace=fix patch &&
	test_must_fail git apply --ignore-space-change --whitespace=fix patch
'

test_expect_success 'missing blank line at end with --whitespace=fix' '
	echo a >one &&
	echo >>one &&
	git add one &&
	echo b >>one &&
	cp one expect &&
	git diff -- one >patch &&
	echo a >one &&
	cp one saved-one &&
	test_must_fail git apply patch &&
	git apply --whitespace=fix patch &&
	test_cmp one expect &&
	mv saved-one one &&
	git apply --ignore-space-change --whitespace=fix patch &&
	test_cmp one expect
'

test_expect_success 'two missing blank lines at end with --whitespace=fix' '
	{ echo a; echo; echo b; echo c; } >one &&
	cp one no-blank-lines &&
	{ echo; echo; } >>one &&
	git add one &&
	echo d >>one &&
	cp one expect &&
	echo >>one &&
	git diff -- one >patch &&
	cp no-blank-lines one &&
	test_must_fail git apply patch &&
	git apply --whitespace=fix patch &&
	test_cmp one expect &&
	mv no-blank-lines one &&
	test_must_fail git apply patch &&
	git apply --ignore-space-change --whitespace=fix patch &&
	test_cmp one expect
'

test_expect_success 'missing blank line at end, insert before end, --whitespace=fix' '
	{ echo a; echo; } >one &&
	git add one &&
	{ echo b; echo a; echo; } >one &&
	cp one expect &&
	git diff -- one >patch &&
	echo a >one &&
	test_must_fail git apply patch &&
	git apply --whitespace=fix patch &&
	test_cmp one expect
'

test_expect_success 'shrink file with tons of missing blanks at end of file' '
	{ echo a; echo b; echo c; } >one &&
	cp one no-blank-lines &&
	for i in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16; do
		echo; echo; echo; echo;
	done >>one &&
	git add one &&
	echo a >one &&
	cp one expect &&
	git diff -- one >patch &&
	cp no-blank-lines one &&
	test_must_fail git apply patch &&
	git apply --whitespace=fix patch &&
	test_cmp one expect &&
	mv no-blank-lines one &&
	git apply --ignore-space-change --whitespace=fix patch &&
	test_cmp one expect
'

test_expect_success 'missing blanks at EOF must only match blank lines' '
	{ echo a; echo b; } >one &&
	git add one &&
	{ echo c; echo d; } >>one &&
	git diff -- one >patch &&

	echo a >one &&
	test_must_fail git apply patch &&
	test_must_fail git apply --whitespace=fix patch &&
	test_must_fail git apply --ignore-space-change --whitespace=fix patch
'

sed -e's/Z//' >one <<EOF
a
b
c
		      Z
EOF

test_expect_success 'missing blank line should match context line with spaces' '
	git add one &&
	echo d >>one &&
	git diff -- one >patch &&
	{ echo a; echo b; echo c; } >one &&
	cp one expect &&
	{ echo; echo d; } >>expect &&
	git add one &&

	git apply --whitespace=fix patch &&
	test_cmp one expect
'

sed -e's/Z//' >one <<EOF
a
b
c
		      Z
EOF

test_expect_success 'same, but with the --ignore-space-option' '
	git add one &&
	echo d >>one &&
	cp one expect &&
	git diff -- one >patch &&
	{ echo a; echo b; echo c; } >one &&
	git add one &&

	git checkout-index -f one &&
	git apply --ignore-space-change --whitespace=fix patch &&
	test_cmp one expect
'

test_expect_success 'same, but with CR-LF line endings && cr-at-eol set' '
	git config core.whitespace cr-at-eol &&
	printf "a\r\n" >one &&
	printf "b\r\n" >>one &&
	printf "c\r\n" >>one &&
	cp one save-one &&
	printf "                 \r\n" >>one &&
	git add one &&
	printf "d\r\n" >>one &&
	cp one expect &&
	git diff -- one >patch &&
	mv save-one one &&

	git apply --ignore-space-change --whitespace=fix patch &&
	test_cmp one expect
'

test_expect_success 'same, but with CR-LF line endings && cr-at-eol unset' '
	git config --unset core.whitespace &&
	printf "a\r\n" >one &&
	printf "b\r\n" >>one &&
	printf "c\r\n" >>one &&
	cp one save-one &&
	printf "                 \r\n" >>one &&
	git add one &&
	cp one expect &&
	printf "d\r\n" >>one &&
	git diff -- one >patch &&
	mv save-one one &&
	echo d >>expect &&

	git apply --ignore-space-change --whitespace=fix patch &&
	test_cmp one expect
'

test_done
