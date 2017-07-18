#!/bin/sh

test_description='wildmatch tests'

. ./test-lib.sh

match() {
    if [ $1 = 1 ]; then
	test_expect_success "wildmatch:     match '$3' '$4'" "
	    test-wildmatch wildmatch '$3' '$4'
	"
    else
	test_expect_success "wildmatch:  no match '$3' '$4'" "
	    ! test-wildmatch wildmatch '$3' '$4'
	"
    fi
}

imatch() {
    if [ $1 = 1 ]; then
	test_expect_success "iwildmatch:    match '$2' '$3'" "
	    test-wildmatch iwildmatch '$2' '$3'
	"
    else
	test_expect_success "iwildmatch: no match '$2' '$3'" "
	    ! test-wildmatch iwildmatch '$2' '$3'
	"
    fi
}

pathmatch() {
    if [ $1 = 1 ]; then
	test_expect_success "pathmatch:     match '$2' '$3'" "
	    test-wildmatch pathmatch '$2' '$3'
	"
    else
	test_expect_success "pathmatch:  no match '$2' '$3'" "
	    ! test-wildmatch pathmatch '$2' '$3'
	"
    fi
}

# Basic wildmat features
match 1 1 foo foo
match 0 0 foo bar
match 1 1 '' ""
match 1 1 foo '???'
match 0 0 foo '??'
match 1 1 foo '*'
match 1 1 foo 'f*'
match 0 0 foo '*f'
match 1 1 foo '*foo*'
match 1 1 foobar '*ob*a*r*'
match 1 1 aaaaaaabababab '*ab'
match 1 1 'foo*' 'foo\*'
match 0 0 foobar 'foo\*bar'
match 1 1 'f\oo' 'f\\oo'
match 1 1 ball '*[al]?'
match 0 0 ten '[ten]'
match 0 1 ten '**[!te]'
match 0 0 ten '**[!ten]'
match 1 1 ten 't[a-g]n'
match 0 0 ten 't[!a-g]n'
match 1 1 ton 't[!a-g]n'
match 1 1 ton 't[^a-g]n'
match 1 x 'a]b' 'a[]]b'
match 1 x a-b 'a[]-]b'
match 1 x 'a]b' 'a[]-]b'
match 0 x aab 'a[]-]b'
match 1 x aab 'a[]a-]b'
match 1 1 ']' ']'

# Extended slash-matching features
match 0 0 'foo/baz/bar' 'foo*bar'
match 0 0 'foo/baz/bar' 'foo**bar'
match 0 1 'foobazbar' 'foo**bar'
match 1 1 'foo/baz/bar' 'foo/**/bar'
match 1 0 'foo/baz/bar' 'foo/**/**/bar'
match 1 0 'foo/b/a/z/bar' 'foo/**/bar'
match 1 0 'foo/b/a/z/bar' 'foo/**/**/bar'
match 1 0 'foo/bar' 'foo/**/bar'
match 1 0 'foo/bar' 'foo/**/**/bar'
match 0 0 'foo/bar' 'foo?bar'
match 0 0 'foo/bar' 'foo[/]bar'
match 0 0 'foo/bar' 'f[^eiu][^eiu][^eiu][^eiu][^eiu]r'
match 1 1 'foo-bar' 'f[^eiu][^eiu][^eiu][^eiu][^eiu]r'
match 1 0 'foo' '**/foo'
match 1 x 'XXX/foo' '**/foo'
match 1 0 'bar/baz/foo' '**/foo'
match 0 0 'bar/baz/foo' '*/foo'
match 0 0 'foo/bar/baz' '**/bar*'
match 1 0 'deep/foo/bar/baz' '**/bar/*'
match 0 0 'deep/foo/bar/baz/' '**/bar/*'
match 1 0 'deep/foo/bar/baz/' '**/bar/**'
match 0 0 'deep/foo/bar' '**/bar/*'
match 1 0 'deep/foo/bar/' '**/bar/**'
match 0 0 'foo/bar/baz' '**/bar**'
match 1 0 'foo/bar/baz/x' '*/bar/**'
match 0 0 'deep/foo/bar/baz/x' '*/bar/**'
match 1 0 'deep/foo/bar/baz/x' '**/bar/*/*'

