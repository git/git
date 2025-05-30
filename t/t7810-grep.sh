#!/bin/sh
#
# Copyright (c) 2006 Junio C Hamano
#

test_description='git grep various.
'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

test_invalid_grep_expression() {
	params="$@" &&
	test_expect_success "invalid expression: grep $params" '
		test_must_fail git grep $params -- nonexisting
	'
}

LC_ALL=en_US.UTF-8 test-tool regex '^.$' '¿' &&
  test_set_prereq MB_REGEX

cat >hello.c <<EOF
#include <assert.h>
#include <stdio.h>

int main(int argc, const char **argv)
{
	printf("Hello world.\n");
	return 0;
	/* char ?? */
}

EOF

test_expect_success setup '
	cat >file <<-\EOF &&
	foo mmap bar
	foo_mmap bar
	foo_mmap bar mmap
	foo mmap bar_mmap
	foo_mmap bar mmap baz
	EOF
	cat >hello_world <<-\EOF &&
	Hello world
	HeLLo world
	Hello_world
	HeLLo_world
	EOF
	cat >ab <<-\EOF &&
	a+b*c
	a+bc
	abc
	EOF
	cat >d0 <<-\EOF &&
	d
	0
	EOF
	echo vvv >v &&
	echo ww w >w &&
	echo x x xx x >x &&
	echo y yy >y &&
	echo zzz > z &&
	mkdir t &&
	echo test >t/t &&
	echo vvv >t/v &&
	mkdir t/a &&
	echo vvv >t/a/v &&
	qz_to_tab_space >space <<-\EOF &&
	line without leading space1
	Zline with leading space1
	Zline with leading space2
	Zline with leading space3
	line without leading space2
	EOF
	cat >hello.ps1 <<-\EOF &&
	# No-op.
	function dummy() {}

	# Say hello.
	function hello() {
	  echo "Hello world."
	  echo "Hello again."
	} # hello

	# Still a no-op.
	function dummy() {}
	EOF
	printf "\200\nASCII\n" >invalid-utf8 &&
	if test_have_prereq FUNNYNAMES
	then
		echo unusual >"\"unusual\" pathname" &&
		echo unusual >"t/nested \"unusual\" pathname"
	fi &&
	if test_have_prereq MB_REGEX
	then
		echo "¿" >reverse-question-mark
	fi &&
	git add . &&
	test_tick &&
	git commit -m initial
'

test_expect_success 'grep should not segfault with a bad input' '
	test_must_fail git grep "("
'

test_invalid_grep_expression --and -e A

test_pattern_type () {
	H=$1 &&
	HC=$2 &&
	L=$3 &&
	type=$4 &&
	shift 4 &&

	expected_str= &&
	case "$type" in
	BRE)
		expected_str="${HC}ab:a+bc"
		;;
	ERE)
		expected_str="${HC}ab:abc"
		;;
	FIX)
		expected_str="${HC}ab:a+b*c"
		;;
	*)
		BUG "unknown pattern type '$type'"
		;;
	esac &&
	config_str="$@" &&

	test_expect_success "grep $L with '$config_str' interpreted as $type" '
		echo $expected_str >expected &&
		git $config_str grep "a+b*c" $H ab >actual &&
		test_cmp expected actual
	'
}

