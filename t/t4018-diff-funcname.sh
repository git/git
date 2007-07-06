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

test_expect_success 'default behaviour' '
	git diff Beer.java Beer-correct.java |
	grep "^@@.*@@ public class Beer"
'

test_expect_success 'preset java pattern' '
	echo "*.java funcname=java" >.gitattributes &&
	git diff Beer.java Beer-correct.java |
	grep "^@@.*@@ public static void main("
'

git config funcname.java '!static
!String
[^ 	].*s.*'

test_expect_success 'custom pattern' '
	git diff Beer.java Beer-correct.java |
	grep "^@@.*@@ int special;$"
'

test_expect_success 'last regexp must not be negated' '
	git config diff.functionnameregexp "!static" &&
	! git diff Beer.java Beer-correct.java
'

test_done