# Various additional tests
match 0 0 'acrt' 'a[c-c]st'
match 1 1 'acrt' 'a[c-c]rt'
match 0 0 ']' '[!]-]'
match 1 x 'a' '[!]-]'
match 0 0 '' '\'
match 0 x '\' '\'
match 0 x 'XXX/\' '*/\'
match 1 x 'XXX/\' '*/\\'
match 1 1 'foo' 'foo'
match 1 1 '@foo' '@foo'
match 0 0 'foo' '@foo'
match 1 1 '[ab]' '\[ab]'
match 1 1 '[ab]' '[[]ab]'
match 1 x '[ab]' '[[:]ab]'
match 0 x '[ab]' '[[::]ab]'
match 1 x '[ab]' '[[:digit]ab]'
match 1 x '[ab]' '[\[:]ab]'
match 1 1 '?a?b' '\??\?b'
match 1 1 'abc' '\a\b\c'
match 0 0 'foo' ''
match 1 0 'foo/bar/baz/to' '**/t[o]'

# Character class tests
match 1 x 'a1B' '[[:alpha:]][[:digit:]][[:upper:]]'
match 0 x 'a' '[[:digit:][:upper:][:space:]]'
match 1 x 'A' '[[:digit:][:upper:][:space:]]'
match 1 x '1' '[[:digit:][:upper:][:space:]]'
match 0 x '1' '[[:digit:][:upper:][:spaci:]]'
match 1 x ' ' '[[:digit:][:upper:][:space:]]'
match 0 x '.' '[[:digit:][:upper:][:space:]]'
match 1 x '.' '[[:digit:][:punct:][:space:]]'
match 1 x '5' '[[:xdigit:]]'
match 1 x 'f' '[[:xdigit:]]'
match 1 x 'D' '[[:xdigit:]]'
match 1 x '_' '[[:alnum:][:alpha:][:blank:][:cntrl:][:digit:][:graph:][:lower:][:print:][:punct:][:space:][:upper:][:xdigit:]]'
match 1 x '_' '[[:alnum:][:alpha:][:blank:][:cntrl:][:digit:][:graph:][:lower:][:print:][:punct:][:space:][:upper:][:xdigit:]]'
match 1 x '.' '[^[:alnum:][:alpha:][:blank:][:cntrl:][:digit:][:lower:][:space:][:upper:][:xdigit:]]'
match 1 x '5' '[a-c[:digit:]x-z]'
match 1 x 'b' '[a-c[:digit:]x-z]'
match 1 x 'y' '[a-c[:digit:]x-z]'
match 0 x 'q' '[a-c[:digit:]x-z]'

# Additional tests, including some malformed wildmats
match 1 x ']' '[\\-^]'
match 0 0 '[' '[\\-^]'
match 1 x '-' '[\-_]'
match 1 x ']' '[\]]'
match 0 0 '\]' '[\]]'
match 0 0 '\' '[\]]'
match 0 0 'ab' 'a[]b'
match 0 x 'a[]b' 'a[]b'
match 0 x 'ab[' 'ab['
match 0 0 'ab' '[!'
match 0 0 'ab' '[-'
match 1 1 '-' '[-]'
match 0 0 '-' '[a-'
match 0 0 '-' '[!a-'
match 1 x '-' '[--A]'
match 1 x '5' '[--A]'
match 1 1 ' ' '[ --]'
match 1 1 '$' '[ --]'
match 1 1 '-' '[ --]'
match 0 0 '0' '[ --]'
match 1 x '-' '[---]'
match 1 x '-' '[------]'
match 0 0 'j' '[a-e-n]'
match 1 x '-' '[a-e-n]'
match 1 x 'a' '[!------]'
match 0 0 '[' '[]-a]'
match 1 x '^' '[]-a]'
match 0 0 '^' '[!]-a]'
match 1 x '[' '[!]-a]'
match 1 1 '^' '[a^bc]'
match 1 x '-b]' '[a-]b]'
match 0 0 '\' '[\]'
match 1 1 '\' '[\\]'
match 0 0 '\' '[!\\]'
match 1 1 'G' '[A-\\]'
match 0 0 'aaabbb' 'b*a'
match 0 0 'aabcaa' '*ba*'
match 1 1 ',' '[,]'
match 1 1 ',' '[\\,]'
match 1 1 '\' '[\\,]'
match 1 1 '-' '[,-.]'
match 0 0 '+' '[,-.]'
match 0 0 '-.]' '[,-.]'
match 1 1 '2' '[\1-\3]'
match 1 1 '3' '[\1-\3]'
match 0 0 '4' '[\1-\3]'
match 1 1 '\' '[[-\]]'
match 1 1 '[' '[[-\]]'
match 1 1 ']' '[[-\]]'
match 0 0 '-' '[[-\]]'