for H in HEAD ''
do
	case "$H" in
	HEAD)	HC='HEAD:' L='HEAD' ;;
	'')	HC= L='in working tree' ;;
	esac

	test_expect_success "grep -w $L" '
		cat >expected <<-EOF &&
		${HC}file:1:foo mmap bar
		${HC}file:3:foo_mmap bar mmap
		${HC}file:4:foo mmap bar_mmap
		${HC}file:5:foo_mmap bar mmap baz
		EOF
		git -c grep.linenumber=false grep -n -w -e mmap $H >actual &&
		test_cmp expected actual
	'

	test_expect_success "grep -w $L (with --column)" '
		cat >expected <<-EOF &&
		${HC}file:5:foo mmap bar
		${HC}file:14:foo_mmap bar mmap
		${HC}file:5:foo mmap bar_mmap
		${HC}file:14:foo_mmap bar mmap baz
		EOF
		git grep --column -w -e mmap $H >actual &&
		test_cmp expected actual
	'

	test_expect_success "grep -w $L (with --column, extended OR)" '
		cat >expected <<-EOF &&
		${HC}file:14:foo_mmap bar mmap
		${HC}file:19:foo_mmap bar mmap baz
		EOF
		git grep --column -w -e mmap$ --or -e baz $H >actual &&
		test_cmp expected actual
	'

	test_expect_success "grep -w $L (with --column, --invert-match)" '
		cat >expected <<-EOF &&
		${HC}file:1:foo mmap bar
		${HC}file:1:foo_mmap bar
		${HC}file:1:foo_mmap bar mmap
		${HC}file:1:foo mmap bar_mmap
		EOF
		git grep --column --invert-match -w -e baz $H -- file >actual &&
		test_cmp expected actual
	'

	test_expect_success "grep $L (with --column, --invert-match, extended OR)" '
		cat >expected <<-EOF &&
		${HC}hello_world:6:HeLLo_world
		EOF
		git grep --column --invert-match -e ll --or --not -e _ $H -- hello_world \
			>actual &&
		test_cmp expected actual
	'

	test_expect_success "grep $L (with --column, --invert-match, extended AND)" '
		cat >expected <<-EOF &&
		${HC}hello_world:3:Hello world
		${HC}hello_world:3:Hello_world
		${HC}hello_world:6:HeLLo_world
		EOF
		git grep --column --invert-match --not -e _ --and --not -e ll $H -- hello_world \
			>actual &&
		test_cmp expected actual
	'

	test_expect_success "grep $L (with --column, double-negation)" '
		cat >expected <<-EOF &&
		${HC}file:1:foo_mmap bar mmap baz
		EOF
		git grep --column --not \( --not -e foo --or --not -e baz \) $H -- file \
			>actual &&
		test_cmp expected actual
	'

	test_expect_success "grep -w $L (with --column, -C)" '
		cat >expected <<-EOF &&
		${HC}file:5:foo mmap bar
		${HC}file-foo_mmap bar
		${HC}file:14:foo_mmap bar mmap
		${HC}file:5:foo mmap bar_mmap
		${HC}file:14:foo_mmap bar mmap baz
		EOF
		git grep --column -w -C1 -e mmap $H >actual &&
		test_cmp expected actual
	'

	test_expect_success "grep -w $L (with --line-number, --column)" '
		cat >expected <<-EOF &&
		${HC}file:1:5:foo mmap bar
		${HC}file:3:14:foo_mmap bar mmap
		${HC}file:4:5:foo mmap bar_mmap
		${HC}file:5:14:foo_mmap bar mmap baz
		EOF
		git grep -n --column -w -e mmap $H >actual &&
		test_cmp expected actual
	'

	test_expect_success "grep -w $L (with non-extended patterns, --column)" '
		cat >expected <<-EOF &&
		${HC}file:5:foo mmap bar
		${HC}file:10:foo_mmap bar
		${HC}file:10:foo_mmap bar mmap
		${HC}file:5:foo mmap bar_mmap
		${HC}file:10:foo_mmap bar mmap baz
		EOF
		git grep --column -w -e bar -e mmap $H >actual &&
		test_cmp expected actual
	'

	test_expect_success "grep -w $L" '
		cat >expected <<-EOF &&
		${HC}file:1:foo mmap bar
		${HC}file:3:foo_mmap bar mmap
		${HC}file:4:foo mmap bar_mmap
		${HC}file:5:foo_mmap bar mmap baz
		EOF
		git -c grep.linenumber=true grep -w -e mmap $H >actual &&
		test_cmp expected actual
	'

	test_expect_success "grep -w $L" '
		cat >expected <<-EOF &&
		${HC}file:foo mmap bar
		${HC}file:foo_mmap bar mmap
		${HC}file:foo mmap bar_mmap
		${HC}file:foo_mmap bar mmap baz
		EOF
		git -c grep.linenumber=true grep --no-line-number -w -e mmap $H >actual &&
		test_cmp expected actual
	'

	test_expect_success "grep -w $L (w)" '
		test_must_fail git grep -n -w -e "^w" $H >actual &&
		test_must_be_empty actual
	'

	test_expect_success "grep -w $L (x)" '
		cat >expected <<-EOF &&
		${HC}x:1:x x xx x
		EOF
		git grep -n -w -e "x xx* x" $H >actual &&
		test_cmp expected actual
	'

	test_expect_success "grep -w $L (y-1)" '
		cat >expected <<-EOF &&
		${HC}y:1:y yy
		EOF
		git grep -n -w -e "^y" $H >actual &&
		test_cmp expected actual
	'

	test_expect_success "grep -w $L (y-2)" '
		if git grep -n -w -e "^y y" $H >actual
		then
			echo should not have matched
			cat actual
			false
		else
			test_must_be_empty actual
		fi
	'

	test_expect_success "grep -w $L (z)" '
		if git grep -n -w -e "^z" $H >actual
		then
			echo should not have matched
			cat actual
			false
		else
			test_must_be_empty actual
		fi
	'

	test_expect_success "grep $L (with --column, --only-matching)" '
		cat >expected <<-EOF &&
		${HC}file:1:5:mmap
		${HC}file:2:5:mmap
		${HC}file:3:5:mmap
		${HC}file:3:13:mmap
		${HC}file:4:5:mmap
		${HC}file:4:13:mmap
		${HC}file:5:5:mmap
		${HC}file:5:13:mmap
		EOF
		git grep --column -n -o -e mmap $H >actual &&
		test_cmp expected actual
	'

	test_expect_success "grep $L (t-1)" '
		echo "${HC}t/t:1:test" >expected &&
		git grep -n -e test $H >actual &&
		test_cmp expected actual
	'

	test_expect_success "grep $L (t-2)" '
		echo "${HC}t:1:test" >expected &&
		(
			cd t &&
			git grep -n -e test $H
		) >actual &&
		test_cmp expected actual
	'

	test_expect_success "grep $L (t-3)" '
		echo "${HC}t/t:1:test" >expected &&
		(
			cd t &&
			git grep --full-name -n -e test $H
		) >actual &&
		test_cmp expected actual
	'

	test_expect_success "grep -c $L (no /dev/null)" '
		! git grep -c test $H | grep /dev/null
	'

	test_expect_success "grep --max-depth -1 $L" '
		cat >expected <<-EOF &&
		${HC}t/a/v:1:vvv
		${HC}t/v:1:vvv
		${HC}v:1:vvv
		EOF
		git grep --max-depth -1 -n -e vvv $H >actual &&
		test_cmp expected actual &&
		git grep --recursive -n -e vvv $H >actual &&
		test_cmp expected actual
	'

	test_expect_success "grep --max-depth 0 $L" '
		cat >expected <<-EOF &&
		${HC}v:1:vvv
		EOF
		git grep --max-depth 0 -n -e vvv $H >actual &&
		test_cmp expected actual &&
		git grep --no-recursive -n -e vvv $H >actual &&
		test_cmp expected actual
	'

	test_expect_success "grep --max-depth 0 -- '*' $L" '
		cat >expected <<-EOF &&
		${HC}t/a/v:1:vvv
		${HC}t/v:1:vvv
		${HC}v:1:vvv
		EOF
		git grep --max-depth 0 -n -e vvv $H -- "*" >actual &&
		test_cmp expected actual &&
		git grep --no-recursive -n -e vvv $H -- "*" >actual &&
		test_cmp expected actual
	'

	test_expect_success "grep --max-depth 1 $L" '
		cat >expected <<-EOF &&
		${HC}t/v:1:vvv
		${HC}v:1:vvv
		EOF
		git grep --max-depth 1 -n -e vvv $H >actual &&
		test_cmp expected actual
	'

	test_expect_success "grep --max-depth 0 -- t $L" '
		cat >expected <<-EOF &&
		${HC}t/v:1:vvv
		EOF
		git grep --max-depth 0 -n -e vvv $H -- t >actual &&
		test_cmp expected actual &&
		git grep --no-recursive -n -e vvv $H -- t >actual &&
		test_cmp expected actual
	'

	test_expect_success "grep --max-depth 0 -- . t $L" '
		cat >expected <<-EOF &&
		${HC}t/v:1:vvv
		${HC}v:1:vvv
		EOF
		git grep --max-depth 0 -n -e vvv $H -- . t >actual &&
		test_cmp expected actual &&
		git grep --no-recursive -n -e vvv $H -- . t >actual &&
		test_cmp expected actual
	'

	test_expect_success "grep --max-depth 0 -- t . $L" '
		cat >expected <<-EOF &&
		${HC}t/v:1:vvv
		${HC}v:1:vvv
		EOF
		git grep --max-depth 0 -n -e vvv $H -- t . >actual &&
		test_cmp expected actual &&
		git grep --no-recursive -n -e vvv $H -- t . >actual &&
		test_cmp expected actual
	'


	test_pattern_type "$H" "$HC" "$L" BRE -c grep.extendedRegexp=false
	test_pattern_type "$H" "$HC" "$L" ERE -c grep.extendedRegexp=true
	test_pattern_type "$H" "$HC" "$L" BRE -c grep.patternType=basic
	test_pattern_type "$H" "$HC" "$L" ERE -c grep.patternType=extended
	test_pattern_type "$H" "$HC" "$L" FIX -c grep.patternType=fixed

	test_expect_success PCRE "grep $L with grep.patterntype=perl" '
		echo "${HC}ab:a+b*c" >expected &&
		git -c grep.patterntype=perl grep "a\x{2b}b\x{2a}c" $H ab >actual &&
		test_cmp expected actual
	'

	test_expect_success !FAIL_PREREQS,!PCRE "grep $L with grep.patterntype=perl errors without PCRE" '
		test_must_fail git -c grep.patterntype=perl grep "foo.*bar"
	'

	test_pattern_type "$H" "$HC" "$L" ERE \
		-c grep.patternType=default \
		-c grep.extendedRegexp=true
	test_pattern_type "$H" "$HC" "$L" ERE \
		-c grep.extendedRegexp=true \
		-c grep.patternType=default
	test_pattern_type "$H" "$HC" "$L" ERE \
		-c grep.patternType=extended \
		-c grep.extendedRegexp=false
	test_pattern_type "$H" "$HC" "$L" BRE \
		-c grep.patternType=basic \
		-c grep.extendedRegexp=true
	test_pattern_type "$H" "$HC" "$L" ERE \
		-c grep.extendedRegexp=false \
		-c grep.patternType=extended
	test_pattern_type "$H" "$HC" "$L" BRE \
		-c grep.extendedRegexp=true \
		-c grep.patternType=basic

	# grep.extendedRegexp is last-one-wins
	test_pattern_type "$H" "$HC" "$L" BRE \
		-c grep.extendedRegexp=true \
		-c grep.extendedRegexp=false

	# grep.patternType=basic pays no attention to grep.extendedRegexp
	test_pattern_type "$H" "$HC" "$L" BRE \
		-c grep.extendedRegexp=true \
		-c grep.patternType=basic \
		-c grep.extendedRegexp=false

	# grep.patternType=extended pays no attention to grep.extendedRegexp
	test_pattern_type "$H" "$HC" "$L" ERE \
		-c grep.extendedRegexp=true \
		-c grep.patternType=extended \
		-c grep.extendedRegexp=false

	# grep.extendedRegexp is used with a last-one-wins grep.patternType=default
	test_pattern_type "$H" "$HC" "$L" ERE \
		-c grep.patternType=fixed \
		-c grep.extendedRegexp=true \
		-c grep.patternType=default

	# grep.extendedRegexp is used with earlier grep.patternType=default
	test_pattern_type "$H" "$HC" "$L" ERE \
		-c grep.extendedRegexp=false \
		-c grep.patternType=default \
		-c grep.extendedRegexp=true

	# grep.extendedRegexp is used with a last-one-loses grep.patternType=default
	test_pattern_type "$H" "$HC" "$L" ERE \
		-c grep.extendedRegexp=false \
		-c grep.extendedRegexp=true \
		-c grep.patternType=default

	# grep.extendedRegexp and grep.patternType are both last-one-wins independently
	test_pattern_type "$H" "$HC" "$L" BRE \
		-c grep.patternType=default \
		-c grep.extendedRegexp=true \
		-c grep.patternType=basic

	# grep.patternType=extended and grep.patternType=default
	test_pattern_type "$H" "$HC" "$L" BRE \
		-c grep.patternType=extended \
		-c grep.patternType=default

	# grep.patternType=[extended -> default -> fixed] (BRE)" '
	test_pattern_type "$H" "$HC" "$L" FIX \
		-c grep.patternType=extended \
		-c grep.patternType=default \
		-c grep.patternType=fixed

	test_expect_success "grep --count $L" '
		echo ${HC}ab:3 >expected &&
		git grep --count -e b $H -- ab >actual &&
		test_cmp expected actual
	'

	test_expect_success "grep --count -h $L" '
		echo 3 >expected &&
		git grep --count -h -e b $H -- ab >actual &&
		test_cmp expected actual
	'

	test_expect_success "grep $L searches past invalid lines on UTF-8 locale" '
		LC_ALL=en_US.UTF-8 git grep A. invalid-utf8 >actual &&
		cat >expected <<-EOF &&
		invalid-utf8:ASCII
		EOF
		test_cmp expected actual
	'

	test_expect_success FUNNYNAMES "grep $L should quote unusual pathnames" '
		cat >expected <<-EOF &&
		${HC}"\"unusual\" pathname":unusual
		${HC}"t/nested \"unusual\" pathname":unusual
		EOF
		git grep unusual $H >actual &&
		test_cmp expected actual
	'

	test_expect_success FUNNYNAMES "grep $L in subdir should quote unusual relative pathnames" '
		cat >expected <<-EOF &&
		${HC}"nested \"unusual\" pathname":unusual
		EOF
		(
			cd t &&
			git grep unusual $H
		) >actual &&
		test_cmp expected actual
	'

	test_expect_success FUNNYNAMES "grep -z $L with unusual pathnames" '
		cat >expected <<-EOF &&
		${HC}"unusual" pathname:unusual
		${HC}t/nested "unusual" pathname:unusual
		EOF
		git grep -z unusual $H >actual &&
		tr "\0" ":" <actual >actual-replace-null &&
		test_cmp expected actual-replace-null
	'

	test_expect_success FUNNYNAMES "grep -z $L in subdir with unusual relative pathnames" '
		cat >expected <<-EOF &&
		${HC}nested "unusual" pathname:unusual
		EOF
		(
			cd t &&
			git grep -z unusual $H
		) >actual &&
		tr "\0" ":" <actual >actual-replace-null &&
		test_cmp expected actual-replace-null
	'
