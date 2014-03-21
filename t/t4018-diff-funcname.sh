#!/bin/sh
#
# Copyright (c) 2007 Johannes E. Schindelin
#

test_description='Test custom diff function name patterns'

. ./test-lib.sh

LF='
'
cat >Beer.java <<\EOF
public class Beer
{
	int special;
	public static void main(String args[])
	{
		String s=" ";
		for(int x = 99; x > 0; x--)
		{
			System.out.print(x + " bottles of beer on the wall "
				+ x + " bottles of beer\n"
				+ "Take one down, pass it around, " + (x - 1)
				+ " bottles of beer on the wall.\n");
		}
		System.out.print("Go to the store, buy some more,\n"
			+ "99 bottles of beer on the wall.\n");
	}
}
EOF
sed 's/beer\\/beer,\\/' <Beer.java >Beer-correct.java

test_expect_success 'setup' '
	# a non-trivial custom pattern
	git config diff.custom1.funcname "!static
!String
[^ 	].*s.*" &&

	# a custom pattern which matches to end of line
	git config diff.custom2.funcname "......Beer\$" &&

	# alternation in pattern
	git config diff.custom3.funcname "Beer$" &&
	git config diff.custom3.xfuncname "^[ 	]*((public|static).*)$"
'

diffpatterns="
	ada
	bibtex
	cpp
	csharp
	fortran
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
			Beer.java Beer-correct.java 2>msg &&
		! grep fatal msg &&
		! grep error msg
	'
	test_expect_success "builtin $p wordRegex pattern compiles" '
		echo "*.java diff=$p" >.gitattributes &&
		test_expect_code 1 git diff --no-index --word-diff \
			Beer.java Beer-correct.java 2>msg &&
		! grep fatal msg &&
		! grep error msg
	'
done

test_expect_success 'set up .gitattributes declaring drivers to test' '
	cat >.gitattributes <<-\EOF
	*.java diff=java
	EOF
'

test_expect_success 'last regexp must not be negated' '
	test_config diff.java.funcname "!static" &&
	test_expect_code 128 git diff --no-index Beer.java Beer-correct.java 2>msg &&
	grep ": Last expression must not be negated:" msg
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
