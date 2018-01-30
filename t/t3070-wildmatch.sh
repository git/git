#!/bin/sh

test_description='wildmatch tests'

. ./test-lib.sh

match() {
	if test "$1" = 1
	then
		test_expect_success "wildmatch: match '$2' '$3'" "
			test-wildmatch wildmatch '$2' '$3'
		"
	elif test "$1" = 0
	then
		test_expect_success "wildmatch: no match '$2' '$3'" "
			! test-wildmatch wildmatch '$2' '$3'
		"
	else
		test_expect_success "PANIC: Test framework error. Unknown matches value $1" 'false'
	fi
}

imatch() {
	if test "$1" = 1
	then
		test_expect_success "iwildmatch: match '$2' '$3'" "
			test-wildmatch iwildmatch '$2' '$3'
		"
	elif test "$1" = 0
	then
		test_expect_success "iwildmatch: no match '$2' '$3'" "
			! test-wildmatch iwildmatch '$2' '$3'
		"
	else
		test_expect_success "PANIC: Test framework error. Unknown matches value $1" 'false'
	fi
}

pathmatch() {
	if test "$1" = 1
	then
		test_expect_success "pathmatch: match '$2' '$3'" "
			test-wildmatch pathmatch '$2' '$3'
		"
	elif test "$1" = 0
	then
		test_expect_success "pathmatch: no match '$2' '$3'" "
			! test-wildmatch pathmatch '$2' '$3'
		"
	else
		test_expect_success "PANIC: Test framework error. Unknown matches value $1" 'false'
	fi
}

# Basic wildmat features
match 1 foo foo
match 0 foo bar
match 1 '' ""
match 1 foo '???'
match 0 foo '??'
match 1 foo '*'
match 1 foo 'f*'
match 0 foo '*f'
match 1 foo '*foo*'
match 1 foobar '*ob*a*r*'
match 1 aaaaaaabababab '*ab'
match 1 'foo*' 'foo\*'
match 0 foobar 'foo\*bar'
match 1 'f\oo' 'f\\oo'
match 1 ball '*[al]?'
match 0 ten '[ten]'
match 0 ten '**[!te]'
match 0 ten '**[!ten]'
match 1 ten 't[a-g]n'
match 0 ten 't[!a-g]n'
match 1 ton 't[!a-g]n'
match 1 ton 't[^a-g]n'
match 1 'a]b' 'a[]]b'
match 1 a-b 'a[]-]b'
match 1 'a]b' 'a[]-]b'
match 0 aab 'a[]-]b'
match 1 aab 'a[]a-]b'
match 1 ']' ']'

# Extended slash-matching features
match 0 'foo/baz/bar' 'foo*bar'
match 0 'foo/baz/bar' 'foo**bar'
match 0 'foobazbar' 'foo**bar'
match 1 'foo/baz/bar' 'foo/**/bar'
match 1 'foo/baz/bar' 'foo/**/**/bar'
match 1 'foo/b/a/z/bar' 'foo/**/bar'
match 1 'foo/b/a/z/bar' 'foo/**/**/bar'
match 1 'foo/bar' 'foo/**/bar'
match 1 'foo/bar' 'foo/**/**/bar'
match 0 'foo/bar' 'foo?bar'
match 0 'foo/bar' 'foo[/]bar'
match 0 'foo/bar' 'foo[^a-z]bar'
match 0 'foo/bar' 'f[^eiu][^eiu][^eiu][^eiu][^eiu]r'
match 1 'foo-bar' 'f[^eiu][^eiu][^eiu][^eiu][^eiu]r'
match 1 'foo' '**/foo'
match 1 'XXX/foo' '**/foo'
match 1 'bar/baz/foo' '**/foo'
match 0 'bar/baz/foo' '*/foo'
match 0 'foo/bar/baz' '**/bar*'
match 1 'deep/foo/bar/baz' '**/bar/*'
match 0 'deep/foo/bar/baz/' '**/bar/*'
match 1 'deep/foo/bar/baz/' '**/bar/**'
match 0 'deep/foo/bar' '**/bar/*'
match 1 'deep/foo/bar/' '**/bar/**'
match 0 'foo/bar/baz' '**/bar**'
match 1 'foo/bar/baz/x' '*/bar/**'
match 0 'deep/foo/bar/baz/x' '*/bar/**'
match 1 'deep/foo/bar/baz/x' '**/bar/*/*'