done

test_expect_success MB_REGEX 'grep exactly one char in single-char multibyte file' '
	LC_ALL=en_US.UTF-8 git grep "^.$" reverse-question-mark
'

test_expect_success MB_REGEX 'grep two chars in single-char multibyte file' '
	LC_ALL=en_US.UTF-8 test_expect_code 1 git grep ".." reverse-question-mark
'

cat >expected <<EOF
file
EOF
test_expect_success 'grep -l -C' '
	git grep -l -C1 foo >actual &&
	test_cmp expected actual
'

cat >expected <<EOF
file:5
EOF
test_expect_success 'grep -c -C' '
	git grep -c -C1 foo >actual &&
	test_cmp expected actual
'

test_expect_success 'grep -L -C' '
	git ls-files >expected &&
	git grep -L -C1 nonexistent_string >actual &&
	test_cmp expected actual
'

test_expect_success 'grep --files-without-match --quiet' '
	git grep --files-without-match --quiet nonexistent_string >actual &&
	test_must_be_empty actual
'

test_expect_success 'grep --max-count 0 (must exit with non-zero)' '
	test_must_fail git grep --max-count 0 foo >actual &&
	test_must_be_empty actual
'

test_expect_success 'grep --max-count 3' '
	cat >expected <<-EOF &&
	file:foo mmap bar
	file:foo_mmap bar
	file:foo_mmap bar mmap
	EOF
	git grep --max-count 3 foo >actual &&
	test_cmp expected actual
'

test_expect_success 'grep --max-count -1 (no limit)' '
	cat >expected <<-EOF &&
	file:foo mmap bar
	file:foo_mmap bar
	file:foo_mmap bar mmap
	file:foo mmap bar_mmap
	file:foo_mmap bar mmap baz
	EOF
	git grep --max-count -1 foo >actual &&
	test_cmp expected actual
'

test_expect_success 'grep --max-count 1 --context 2' '
	cat >expected <<-EOF &&
	file-foo mmap bar
	file:foo_mmap bar
	file-foo_mmap bar mmap
	EOF
	git grep --max-count 1 --context 1 foo_mmap >actual &&
	test_cmp expected actual
'

