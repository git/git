#!/bin/sh

test_description='core.whitespace rules and git-apply'

. ./test-lib.sh

prepare_test_file () {

	# A line that has character X is touched iff RULE is in effect:
	#       X  RULE
	#   	!  trailing-space
	#   	@  space-before-tab
	#   	#  indent-with-non-tab
	sed -e "s/_/ /g" -e "s/>/	/" <<-\EOF
		An_SP in an ordinary line>and a HT.
		>A HT.
		_>A SP and a HT (@).
		_>_A SP, a HT and a SP (@).
		_______Seven SP.
		________Eight SP (#).
		_______>Seven SP and a HT (@).
		________>Eight SP and a HT (@#).
		_______>_Seven SP, a HT and a SP (@).
		________>_Eight SP, a HT and a SP (@#).
		_______________Fifteen SP (#).
		_______________>Fifteen SP and a HT (@#).
		________________Sixteen SP (#).
		________________>Sixteen SP and a HT (@#).
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
	diff file target | sed -n -e "s/^> //p" >fixed

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
	diff file target

'

test_expect_success 'whitespace=warn, default rule' '

	apply_patch --whitespace=warn &&
	diff file target

'

test_expect_success 'whitespace=error-all, default rule' '

	apply_patch --whitespace=error-all && return 1
	test -s target && return 1
	: happy

'

test_expect_success 'whitespace=error-all, no rule' '

	git config core.whitespace -trailing,-space-before,-indent &&
	apply_patch --whitespace=error-all &&
	diff file target

'

test_expect_success 'whitespace=error-all, no rule (attribute)' '

	git config --unset core.whitespace &&
	echo "target -whitespace" >.gitattributes &&
	apply_patch --whitespace=error-all &&
	diff file target

'

for t in - ''
do
	case "$t" in '') tt='!' ;; *) tt= ;; esac
	for s in - ''
	do
		case "$s" in '') ts='@' ;; *) ts= ;; esac
		for i in - ''
		do
			case "$i" in '') ti='#' ;; *) ti= ;; esac
			rule=${t}trailing,${s}space,${i}indent

			rm -f .gitattributes
			test_expect_success "rule=$rule" '
				git config core.whitespace "$rule" &&
				test_fix "$tt$ts$ti"
			'

			test_expect_success "rule=$rule (attributes)" '
				git config --unset core.whitespace &&
				echo "target whitespace=$rule" >.gitattributes &&
				test_fix "$tt$ts$ti"
			'

		done
	done
done

test_done