# Various additional tests
match 0 'acrt' 'a[c-c]st'
match 1 'acrt' 'a[c-c]rt'
match 0 ']' '[!]-]'
match 1 'a' '[!]-]'
match 0 '' '\'
match 0 '\' '\'
match 0 'XXX/\' '*/\'
match 1 'XXX/\' '*/\\'
match 1 'foo' 'foo'
match 1 '@foo' '@foo'
match 0 'foo' '@foo'
match 1 '[ab]' '\[ab]'
match 1 '[ab]' '[[]ab]'
match 1 '[ab]' '[[:]ab]'
match 0 '[ab]' '[[::]ab]'
match 1 '[ab]' '[[:digit]ab]'
match 1 '[ab]' '[\[:]ab]'
match 1 '?a?b' '\??\?b'
match 1 'abc' '\a\b\c'
match 0 'foo' ''
match 1 'foo/bar/baz/to' '**/t[o]'

# Character class tests
match 1 'a1B' '[[:alpha:]][[:digit:]][[:upper:]]'
match 0 'a' '[[:digit:][:upper:][:space:]]'
match 1 'A' '[[:digit:][:upper:][:space:]]'
match 1 '1' '[[:digit:][:upper:][:space:]]'
match 0 '1' '[[:digit:][:upper:][:spaci:]]'
match 1 ' ' '[[:digit:][:upper:][:space:]]'
match 0 '.' '[[:digit:][:upper:][:space:]]'
match 1 '.' '[[:digit:][:punct:][:space:]]'
match 1 '5' '[[:xdigit:]]'
match 1 'f' '[[:xdigit:]]'
match 1 'D' '[[:xdigit:]]'
match 1 '_' '[[:alnum:][:alpha:][:blank:][:cntrl:][:digit:][:graph:][:lower:][:print:][:punct:][:space:][:upper:][:xdigit:]]'
match 1 '.' '[^[:alnum:][:alpha:][:blank:][:cntrl:][:digit:][:lower:][:space:][:upper:][:xdigit:]]'
match 1 '5' '[a-c[:digit:]x-z]'
match 1 'b' '[a-c[:digit:]x-z]'
match 1 'y' '[a-c[:digit:]x-z]'
match 0 'q' '[a-c[:digit:]x-z]'

# Additional tests, including some malformed wildmats
match 1 ']' '[\\-^]'
match 0 '[' '[\\-^]'
match 1 '-' '[\-_]'
match 1 ']' '[\]]'
match 0 '\]' '[\]]'
match 0 '\' '[\]]'
match 0 'ab' 'a[]b'
match 0 'a[]b' 'a[]b'
match 0 'ab[' 'ab['
match 0 'ab' '[!'
match 0 'ab' '[-'
match 1 '-' '[-]'
match 0 '-' '[a-'
match 0 '-' '[!a-'
match 1 '-' '[--A]'
match 1 '5' '[--A]'
match 1 ' ' '[ --]'
match 1 '$' '[ --]'
match 1 '-' '[ --]'
match 0 '0' '[ --]'
match 1 '-' '[---]'
match 1 '-' '[------]'
match 0 'j' '[a-e-n]'
match 1 '-' '[a-e-n]'
match 1 'a' '[!------]'
match 0 '[' '[]-a]'
match 1 '^' '[]-a]'
match 0 '^' '[!]-a]'
match 1 '[' '[!]-a]'
match 1 '^' '[a^bc]'
match 1 '-b]' '[a-]b]'
match 0 '\' '[\]'
match 1 '\' '[\\]'
match 0 '\' '[!\\]'
match 1 'G' '[A-\\]'
match 0 'aaabbb' 'b*a'
match 0 'aabcaa' '*ba*'
match 1 ',' '[,]'
match 1 ',' '[\\,]'
match 1 '\' '[\\,]'
match 1 '-' '[,-.]'
match 0 '+' '[,-.]'
match 0 '-.]' '[,-.]'
match 1 '2' '[\1-\3]'
match 1 '3' '[\1-\3]'
match 0 '4' '[\1-\3]'
match 1 '\' '[[-\]]'
match 1 '[' '[[-\]]'
match 1 ']' '[[-\]]'
match 0 '-' '[[-\]]'