test_expect_success 'grep --max-count 1 --show-function' '
	cat >expected <<-EOF &&
	hello.ps1=function hello() {
	hello.ps1:  echo "Hello world."
	EOF
	git grep --max-count 1 --show-function Hello hello.ps1 >actual &&
	test_cmp expected actual
'

test_expect_success 'grep --max-count 2 --show-function' '
	cat >expected <<-EOF &&
	hello.ps1=function hello() {
	hello.ps1:  echo "Hello world."
	hello.ps1:  echo "Hello again."
	EOF
	git grep --max-count 2 --show-function Hello hello.ps1 >actual &&
	test_cmp expected actual
'

test_expect_success 'grep --max-count 1 --count' '
	cat >expected <<-EOF &&
	hello.ps1:1
	EOF
	git grep --max-count 1 --count Hello hello.ps1 >actual &&
	test_cmp expected actual
'

test_expect_success 'grep --max-count 1 (multiple files)' '
	cat >expected <<-EOF &&
	hello.c:#include <stdio.h>
	hello.ps1:# No-op.
	EOF
	git grep --max-count 1 -e o -- hello.\* >actual &&
	test_cmp expected actual
'

test_expect_success 'grep --max-count 1 --context 1 (multiple files)' '
	cat >expected <<-EOF &&
	hello.c-#include <assert.h>
	hello.c:#include <stdio.h>
	hello.c-
	--
	hello.ps1:# No-op.
	hello.ps1-function dummy() {}
	EOF
	git grep --max-count 1 --context 1 -e o -- hello.\* >actual &&
	test_cmp expected actual
'

cat >expected <<EOF
file:foo mmap bar_mmap
EOF

test_expect_success 'grep -e A --and -e B' '
	git grep -e "foo mmap" --and -e bar_mmap >actual &&
	test_cmp expected actual
'

cat >expected <<EOF
file:foo_mmap bar mmap
file:foo_mmap bar mmap baz
EOF


test_expect_success 'grep ( -e A --or -e B ) --and -e B' '
	git grep \( -e foo_ --or -e baz \) \
		--and -e " mmap" >actual &&
	test_cmp expected actual
'

cat >expected <<EOF
file:foo mmap bar
EOF

test_expect_success 'grep -e A --and --not -e B' '
	git grep -e "foo mmap" --and --not -e bar_mmap >actual &&
	test_cmp expected actual
'

test_expect_success 'grep should ignore GREP_OPTIONS' '
	GREP_OPTIONS=-v git grep " mmap bar\$" >actual &&
	test_cmp expected actual
'

test_expect_success 'grep -f, non-existent file' '
	test_must_fail git grep -f patterns
'

cat >expected <<EOF
file:foo mmap bar
file:foo_mmap bar
file:foo_mmap bar mmap
file:foo mmap bar_mmap
file:foo_mmap bar mmap baz
EOF

cat >pattern <<EOF
mmap
EOF

test_expect_success 'grep -f, one pattern' '
	git grep -f pattern >actual &&
	test_cmp expected actual
'

cat >expected <<EOF
file:foo mmap bar
file:foo_mmap bar
file:foo_mmap bar mmap
file:foo mmap bar_mmap
file:foo_mmap bar mmap baz
t/a/v:vvv
t/v:vvv
v:vvv
EOF

cat >patterns <<EOF
mmap
vvv
EOF

test_expect_success 'grep -f, multiple patterns' '
	git grep -f patterns >actual &&
	test_cmp expected actual
'

test_expect_success 'grep, multiple patterns' '
	git grep "$(cat patterns)" >actual &&
	test_cmp expected actual
'

cat >expected <<EOF
file:foo mmap bar
file:foo_mmap bar
file:foo_mmap bar mmap
file:foo mmap bar_mmap
file:foo_mmap bar mmap baz
t/a/v:vvv
t/v:vvv
v:vvv
EOF

cat >patterns <<EOF

mmap

vvv

EOF

test_expect_success 'grep -f, ignore empty lines' '
	git grep -f patterns >actual &&
	test_cmp expected actual
'

test_expect_success 'grep -f, ignore empty lines, read patterns from stdin' '
	git grep -f - <patterns >actual &&
	test_cmp expected actual
'

test_expect_success 'grep -f, use cwd relative file' '
	test_when_finished "git rm -f sub/dir/file" &&
	mkdir -p sub/dir &&
	echo hit >sub/dir/file &&
	git add sub/dir/file &&
	echo hit >sub/dir/pattern &&
	echo miss >pattern &&
	(
		cd sub/dir && git grep -f pattern file
	) &&
	git -C sub/dir grep -f pattern file
'

cat >expected <<EOF
y:y yy
--
z:zzz
EOF

test_expect_success 'grep -q, silently report matches' '
	git grep -q mmap >actual &&
	test_must_be_empty actual &&
	test_must_fail git grep -q qfwfq >actual &&
	test_must_be_empty actual
'

test_expect_success 'grep -C1 hunk mark between files' '
	git grep -C1 "^[yz]" >actual &&
	test_cmp expected actual
'

test_expect_success 'log grep setup' '
	test_commit --append --author "With * Asterisk <xyzzy@frotz.com>" second file a &&
	test_commit --append third file a &&
	test_commit --append --author "Night Fall <nitfol@frobozz.com>" fourth file a
'

test_expect_success 'log grep (1)' '
	git log --author=author --pretty=tformat:%s >actual &&
	{
		echo third && echo initial
	} >expect &&
	test_cmp expect actual
'

test_expect_success 'log grep (2)' '
	git log --author=" * " -F --pretty=tformat:%s >actual &&
	{
		echo second
	} >expect &&
	test_cmp expect actual
'

test_expect_success 'log grep (3)' '
	git log --author="^A U" --pretty=tformat:%s >actual &&
	{
		echo third && echo initial
	} >expect &&
	test_cmp expect actual
'

test_expect_success 'log grep (4)' '
	git log --author="frotz\.com>$" --pretty=tformat:%s >actual &&
	{
		echo second
	} >expect &&
	test_cmp expect actual
'

test_expect_success 'log grep (5)' '
	git log --author=Thor -F --pretty=tformat:%s >actual &&
	{
		echo third && echo initial
	} >expect &&
	test_cmp expect actual
'

test_expect_success 'log grep (6)' '
	git log --author=-0700  --pretty=tformat:%s >actual &&
	test_must_be_empty actual
'

test_expect_success 'log grep (7)' '
	git log -g --grep-reflog="commit: third" --pretty=tformat:%s >actual &&
	echo third >expect &&
	test_cmp expect actual
'

test_expect_success 'log grep (8)' '
	git log -g --grep-reflog="commit: third" --grep-reflog="commit: second" --pretty=tformat:%s >actual &&
	{
		echo third && echo second
	} >expect &&
	test_cmp expect actual
'

test_expect_success 'log grep (9)' '
	git log -g --grep-reflog="commit: third" --author="Thor" --pretty=tformat:%s >actual &&
	echo third >expect &&
	test_cmp expect actual
'

test_expect_success 'log grep (9)' '
	git log -g --grep-reflog="commit: third" --author="non-existent" --pretty=tformat:%s >actual &&
	test_must_be_empty actual
'

test_expect_success 'log --grep-reflog can only be used under -g' '
	test_must_fail git log --grep-reflog="commit: third"
'

test_expect_success 'log with multiple --grep uses union' '
	git log --grep=i --grep=r --format=%s >actual &&
	{
		echo fourth && echo third && echo initial
	} >expect &&
	test_cmp expect actual
'

test_expect_success 'log --all-match with multiple --grep uses intersection' '
	git log --all-match --grep=i --grep=r --format=%s >actual &&
	{
		echo third
	} >expect &&
	test_cmp expect actual
'

test_expect_success 'log with multiple --author uses union' '
	git log --author="Thor" --author="Aster" --format=%s >actual &&
	{
	    echo third && echo second && echo initial
	} >expect &&
	test_cmp expect actual
'

test_expect_success 'log --all-match with multiple --author still uses union' '
	git log --all-match --author="Thor" --author="Aster" --format=%s >actual &&
	{
	    echo third && echo second && echo initial
	} >expect &&
	test_cmp expect actual
'

test_expect_success 'log --grep --author uses intersection' '
	# grep matches only third and fourth
	# author matches only initial and third
	git log --author="A U Thor" --grep=r --format=%s >actual &&
	{
		echo third
	} >expect &&
	test_cmp expect actual
'

test_expect_success 'log --grep --grep --author takes union of greps and intersects with author' '
	# grep matches initial and second but not third
	# author matches only initial and third
	git log --author="A U Thor" --grep=s --grep=l --format=%s >actual &&
	{
		echo initial
	} >expect &&
	test_cmp expect actual
'

test_expect_success 'log ---all-match -grep --author --author still takes union of authors and intersects with grep' '
	# grep matches only initial and third
	# author matches all but second
	git log --all-match --author="Thor" --author="Night" --grep=i --format=%s >actual &&
	{
	    echo third && echo initial
	} >expect &&
	test_cmp expect actual
'

test_expect_success 'log --grep --author --author takes union of authors and intersects with grep' '
	# grep matches only initial and third
	# author matches all but second
	git log --author="Thor" --author="Night" --grep=i --format=%s >actual &&
	{
	    echo third && echo initial
	} >expect &&
	test_cmp expect actual
'

test_expect_success 'log --all-match --grep --grep --author takes intersection' '
	# grep matches only third
	# author matches only initial and third
	git log --all-match --author="A U Thor" --grep=i --grep=r --format=%s >actual &&
	{
		echo third
	} >expect &&
	test_cmp expect actual
'

test_expect_success 'log --author does not search in timestamp' '
	git log --author="$GIT_AUTHOR_DATE" >actual &&
	test_must_be_empty actual
'

test_expect_success 'log --committer does not search in timestamp' '
	git log --committer="$GIT_COMMITTER_DATE" >actual &&
	test_must_be_empty actual
'

test_expect_success 'grep with CE_VALID file' '
	git update-index --assume-unchanged t/t &&
	rm t/t &&
	echo "t/t:test" >expect &&
	git grep test >actual &&
	test_cmp expect actual &&
	git update-index --no-assume-unchanged t/t &&
	git checkout t/t
'

cat >expected <<EOF
hello.c=#include <stdio.h>
hello.c:	return 0;
EOF

test_expect_success 'grep -p with userdiff' '
	git config diff.custom.funcname "^#" &&
	echo "hello.c diff=custom" >.gitattributes &&
	git grep -p return >actual &&
	test_cmp expected actual
'

cat >expected <<EOF
hello.c=int main(int argc, const char **argv)
hello.c:	return 0;
EOF

test_expect_success 'grep -p' '
	rm -f .gitattributes &&
	git grep -p return >actual &&
	test_cmp expected actual
'

cat >expected <<EOF
hello.c-#include <stdio.h>
hello.c-
hello.c=int main(int argc, const char **argv)
hello.c-{
hello.c-	printf("Hello world.\n");
hello.c:	return 0;
EOF

test_expect_success 'grep -p -B5' '
	git grep -p -B5 return >actual &&
	test_cmp expected actual
'

cat >expected <<EOF
hello.c=int main(int argc, const char **argv)
hello.c-{
hello.c-	printf("Hello world.\n");
hello.c:	return 0;
hello.c-	/* char ?? */
hello.c-}
EOF

test_expect_success 'grep -W' '
	git grep -W return >actual &&
	test_cmp expected actual
'

cat >expected <<EOF
hello.c-#include <assert.h>
hello.c:#include <stdio.h>
EOF

test_expect_success 'grep -W shows no trailing empty lines' '
	git grep -W stdio >actual &&
	test_cmp expected actual
'

test_expect_success 'grep -W with userdiff' '
	test_when_finished "rm -f .gitattributes" &&
	git config diff.custom.xfuncname "^function .*$" &&
	echo "hello.ps1 diff=custom" >.gitattributes &&
	git grep -W echo >function-context-userdiff-actual
'

test_expect_success ' includes preceding comment' '
	grep "# Say hello" function-context-userdiff-actual
'

test_expect_success ' includes function line' '
	grep "=function hello" function-context-userdiff-actual
'

test_expect_success ' includes matching line' '
	grep ":  echo" function-context-userdiff-actual
'

test_expect_success ' includes last line of the function' '
	grep "} # hello" function-context-userdiff-actual
'

for threads in $(test_seq 0 10)
do
	test_expect_success "grep --threads=$threads & -c grep.threads=$threads" "
		git grep --threads=$threads . >actual.$threads &&
		if test $threads -ge 1
		then
			test_cmp actual.\$(($threads - 1)) actual.$threads
		fi &&
		git -c grep.threads=$threads grep . >actual.$threads &&
		if test $threads -ge 1
		then
			test_cmp actual.\$(($threads - 1)) actual.$threads
		fi
	"
done

test_expect_success !PTHREADS,!FAIL_PREREQS \
	'grep --threads=N or pack.threads=N warns when no pthreads' '
	git grep --threads=2 Hello hello_world 2>err &&
	grep ^warning: err >warnings &&
	test_line_count = 1 warnings &&
	grep -F "no threads support, ignoring --threads" err &&
	git -c grep.threads=2 grep Hello hello_world 2>err &&
	grep ^warning: err >warnings &&
	test_line_count = 1 warnings &&
	grep -F "no threads support, ignoring grep.threads" err &&
	git -c grep.threads=2 grep --threads=4 Hello hello_world 2>err &&
	grep ^warning: err >warnings &&
	test_line_count = 2 warnings &&
	grep -F "no threads support, ignoring --threads" err &&
	grep -F "no threads support, ignoring grep.threads" err &&
	git -c grep.threads=0 grep --threads=0 Hello hello_world 2>err &&
	test_line_count = 0 err
'

test_expect_success 'grep from a subdirectory to search wider area (1)' '
	mkdir -p s &&
	(
		cd s && git grep "x x x" ..
	)
'

test_expect_success 'grep from a subdirectory to search wider area (2)' '
	mkdir -p s &&
	(
		cd s &&
		test_expect_code 1 git grep xxyyzz .. >out &&
		test_must_be_empty out
	)
'

cat >expected <<EOF
hello.c:int main(int argc, const char **argv)
EOF

test_expect_success 'grep -Fi' '
	git grep -Fi "CHAR *" >actual &&
	test_cmp expected actual
'

test_expect_success 'outside of git repository' '
	rm -fr non &&
	mkdir -p non/git/sub &&
	echo hello >non/git/file1 &&
	echo world >non/git/sub/file2 &&
	{
		echo file1:hello &&
		echo sub/file2:world
	} >non/expect.full &&
	echo file2:world >non/expect.sub &&
	(
		GIT_CEILING_DIRECTORIES="$(pwd)/non" &&
		export GIT_CEILING_DIRECTORIES &&
		cd non/git &&
		test_must_fail git grep o &&
		git grep --no-index o >../actual.full &&
		test_cmp ../expect.full ../actual.full &&
		cd sub &&
		test_must_fail git grep o &&
		git grep --no-index o >../../actual.sub &&
		test_cmp ../../expect.sub ../../actual.sub
	) &&

	echo ".*o*" >non/git/.gitignore &&
	(
		GIT_CEILING_DIRECTORIES="$(pwd)/non" &&
		export GIT_CEILING_DIRECTORIES &&
		cd non/git &&
		test_must_fail git grep o &&
		git grep --no-index --exclude-standard o >../actual.full &&
		test_cmp ../expect.full ../actual.full &&

		{
			echo ".gitignore:.*o*" &&
			cat ../expect.full
		} >../expect.with.ignored &&
		git grep --no-index --no-exclude-standard o >../actual.full &&
		test_cmp ../expect.with.ignored ../actual.full
	)
'

test_expect_success 'outside of git repository with fallbackToNoIndex' '
	rm -fr non &&
	mkdir -p non/git/sub &&
	echo hello >non/git/file1 &&
	echo world >non/git/sub/file2 &&
	cat <<-\EOF >non/expect.full &&
	file1:hello
	sub/file2:world
	EOF
	echo file2:world >non/expect.sub &&
	(
		GIT_CEILING_DIRECTORIES="$(pwd)/non" &&
		export GIT_CEILING_DIRECTORIES &&
		cd non/git &&
		test_must_fail git -c grep.fallbackToNoIndex=false grep o &&
		git -c grep.fallbackToNoIndex=true grep o >../actual.full &&
		test_cmp ../expect.full ../actual.full &&
		cd sub &&
		test_must_fail git -c grep.fallbackToNoIndex=false grep o &&
		git -c grep.fallbackToNoIndex=true grep o >../../actual.sub &&
		test_cmp ../../expect.sub ../../actual.sub
	) &&

	echo ".*o*" >non/git/.gitignore &&
	(
		GIT_CEILING_DIRECTORIES="$(pwd)/non" &&
		export GIT_CEILING_DIRECTORIES &&
		cd non/git &&
		test_must_fail git -c grep.fallbackToNoIndex=false grep o &&
		git -c grep.fallbackToNoIndex=true grep --exclude-standard o >../actual.full &&
		test_cmp ../expect.full ../actual.full &&

		{
			echo ".gitignore:.*o*" &&
			cat ../expect.full
		} >../expect.with.ignored &&
		git -c grep.fallbackToNoIndex grep --no-exclude-standard o >../actual.full &&
		test_cmp ../expect.with.ignored ../actual.full
	)
'

test_expect_success 'no repository with path outside $cwd' '
	test_when_finished rm -fr non &&
	rm -fr non &&
	mkdir -p non/git/sub non/tig &&
	(
		GIT_CEILING_DIRECTORIES="$(pwd)/non" &&
		export GIT_CEILING_DIRECTORIES &&
		cd non/git &&
		test_expect_code 128 git grep --no-index search .. 2>error &&
		grep "is outside the directory tree" error
	) &&
	(
		GIT_CEILING_DIRECTORIES="$(pwd)/non" &&
		export GIT_CEILING_DIRECTORIES &&
		cd non/git &&
		test_expect_code 128 git grep --no-index search ../tig 2>error &&
		grep "is outside the directory tree" error
	) &&
	(
		GIT_CEILING_DIRECTORIES="$(pwd)/non" &&
		export GIT_CEILING_DIRECTORIES &&
		cd non/git &&
		test_expect_code 128 git grep --no-index search ../non 2>error &&
		grep "no such path in the working tree" error
	)
'

test_expect_success 'inside git repository but with --no-index' '
	rm -fr is &&
	mkdir -p is/git/sub &&
	echo hello >is/git/file1 &&
	echo world >is/git/sub/file2 &&
	echo ".*o*" >is/git/.gitignore &&
	{
		echo file1:hello &&
		echo sub/file2:world
	} >is/expect.unignored &&
	{
		echo ".gitignore:.*o*" &&
		cat is/expect.unignored
	} >is/expect.full &&
	echo file2:world >is/expect.sub &&
	(
		cd is/git &&
		git init &&
		test_must_fail git grep o >../actual.full &&
		test_must_be_empty ../actual.full &&

		git grep --untracked o >../actual.unignored &&
		test_cmp ../expect.unignored ../actual.unignored &&

		git grep --no-index o >../actual.full &&
		test_cmp ../expect.full ../actual.full &&

		git grep --no-index --exclude-standard o >../actual.unignored &&
		test_cmp ../expect.unignored ../actual.unignored &&

		cd sub &&
		test_must_fail git grep o >../../actual.sub &&
		test_must_be_empty ../../actual.sub &&

		git grep --no-index o >../../actual.sub &&
		test_cmp ../../expect.sub ../../actual.sub &&

		git grep --untracked o >../../actual.sub &&
		test_cmp ../../expect.sub ../../actual.sub
	)
'

test_expect_success 'grep --no-index descends into repos, but not .git' '
	rm -fr non &&
	mkdir -p non/git &&
	(
		GIT_CEILING_DIRECTORIES="$(pwd)/non" &&
		export GIT_CEILING_DIRECTORIES &&
		cd non/git &&

		echo magic >file &&
		git init repo &&
		(
			cd repo &&
			echo magic >file &&
			git add file &&
			git commit -m foo &&
			echo magic >.git/file
		) &&

		cat >expect <<-\EOF &&
		file
		repo/file
		EOF
		git grep -l --no-index magic >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'setup double-dash tests' '
cat >double-dash <<EOF &&
--
->
other
EOF
git add double-dash
'

cat >expected <<EOF
double-dash:->
EOF
test_expect_success 'grep -- pattern' '
	git grep -- "->" >actual &&
	test_cmp expected actual
'
test_expect_success 'grep -- pattern -- pathspec' '
	git grep -- "->" -- double-dash >actual &&
	test_cmp expected actual
'
test_expect_success 'grep -e pattern -- path' '
	git grep -e "->" -- double-dash >actual &&
	test_cmp expected actual
'

cat >expected <<EOF
double-dash:--
EOF
test_expect_success 'grep -e -- -- path' '
	git grep -e -- -- double-dash >actual &&
	test_cmp expected actual
'

test_expect_success 'dashdash disambiguates rev as rev' '
	test_when_finished "rm -f main" &&
	echo content >main &&
	echo main:hello.c >expect &&
	git grep -l o main -- hello.c >actual &&
	test_cmp expect actual
'

test_expect_success 'dashdash disambiguates pathspec as pathspec' '
	test_when_finished "git rm -f main" &&
	echo content >main &&
	git add main &&
	echo main:content >expect &&
	git grep o -- main >actual &&
	test_cmp expect actual
'

test_expect_success 'report bogus arg without dashdash' '
	test_must_fail git grep o does-not-exist
'

test_expect_success 'report bogus rev with dashdash' '
	test_must_fail git grep o hello.c --
'

test_expect_success 'allow non-existent path with dashdash' '
	# We need a real match so grep exits with success.
	tree=$(git ls-tree HEAD |
	       sed s/hello.c/not-in-working-tree/ |
	       git mktree) &&
	git grep o "$tree" -- not-in-working-tree
'

test_expect_success 'grep --no-index pattern -- path' '
	rm -fr non &&
	mkdir -p non/git &&
	(
		GIT_CEILING_DIRECTORIES="$(pwd)/non" &&
		export GIT_CEILING_DIRECTORIES &&
		cd non/git &&
		echo hello >hello &&
		echo goodbye >goodbye &&
		echo hello:hello >expect &&
		git grep --no-index o -- hello >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'grep --no-index complains of revs' '
	test_must_fail git grep --no-index o main -- 2>err &&
	test_grep "cannot be used with revs" err
'

test_expect_success 'grep --no-index prefers paths to revs' '
	test_when_finished "rm -f main" &&
	echo content >main &&
	echo main:content >expect &&
	git grep --no-index o main >actual &&
	test_cmp expect actual
'

test_expect_success 'grep --no-index does not "diagnose" revs' '
	test_must_fail git grep --no-index o :1:hello.c 2>err &&
	test_grep ! -i "did you mean" err
'

cat >expected <<EOF
hello.c:int main(int argc, const char **argv)
hello.c:	printf("Hello world.\n");
EOF

test_expect_success PCRE 'grep --perl-regexp pattern' '
	git grep --perl-regexp "\p{Ps}.*?\p{Pe}" hello.c >actual &&
	test_cmp expected actual
'

test_expect_success !FAIL_PREREQS,!PCRE 'grep --perl-regexp pattern errors without PCRE' '
	test_must_fail git grep --perl-regexp "foo.*bar"
'

test_expect_success PCRE 'grep -P pattern' '
	git grep -P "\p{Ps}.*?\p{Pe}" hello.c >actual &&
	test_cmp expected actual
'

test_expect_success LIBPCRE2 "grep -P with (*NO_JIT) doesn't error out" '
	git grep -P "(*NO_JIT)\p{Ps}.*?\p{Pe}" hello.c >actual &&
	test_cmp expected actual

'

test_expect_success !FAIL_PREREQS,!PCRE 'grep -P pattern errors without PCRE' '
	test_must_fail git grep -P "foo.*bar"
'

test_expect_success 'grep pattern with grep.extendedRegexp=true' '
	test_must_fail git -c grep.extendedregexp=true \
		grep "\p{Ps}.*?\p{Pe}" hello.c >actual &&
	test_must_be_empty actual
'

test_expect_success PCRE 'grep -P pattern with grep.extendedRegexp=true' '
	git -c grep.extendedregexp=true \
		grep -P "\p{Ps}.*?\p{Pe}" hello.c >actual &&
	test_cmp expected actual
'

test_expect_success PCRE 'grep -P -v pattern' '
	cat >expected <<-\EOF &&
	ab:a+b*c
	ab:a+bc
	EOF
	git grep -P -v "abc" ab >actual &&
	test_cmp expected actual
'

test_expect_success PCRE 'grep -P -i pattern' '
	cat >expected <<-EOF &&
	hello.c:	printf("Hello world.\n");
	EOF
	git grep -P -i "PRINTF\([^\d]+\)" hello.c >actual &&
	test_cmp expected actual
'

test_expect_success PCRE 'grep -P -w pattern' '
	cat >expected <<-\EOF &&
	hello_world:Hello world
	hello_world:HeLLo world
	EOF
	git grep -P -w "He((?i)ll)o" hello_world >actual &&
	test_cmp expected actual
'

test_expect_success PCRE 'grep -P backreferences work (the PCRE NO_AUTO_CAPTURE flag is not set)' '
	git grep -P -h "(?P<one>.)(?P=one)" hello_world >actual &&
	test_cmp hello_world actual &&
	git grep -P -h "(.)\1" hello_world >actual &&
	test_cmp hello_world actual
'

test_expect_success 'grep -G invalidpattern properly dies ' '
	test_must_fail git grep -G "a["
'

test_expect_success 'grep invalidpattern properly dies with grep.patternType=basic' '
	test_must_fail git -c grep.patterntype=basic grep "a["
'

test_expect_success 'grep -E invalidpattern properly dies ' '
	test_must_fail git grep -E "a["
'

test_expect_success 'grep invalidpattern properly dies with grep.patternType=extended' '
	test_must_fail git -c grep.patterntype=extended grep "a["
'

test_expect_success PCRE 'grep -P invalidpattern properly dies ' '
	test_must_fail git grep -P "a["
'

test_expect_success PCRE 'grep invalidpattern properly dies with grep.patternType=perl' '
	test_must_fail git -c grep.patterntype=perl grep "a["
'

test_expect_success 'grep -G -E -F pattern' '
	echo "ab:a+b*c" >expected &&
	git grep -G -E -F "a+b*c" ab >actual &&
	test_cmp expected actual
'

test_expect_success 'grep pattern with grep.patternType=basic, =extended, =fixed' '
	echo "ab:a+b*c" >expected &&
	git \
		-c grep.patterntype=basic \
		-c grep.patterntype=extended \
		-c grep.patterntype=fixed \
		grep "a+b*c" ab >actual &&
	test_cmp expected actual
'

test_expect_success 'grep -E -F -G pattern' '
	echo "ab:a+bc" >expected &&
	git grep -E -F -G "a+b*c" ab >actual &&
	test_cmp expected actual
'

test_expect_success 'grep pattern with grep.patternType=extended, =fixed, =basic' '
	echo "ab:a+bc" >expected &&
	git \
		-c grep.patterntype=extended \
		-c grep.patterntype=fixed \
		-c grep.patterntype=basic \
		grep "a+b*c" ab >actual &&
	test_cmp expected actual
'

test_expect_success 'grep -F -G -E pattern' '
	echo "ab:abc" >expected &&
	git grep -F -G -E "a+b*c" ab >actual &&
	test_cmp expected actual
'

test_expect_success 'grep pattern with grep.patternType=fixed, =basic, =extended' '
	echo "ab:abc" >expected &&
	git \
		-c grep.patterntype=fixed \
		-c grep.patterntype=basic \
		-c grep.patterntype=extended \
		grep "a+b*c" ab >actual &&
	test_cmp expected actual
'

test_expect_success 'grep -G -F -P -E pattern' '
	echo "d0:d" >expected &&
	git grep -G -F -P -E "[\d]" d0 >actual &&
	test_cmp expected actual
'

test_expect_success 'grep pattern with grep.patternType=fixed, =basic, =perl, =extended' '
	echo "d0:d" >expected &&
	git \
		-c grep.patterntype=fixed \
		-c grep.patterntype=basic \
		-c grep.patterntype=perl \
		-c grep.patterntype=extended \
		grep "[\d]" d0 >actual &&
	test_cmp expected actual
'

test_expect_success PCRE 'grep -G -F -E -P pattern' '
	echo "d0:0" >expected &&
	git grep -G -F -E -P "[\d]" d0 >actual &&
	test_cmp expected actual
'

test_expect_success PCRE 'grep pattern with grep.patternType=fixed, =basic, =extended, =perl' '
	echo "d0:0" >expected &&
	git \
		-c grep.patterntype=fixed \
		-c grep.patterntype=basic \
		-c grep.patterntype=extended \
		-c grep.patterntype=perl \
		grep "[\d]" d0 >actual &&
	test_cmp expected actual
'

test_expect_success PCRE 'grep -P pattern with grep.patternType=fixed' '
	echo "ab:a+b*c" >expected &&
	git \
		-c grep.patterntype=fixed \
		grep -P "a\x{2b}b\x{2a}c" ab >actual &&
	test_cmp expected actual
'

test_expect_success 'grep -F pattern with grep.patternType=basic' '
	echo "ab:a+b*c" >expected &&
	git \
		-c grep.patterntype=basic \
		grep -F "*c" ab >actual &&
	test_cmp expected actual
'

test_expect_success 'grep -G pattern with grep.patternType=fixed' '
	cat >expected <<-\EOF &&
	ab:a+b*c
	ab:a+bc
	EOF
	git \
		-c grep.patterntype=fixed \
		grep -G "a+b" ab >actual &&
	test_cmp expected actual
'

test_expect_success 'grep -E pattern with grep.patternType=fixed' '
	cat >expected <<-\EOF &&
	ab:a+b*c
	ab:a+bc
	ab:abc
	EOF
	git \
		-c grep.patterntype=fixed \
		grep -E "a+" ab >actual &&
	test_cmp expected actual
'

cat >expected <<EOF
hello.c<RED>:<RESET>int main(int argc, const char **argv)
hello.c<RED>-<RESET>{
<RED>--<RESET>
hello.c<RED>:<RESET>	/* char ?? */
hello.c<RED>-<RESET>}
<RED>--<RESET>
hello_world<RED>:<RESET>Hello_world
hello_world<RED>-<RESET>HeLLo_world
EOF

test_expect_success 'grep --color, separator' '
	test_config color.grep.context		normal &&
	test_config color.grep.filename		normal &&
	test_config color.grep.function		normal &&
	test_config color.grep.linenumber	normal &&
	test_config color.grep.match		normal &&
	test_config color.grep.selected		normal &&
	test_config color.grep.separator	red &&

	git grep --color=always -A1 -e char -e lo_w hello.c hello_world |
	test_decode_color >actual &&
	test_cmp expected actual
'

cat >expected <<EOF
hello.c:int main(int argc, const char **argv)
hello.c:	/* char ?? */

hello_world:Hello_world
EOF

test_expect_success 'grep --break' '
	git grep --break -e char -e lo_w hello.c hello_world >actual &&
	test_cmp expected actual
'

cat >expected <<EOF
hello.c:int main(int argc, const char **argv)
hello.c-{
--
hello.c:	/* char ?? */
hello.c-}

hello_world:Hello_world
hello_world-HeLLo_world
EOF

test_expect_success 'grep --break with context' '
	git grep --break -A1 -e char -e lo_w hello.c hello_world >actual &&
	test_cmp expected actual
'

cat >expected <<EOF
hello.c
int main(int argc, const char **argv)
	/* char ?? */
hello_world
Hello_world
EOF

test_expect_success 'grep --heading' '
	git grep --heading -e char -e lo_w hello.c hello_world >actual &&
	test_cmp expected actual
'

cat >expected <<EOF
<BOLD;GREEN>hello.c<RESET>
4:int main(int argc, const <BLACK;BYELLOW>char<RESET> **argv)
8:	/* <BLACK;BYELLOW>char<RESET> ?? */

<BOLD;GREEN>hello_world<RESET>
3:Hel<BLACK;BYELLOW>lo_w<RESET>orld
EOF

test_expect_success 'mimic ack-grep --group' '
	test_config color.grep.context		normal &&
	test_config color.grep.filename		"bold green" &&
	test_config color.grep.function		normal &&
	test_config color.grep.linenumber	normal &&
	test_config color.grep.match		"black yellow" &&
	test_config color.grep.selected		normal &&
	test_config color.grep.separator	normal &&

	git grep --break --heading -n --color \
		-e char -e lo_w hello.c hello_world |
	test_decode_color >actual &&
	test_cmp expected actual
'

cat >expected <<EOF
space: line with leading space1
space: line with leading space2
space: line with leading space3
EOF

test_expect_success PCRE 'grep -E "^ "' '
	git grep -E "^ " space >actual &&
	test_cmp expected actual
'

test_expect_success PCRE 'grep -P "^ "' '
	git grep -P "^ " space >actual &&
	test_cmp expected actual
'

cat >expected <<EOF
space-line without leading space1
space: line <RED>with <RESET>leading space1
space: line <RED>with <RESET>leading <RED>space2<RESET>
space: line <RED>with <RESET>leading space3
space:line without leading <RED>space2<RESET>
EOF

test_expect_success 'grep --color -e A -e B with context' '
	test_config color.grep.context		normal &&
	test_config color.grep.filename		normal &&
	test_config color.grep.function		normal &&
	test_config color.grep.linenumber	normal &&
	test_config color.grep.matchContext	normal &&
	test_config color.grep.matchSelected	red &&
	test_config color.grep.selected		normal &&
	test_config color.grep.separator	normal &&

	git grep --color=always -C2 -e "with " -e space2  space |
	test_decode_color >actual &&
	test_cmp expected actual
'

cat >expected <<EOF
space-line without leading space1
space- line with leading space1
space: line <RED>with <RESET>leading <RED>space2<RESET>
space- line with leading space3
space-line without leading space2
EOF

test_expect_success 'grep --color -e A --and -e B with context' '
	test_config color.grep.context		normal &&
	test_config color.grep.filename		normal &&
	test_config color.grep.function		normal &&
	test_config color.grep.linenumber	normal &&
	test_config color.grep.matchContext	normal &&
	test_config color.grep.matchSelected	red &&
	test_config color.grep.selected		normal &&
	test_config color.grep.separator	normal &&

	git grep --color=always -C2 -e "with " --and -e space2  space |
	test_decode_color >actual &&
	test_cmp expected actual
'

cat >expected <<EOF
space-line without leading space1
space: line <RED>with <RESET>leading space1
space- line with leading space2
space: line <RED>with <RESET>leading space3
space-line without leading space2
EOF

test_expect_success 'grep --color -e A --and --not -e B with context' '
	test_config color.grep.context		normal &&
	test_config color.grep.filename		normal &&
	test_config color.grep.function		normal &&
	test_config color.grep.linenumber	normal &&
	test_config color.grep.matchContext	normal &&
	test_config color.grep.matchSelected	red &&
	test_config color.grep.selected		normal &&
	test_config color.grep.separator	normal &&

	git grep --color=always -C2 -e "with " --and --not -e space2  space |
	test_decode_color >actual &&
	test_cmp expected actual
'

cat >expected <<EOF
hello.c-
hello.c=int main(int argc, const char **argv)
hello.c-{
hello.c:	pr<RED>int<RESET>f("<RED>Hello<RESET> world.\n");
hello.c-	return 0;
hello.c-	/* char ?? */
hello.c-}
EOF

test_expect_success 'grep --color -e A --and -e B -p with context' '
	test_config color.grep.context		normal &&
	test_config color.grep.filename		normal &&
	test_config color.grep.function		normal &&
	test_config color.grep.linenumber	normal &&
	test_config color.grep.matchContext	normal &&
	test_config color.grep.matchSelected	red &&
	test_config color.grep.selected		normal &&
	test_config color.grep.separator	normal &&

	git grep --color=always -p -C3 -e int --and -e Hello --no-index hello.c |
	test_decode_color >actual &&
	test_cmp expected actual
'

test_expect_success 'grep can find things only in the work tree' '
	: >work-tree-only &&
	git add work-tree-only &&
	test_when_finished "git rm -f work-tree-only" &&
	echo "find in work tree" >work-tree-only &&
	git grep --quiet "find in work tree" &&
	test_must_fail git grep --quiet --cached "find in work tree" &&
	test_must_fail git grep --quiet "find in work tree" HEAD
'

test_expect_success 'grep can find things only in the work tree (i-t-a)' '
	echo "intend to add this" >intend-to-add &&
	git add -N intend-to-add &&
	test_when_finished "git rm -f intend-to-add" &&
	git grep --quiet "intend to add this" &&
	test_must_fail git grep --quiet --cached "intend to add this" &&
	test_must_fail git grep --quiet "intend to add this" HEAD
'

test_expect_success 'grep does not search work tree with assume unchanged' '
	echo "intend to add this" >intend-to-add &&
	git add -N intend-to-add &&
	git update-index --assume-unchanged intend-to-add &&
	test_when_finished "git rm -f intend-to-add" &&
	test_must_fail git grep --quiet "intend to add this" &&
	test_must_fail git grep --quiet --cached "intend to add this" &&
	test_must_fail git grep --quiet "intend to add this" HEAD
'

test_expect_success 'grep can find things only in the index' '
	echo "only in the index" >cache-this &&
	git add cache-this &&
	rm cache-this &&
	test_when_finished "git rm --cached cache-this" &&
	test_must_fail git grep --quiet "only in the index" &&
	git grep --quiet --cached "only in the index" &&
	test_must_fail git grep --quiet "only in the index" HEAD
'

test_expect_success 'grep does not report i-t-a with -L --cached' '
	echo "intend to add this" >intend-to-add &&
	git add -N intend-to-add &&
	test_when_finished "git rm -f intend-to-add" &&
	git ls-files | grep -v "^intend-to-add\$" >expected &&
	git grep -L --cached "nonexistent_string" >actual &&
	test_cmp expected actual
'

test_expect_success 'grep does not report i-t-a and assume unchanged with -L' '
	echo "intend to add this" >intend-to-add-assume-unchanged &&
	git add -N intend-to-add-assume-unchanged &&
	test_when_finished "git rm -f intend-to-add-assume-unchanged" &&
	git update-index --assume-unchanged intend-to-add-assume-unchanged &&
	git ls-files | grep -v "^intend-to-add-assume-unchanged\$" >expected &&
	git grep -L "nonexistent_string" >actual &&
	test_cmp expected actual
'

test_done