# Test recursion and the abort code (use "wildtest -i" to see iteration counts)
match 1 1 '-adobe-courier-bold-o-normal--12-120-75-75-m-70-iso8859-1' '-*-*-*-*-*-*-12-*-*-*-m-*-*-*'
match 0 0 '-adobe-courier-bold-o-normal--12-120-75-75-X-70-iso8859-1' '-*-*-*-*-*-*-12-*-*-*-m-*-*-*'
match 0 0 '-adobe-courier-bold-o-normal--12-120-75-75-/-70-iso8859-1' '-*-*-*-*-*-*-12-*-*-*-m-*-*-*'
match 1 1 'XXX/adobe/courier/bold/o/normal//12/120/75/75/m/70/iso8859/1' 'XXX/*/*/*/*/*/*/12/*/*/*/m/*/*/*'
match 0 0 'XXX/adobe/courier/bold/o/normal//12/120/75/75/X/70/iso8859/1' 'XXX/*/*/*/*/*/*/12/*/*/*/m/*/*/*'
match 1 0 'abcd/abcdefg/abcdefghijk/abcdefghijklmnop.txt' '**/*a*b*g*n*t'
match 0 0 'abcd/abcdefg/abcdefghijk/abcdefghijklmnop.txtz' '**/*a*b*g*n*t'
match 0 x foo '*/*/*'
match 0 x foo/bar '*/*/*'
match 1 x foo/bba/arr '*/*/*'
match 0 x foo/bb/aa/rr '*/*/*'
match 1 x foo/bb/aa/rr '**/**/**'
match 1 x abcXdefXghi '*X*i'
match 0 x ab/cXd/efXg/hi '*X*i'
match 1 x ab/cXd/efXg/hi '*/*X*/*/*i'
match 1 x ab/cXd/efXg/hi '**/*X*/**/*i'

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
pathmatch 0 foo '*/*/*'
pathmatch 0 foo/bar '*/*/*'
pathmatch 1 foo/bba/arr '*/*/*'
pathmatch 1 foo/bb/aa/rr '*/*/*'
pathmatch 1 abcXdefXghi '*X*i'
pathmatch 1 ab/cXd/efXg/hi '*/*X*/*/*i'
pathmatch 1 ab/cXd/efXg/hi '*Xg*i'

# Case-sensitivy features
match 0 x 'a' '[A-Z]'
match 1 x 'A' '[A-Z]'
match 0 x 'A' '[a-z]'
match 1 x 'a' '[a-z]'
match 0 x 'a' '[[:upper:]]'
match 1 x 'A' '[[:upper:]]'
match 0 x 'A' '[[:lower:]]'
match 1 x 'a' '[[:lower:]]'
match 0 x 'A' '[B-Za]'
match 1 x 'a' '[B-Za]'
match 0 x 'A' '[B-a]'
match 1 x 'a' '[B-a]'
match 0 x 'z' '[Z-y]'
match 1 x 'Z' '[Z-y]'

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