# Test recursion and the abort code (use "wildtest -i" to see iteration counts)
match 1 '-adobe-courier-bold-o-normal--12-120-75-75-m-70-iso8859-1' '-*-*-*-*-*-*-12-*-*-*-m-*-*-*'
match 0 '-adobe-courier-bold-o-normal--12-120-75-75-X-70-iso8859-1' '-*-*-*-*-*-*-12-*-*-*-m-*-*-*'
match 0 '-adobe-courier-bold-o-normal--12-120-75-75-/-70-iso8859-1' '-*-*-*-*-*-*-12-*-*-*-m-*-*-*'
match 1 'XXX/adobe/courier/bold/o/normal//12/120/75/75/m/70/iso8859/1' 'XXX/*/*/*/*/*/*/12/*/*/*/m/*/*/*'
match 0 'XXX/adobe/courier/bold/o/normal//12/120/75/75/X/70/iso8859/1' 'XXX/*/*/*/*/*/*/12/*/*/*/m/*/*/*'
match 1 'abcd/abcdefg/abcdefghijk/abcdefghijklmnop.txt' '**/*a*b*g*n*t'
match 0 'abcd/abcdefg/abcdefghijk/abcdefghijklmnop.txtz' '**/*a*b*g*n*t'
match 0 foo '*/*/*'
match 0 foo/bar '*/*/*'
match 1 foo/bba/arr '*/*/*'
match 0 foo/bb/aa/rr '*/*/*'
match 1 foo/bb/aa/rr '**/**/**'
match 1 abcXdefXghi '*X*i'
match 0 ab/cXd/efXg/hi '*X*i'
match 1 ab/cXd/efXg/hi '*/*X*/*/*i'
match 1 ab/cXd/efXg/hi '**/*X*/**/*i'

pathmatch 1 foo foo
pathmatch 0 foo fo
pathmatch 1 foo/bar foo/bar
pathmatch 1 foo/bar 'foo/*'
pathmatch 1 foo/bba/arr 'foo/*'
pathmatch 1 foo/bba/arr 'foo/**'
pathmatch 1 foo/bba/arr 'foo*'
pathmatch 1 foo/bba/arr 'foo**'
pathmatch 1 foo/bba/arr 'foo/*arr'
pathmatch 1 foo/bba/arr 'foo/**arr'
pathmatch 0 foo/bba/arr 'foo/*z'
pathmatch 0 foo/bba/arr 'foo/**z'
pathmatch 1 foo/bar 'foo?bar'
pathmatch 1 foo/bar 'foo[/]bar'
pathmatch 1 foo/bar 'foo[^a-z]bar'
pathmatch 0 foo '*/*/*'
pathmatch 0 foo/bar '*/*/*'
pathmatch 1 foo/bba/arr '*/*/*'
pathmatch 1 foo/bb/aa/rr '*/*/*'
pathmatch 1 abcXdefXghi '*X*i'
pathmatch 1 ab/cXd/efXg/hi '*/*X*/*/*i'
pathmatch 1 ab/cXd/efXg/hi '*Xg*i'

# Case-sensitivity features
match 0 'a' '[A-Z]'
match 1 'A' '[A-Z]'
match 0 'A' '[a-z]'
match 1 'a' '[a-z]'
match 0 'a' '[[:upper:]]'
match 1 'A' '[[:upper:]]'
match 0 'A' '[[:lower:]]'
match 1 'a' '[[:lower:]]'
match 0 'A' '[B-Za]'
match 1 'a' '[B-Za]'
match 0 'A' '[B-a]'
match 1 'a' '[B-a]'
match 0 'z' '[Z-y]'
match 1 'Z' '[Z-y]'

imatch 1 'a' '[A-Z]'
imatch 1 'A' '[A-Z]'
imatch 1 'A' '[a-z]'
imatch 1 'a' '[a-z]'
imatch 1 'a' '[[:upper:]]'
imatch 1 'A' '[[:upper:]]'
imatch 1 'A' '[[:lower:]]'
imatch 1 'a' '[[:lower:]]'
imatch 1 'A' '[B-Za]'
imatch 1 'a' '[B-Za]'
imatch 1 'A' '[B-a]'
imatch 1 'a' '[B-a]'
imatch 1 'z' '[Z-y]'
imatch 1 'Z' '[Z-y]'

test_done
