#!/bin/sh
#
# Copyright (c) 2007 Johannes E. Schindelin
#

test_description='Test custom diff function name patterns'

. ./test-lib.sh

test_expect_success 'setup' '
	# a non-trivial custom pattern
	git config diff.custom1.funcname "!static
!String
[^ 	].*s.*" &&

	# a custom pattern which matches to end of line
	git config diff.custom2.funcname "......Beer\$" &&

	# alternation in pattern
	git config diff.custom3.funcname "Beer$" &&
	git config diff.custom3.xfuncname "^[ 	]*((public|static).*)$" &&

	# for regexp compilation tests
	echo A >A.java &&
	echo B >B.java
'

diffpatterns="
	ada
	bibtex
	cpp
	csharp
	css
	fortran
	fountain
	html
	java
	matlab
	objc
	pascal
	perl
	php
	python
	ruby
	tex
	custom1
	custom2
	custom3
"

for p in $diffpatterns
do
	test_expect_success "builtin $p pattern compiles" '
		echo "*.java diff=$p" >.gitattributes &&
		test_expect_code 1 git diff --no-index \
			A.java B.java 2>msg &&
		test_i18ngrep ! fatal msg &&
		test_i18ngrep ! error msg
	'
	test_expect_success "builtin $p wordRegex pattern compiles" '
		echo "*.java diff=$p" >.gitattributes &&
		test_expect_code 1 git diff --no-index --word-diff \
			A.java B.java 2>msg &&
		test_i18ngrep ! fatal msg &&
		test_i18ngrep ! error msg
	'
done

test_expect_success 'last regexp must not be negated' '
	echo "*.java diff=java" >.gitattributes &&
	test_config diff.java.funcname "!static" &&
	test_expect_code 128 git diff --no-index A.java B.java 2>msg &&
	test_i18ngrep ": Last expression must not be negated:" msg
'

test_expect_success 'setup hunk header tests' '
	for i in $diffpatterns
	do
		echo "$i-* diff=$i"
	done > .gitattributes &&

	# add all test files to the index
	(
		cd "$TEST_DIRECTORY"/t4018 &&
		git --git-dir="$TRASH_DIRECTORY/.git" add .
	) &&

	# place modified files in the worktree
	for i in $(git ls-files)
	do
		sed -e "s/ChangeMe/IWasChanged/" <"$TEST_DIRECTORY/t4018/$i" >"$i" || return 1
	done
'

# check each individual file
for i in $(git ls-files)
do
	if grep broken "$i" >/dev/null 2>&1
	then
		result=failure
	else
		result=success
	fi
	test_expect_$result "hunk header: $i" "
		test_when_finished 'cat actual' &&	# for debugging only
		git diff -U1 $i >actual &&
		grep '@@ .* @@.*RIGHT' actual
	"
done

test_done
