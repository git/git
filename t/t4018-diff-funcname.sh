#!/bin/sh
#
# Copyright (c) 2007 Johannes E. Schindelin
#

test_description='Test custom diff function name patterns'

. ./test-lib.sh

LF='
'

cat > Beer.java << EOF
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

sed 's/beer\\/beer,\\/' < Beer.java > Beer-correct.java

builtin_patterns="bibtex cpp html java objc pascal php python ruby tex"
for p in $builtin_patterns
do
	test_expect_success "builtin $p pattern compiles" '
		echo "*.java diff=$p" > .gitattributes &&
		! ( git diff --no-index Beer.java Beer-correct.java 2>&1 |
			grep "fatal" > /dev/null )
	'
done

test_expect_success 'default behaviour' '
	rm -f .gitattributes &&
	git diff --no-index Beer.java Beer-correct.java |
	grep "^@@.*@@ public class Beer"
'

test_expect_success 'preset java pattern' '
	echo "*.java diff=java" >.gitattributes &&
	git diff --no-index Beer.java Beer-correct.java |
	grep "^@@.*@@ public static void main("
'

git config diff.java.funcname '!static
!String
[^ 	].*s.*'

test_expect_success 'custom pattern' '
	git diff --no-index Beer.java Beer-correct.java |
	grep "^@@.*@@ int special;$"
'

test_expect_success 'last regexp must not be negated' '
	git config diff.java.funcname "!static" &&
	git diff --no-index Beer.java Beer-correct.java 2>&1 |
	grep "fatal: Last expression must not be negated:"
'

test_expect_success 'pattern which matches to end of line' '
	git config diff.java.funcname "Beer$" &&
	git diff --no-index Beer.java Beer-correct.java |
	grep "^@@.*@@ Beer"
'

test_expect_success 'alternation in pattern' '
	git config diff.java.xfuncname "^[ 	]*((public|static).*)$" &&
	git diff --no-index Beer.java Beer-correct.java |
	grep "^@@.*@@ public static void main("
'

test_done
