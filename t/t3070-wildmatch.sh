#!/bin/sh

test_description='wildmatch tests'

. ./test-lib.sh

# Disable expensive chain-lint tests; all of the tests in this script
# are variants of a few trivial test-tool invocations, and there are a lot of
# them.
GIT_TEST_CHAIN_LINT_HARDER_DEFAULT=0

should_create_test_file() {
	file=$1

	case $file in
	# `touch .` will succeed but obviously not do what we intend
	# here.
	".")
		return 1
		;;
	# We cannot create a file with an empty filename.
	"")
		return 1
		;;
	# The tests that are testing that e.g. foo//bar is matched by
	# foo/*/bar can't be tested on filesystems since there's no
	# way we're getting a double slash.
	*//*)
		return 1
		;;
	# When testing the difference between foo/bar and foo/bar/ we
	# can't test the latter.
	*/)
		return 1
		;;
	# On Windows, \ in paths is silently converted to /, which
	# would result in the "touch" below working, but the test
	# itself failing. See 6fd1106aa4 ("t3700: Skip a test with
	# backslashes in pathspec", 2009-03-13) for prior art and
	# details.
	*\\*)
		if ! test_have_prereq BSLASHPSPEC
		then
			return 1
		fi
		# NOTE: The ;;& bash extension is not portable, so
		# this test needs to be at the end of the pattern
		# list.
		#
		# If we want to add more conditional returns we either
		# need a new case statement, or turn this whole thing
		# into a series of "if" tests.
		;;
	esac


	# On Windows proper (i.e. not Cygwin) many file names which
	# under Cygwin would be emulated don't work.
	if test_have_prereq MINGW
	then
		case $file in
		" ")
			# Files called " " are forbidden on Windows
			return 1
			;;
		*\<*|*\>*|*:*|*\"*|*\|*|*\?*|*\**)
			# Files with various special characters aren't
			# allowed on Windows. Sourced from
			# https://stackoverflow.com/a/31976060
			return 1
			;;
		esac
	fi

	return 0
}

match_with_function() {
	text=$1
	pattern=$2
	match_expect=$3
	match_function=$4

	if test "$match_expect" = 1
	then
		test_expect_success "$match_function: match '$text' '$pattern'" "
			test-tool wildmatch $match_function '$text' '$pattern'
		"
	elif test "$match_expect" = 0
	then
		test_expect_success "$match_function: no match '$text' '$pattern'" "
			test_must_fail test-tool wildmatch $match_function '$text' '$pattern'
		"
	else
		test_expect_success "PANIC: Test framework error. Unknown matches value $match_expect" 'false'
	fi

}

match_with_ls_files() {
	text=$1
	pattern=$2
	match_expect=$3
	match_function=$4
	ls_files_args=$5

	match_stdout_stderr_cmp="
		tr -d '\0' <actual.raw >actual &&
		test_must_be_empty actual.err &&
		test_cmp expect actual"

	if test "$match_expect" = 'E'
	then
		if test -e .git/created_test_file
		then
			test_expect_success EXPENSIVE_ON_WINDOWS "$match_function (via ls-files): match dies on '$pattern' '$text'" "
				printf '%s' '$text' >expect &&
				test_must_fail git$ls_files_args ls-files -z -- '$pattern'
			"
		else
			test_expect_failure EXPENSIVE_ON_WINDOWS "$match_function (via ls-files): match skip '$pattern' '$text'" 'false'
		fi
	elif test "$match_expect" = 1
	then
		if test -e .git/created_test_file
		then
			test_expect_success EXPENSIVE_ON_WINDOWS "$match_function (via ls-files): match '$pattern' '$text'" "
				printf '%s' '$text' >expect &&
				git$ls_files_args ls-files -z -- '$pattern' >actual.raw 2>actual.err &&
				$match_stdout_stderr_cmp
			"
		else
			test_expect_failure EXPENSIVE_ON_WINDOWS "$match_function (via ls-files): match skip '$pattern' '$text'" 'false'
		fi
	elif test "$match_expect" = 0
	then
		if test -e .git/created_test_file
		then
			test_expect_success EXPENSIVE_ON_WINDOWS "$match_function (via ls-files): no match '$pattern' '$text'" "
				>expect &&
				git$ls_files_args ls-files -z -- '$pattern' >actual.raw 2>actual.err &&
				$match_stdout_stderr_cmp
			"
		else
			test_expect_failure EXPENSIVE_ON_WINDOWS "$match_function (via ls-files): no match skip '$pattern' '$text'" 'false'
		fi
	else
		test_expect_success "PANIC: Test framework error. Unknown matches value $match_expect" 'false'
	fi
}

match() {
	if test "$#" = 6
	then
		# When test-tool wildmatch and git ls-files produce the same
		# result.
		match_glob=$1
		match_file_glob=$match_glob
		match_iglob=$2
		match_file_iglob=$match_iglob
		match_pathmatch=$3
		match_file_pathmatch=$match_pathmatch
		match_pathmatchi=$4
		match_file_pathmatchi=$match_pathmatchi
		text=$5
		pattern=$6
	elif test "$#" = 10
	then
		match_glob=$1
		match_iglob=$2
		match_pathmatch=$3
		match_pathmatchi=$4
		match_file_glob=$5
		match_file_iglob=$6
		match_file_pathmatch=$7
		match_file_pathmatchi=$8
		text=$9
		pattern=${10}
	fi

	test_expect_success EXPENSIVE_ON_WINDOWS 'cleanup after previous file test' '
		if test -e .git/created_test_file
		then
			git reset &&
			git clean -df
		fi
	'

	printf '%s' "$text" >.git/expected_test_file

	test_expect_success EXPENSIVE_ON_WINDOWS "setup match file test for $text" '
		file=$(cat .git/expected_test_file) &&
		if should_create_test_file "$file"
		then
			dirs=${file%/*}
			if test "$file" != "$dirs"
			then
				mkdir -p -- "$dirs" &&
				touch -- "./$text"
			else
				touch -- "./$file"
			fi &&
			git add -A &&
			printf "%s" "$file" >.git/created_test_file
		elif test -e .git/created_test_file
		then
			rm .git/created_test_file
		fi
	'

	# $1: Case sensitive glob match: test-tool wildmatch & ls-files
	match_with_function "$text" "$pattern" $match_glob "wildmatch"
	match_with_ls_files "$text" "$pattern" $match_file_glob "wildmatch" " --glob-pathspecs"

	# $2: Case insensitive glob match: test-tool wildmatch & ls-files
	match_with_function "$text" "$pattern" $match_iglob "iwildmatch"
	match_with_ls_files "$text" "$pattern" $match_file_iglob "iwildmatch" " --glob-pathspecs --icase-pathspecs"

	# $3: Case sensitive path match: test-tool wildmatch & ls-files
	match_with_function "$text" "$pattern" $match_pathmatch "pathmatch"
	match_with_ls_files "$text" "$pattern" $match_file_pathmatch "pathmatch" ""

	# $4: Case insensitive path match: test-tool wildmatch & ls-files
	match_with_function "$text" "$pattern" $match_pathmatchi "ipathmatch"
	match_with_ls_files "$text" "$pattern" $match_file_pathmatchi "ipathmatch" " --icase-pathspecs"
}

# Basic wildmatch features
match 1 1 1 1 foo foo
match 0 0 0 0 foo bar
match 1 1 1 1 '' ""
match 1 1 1 1 foo '???'
match 0 0 0 0 foo '??'
match 1 1 1 1 foo '*'
match 1 1 1 1 foo 'f*'
match 0 0 0 0 foo '*f'
match 1 1 1 1 foo '*foo*'
match 1 1 1 1 foobar '*ob*a*r*'
match 1 1 1 1 aaaaaaabababab '*ab'
match 1 1 1 1 'foo*' 'foo\*'
match 0 0 0 0 foobar 'foo\*bar'
match 1 1 1 1 'f\oo' 'f\\oo'
match 1 1 1 1 ball '*[al]?'
match 0 0 0 0 ten '[ten]'
match 1 1 1 1 ten '**[!te]'
match 0 0 0 0 ten '**[!ten]'
match 1 1 1 1 ten 't[a-g]n'
match 0 0 0 0 ten 't[!a-g]n'
match 1 1 1 1 ton 't[!a-g]n'
match 1 1 1 1 ton 't[^a-g]n'
match 1 1 1 1 'a]b' 'a[]]b'
match 1 1 1 1 a-b 'a[]-]b'
match 1 1 1 1 'a]b' 'a[]-]b'
match 0 0 0 0 aab 'a[]-]b'
match 1 1 1 1 aab 'a[]a-]b'
match 1 1 1 1 ']' ']'

# Extended slash-matching features
match 0 0 1 1 'foo/baz/bar' 'foo*bar'
match 0 0 1 1 'foo/baz/bar' 'foo**bar'
match 1 1 1 1 'foobazbar' 'foo**bar'
match 1 1 1 1 'foo/baz/bar' 'foo/**/bar'
match 1 1 0 0 'foo/baz/bar' 'foo/**/**/bar'
match 1 1 1 1 'foo/b/a/z/bar' 'foo/**/bar'
match 1 1 1 1 'foo/b/a/z/bar' 'foo/**/**/bar'
match 1 1 0 0 'foo/bar' 'foo/**/bar'
match 1 1 0 0 'foo/bar' 'foo/**/**/bar'
match 0 0 1 1 'foo/bar' 'foo?bar'
match 0 0 1 1 'foo/bar' 'foo[/]bar'
match 0 0 1 1 'foo/bar' 'foo[^a-z]bar'
match 0 0 1 1 'foo/bar' 'f[^eiu][^eiu][^eiu][^eiu][^eiu]r'
match 1 1 1 1 'foo-bar' 'f[^eiu][^eiu][^eiu][^eiu][^eiu]r'
match 1 1 0 0 'foo' '**/foo'
match 1 1 1 1 'XXX/foo' '**/foo'
match 1 1 1 1 'bar/baz/foo' '**/foo'
match 0 0 1 1 'bar/baz/foo' '*/foo'
match 0 0 1 1 'foo/bar/baz' '**/bar*'
match 1 1 1 1 'deep/foo/bar/baz' '**/bar/*'
match 0 0 1 1 'deep/foo/bar/baz/' '**/bar/*'
match 1 1 1 1 'deep/foo/bar/baz/' '**/bar/**'
match 0 0 0 0 'deep/foo/bar' '**/bar/*'
match 1 1 1 1 'deep/foo/bar/' '**/bar/**'
match 0 0 1 1 'foo/bar/baz' '**/bar**'
match 1 1 1 1 'foo/bar/baz/x' '*/bar/**'
match 0 0 1 1 'deep/foo/bar/baz/x' '*/bar/**'
match 1 1 1 1 'deep/foo/bar/baz/x' '**/bar/*/*'

# Various additional tests
match 0 0 0 0 'acrt' 'a[c-c]st'
match 1 1 1 1 'acrt' 'a[c-c]rt'
match 0 0 0 0 ']' '[!]-]'
match 1 1 1 1 'a' '[!]-]'
match 0 0 0 0 '' '\'
match 0 0 0 0 \
      1 1 1 1 '\' '\'
match 0 0 0 0 'XXX/\' '*/\'
match 1 1 1 1 'XXX/\' '*/\\'
match 1 1 1 1 'foo' 'foo'
match 1 1 1 1 '@foo' '@foo'
match 0 0 0 0 'foo' '@foo'
match 1 1 1 1 '[ab]' '\[ab]'
match 1 1 1 1 '[ab]' '[[]ab]'
match 1 1 1 1 '[ab]' '[[:]ab]'
match 0 0 0 0 '[ab]' '[[::]ab]'
match 1 1 1 1 '[ab]' '[[:digit]ab]'
match 1 1 1 1 '[ab]' '[\[:]ab]'
match 1 1 1 1 '?a?b' '\??\?b'
match 1 1 1 1 'abc' '\a\b\c'
match 0 0 0 0 \
      E E E E 'foo' ''
match 1 1 1 1 'foo/bar/baz/to' '**/t[o]'

# Character class tests
match 1 1 1 1 'a1B' '[[:alpha:]][[:digit:]][[:upper:]]'
match 0 1 0 1 'a' '[[:digit:][:upper:][:space:]]'
match 1 1 1 1 'A' '[[:digit:][:upper:][:space:]]'
match 1 1 1 1 '1' '[[:digit:][:upper:][:space:]]'
match 0 0 0 0 '1' '[[:digit:][:upper:][:spaci:]]'
match 1 1 1 1 ' ' '[[:digit:][:upper:][:space:]]'
match 0 0 0 0 '.' '[[:digit:][:upper:][:space:]]'
match 1 1 1 1 '.' '[[:digit:][:punct:][:space:]]'
match 1 1 1 1 '5' '[[:xdigit:]]'
match 1 1 1 1 'f' '[[:xdigit:]]'
match 1 1 1 1 'D' '[[:xdigit:]]'
match 1 1 1 1 '_' '[[:alnum:][:alpha:][:blank:][:cntrl:][:digit:][:graph:][:lower:][:print:][:punct:][:space:][:upper:][:xdigit:]]'
match 1 1 1 1 '.' '[^[:alnum:][:alpha:][:blank:][:cntrl:][:digit:][:lower:][:space:][:upper:][:xdigit:]]'
match 1 1 1 1 '5' '[a-c[:digit:]x-z]'
match 1 1 1 1 'b' '[a-c[:digit:]x-z]'
match 1 1 1 1 'y' '[a-c[:digit:]x-z]'
match 0 0 0 0 'q' '[a-c[:digit:]x-z]'

# Additional tests, including some malformed wildmatch patterns
match 1 1 1 1 ']' '[\\-^]'
match 0 0 0 0 '[' '[\\-^]'
match 1 1 1 1 '-' '[\-_]'
match 1 1 1 1 ']' '[\]]'
match 0 0 0 0 '\]' '[\]]'
match 0 0 0 0 '\' '[\]]'
match 0 0 0 0 'ab' 'a[]b'
match 0 0 0 0 \
      1 1 1 1 'a[]b' 'a[]b'
match 0 0 0 0 \
      1 1 1 1 'ab[' 'ab['
match 0 0 0 0 'ab' '[!'
match 0 0 0 0 'ab' '[-'
match 1 1 1 1 '-' '[-]'
match 0 0 0 0 '-' '[a-'
match 0 0 0 0 '-' '[!a-'
match 1 1 1 1 '-' '[--A]'
match 1 1 1 1 '5' '[--A]'
match 1 1 1 1 ' ' '[ --]'
match 1 1 1 1 '$' '[ --]'
match 1 1 1 1 '-' '[ --]'
match 0 0 0 0 '0' '[ --]'
match 1 1 1 1 '-' '[---]'
match 1 1 1 1 '-' '[------]'
match 0 0 0 0 'j' '[a-e-n]'
match 1 1 1 1 '-' '[a-e-n]'
match 1 1 1 1 'a' '[!------]'
match 0 0 0 0 '[' '[]-a]'
match 1 1 1 1 '^' '[]-a]'
match 0 0 0 0 '^' '[!]-a]'
match 1 1 1 1 '[' '[!]-a]'
match 1 1 1 1 '^' '[a^bc]'
match 1 1 1 1 '-b]' '[a-]b]'
match 0 0 0 0 '\' '[\]'
match 1 1 1 1 '\' '[\\]'
match 0 0 0 0 '\' '[!\\]'
match 1 1 1 1 'G' '[A-\\]'
match 0 0 0 0 'aaabbb' 'b*a'
match 0 0 0 0 'aabcaa' '*ba*'
match 1 1 1 1 ',' '[,]'
match 1 1 1 1 ',' '[\\,]'
match 1 1 1 1 '\' '[\\,]'
match 1 1 1 1 '-' '[,-.]'
match 0 0 0 0 '+' '[,-.]'
match 0 0 0 0 '-.]' '[,-.]'
match 1 1 1 1 '2' '[\1-\3]'
match 1 1 1 1 '3' '[\1-\3]'
match 0 0 0 0 '4' '[\1-\3]'
match 1 1 1 1 '\' '[[-\]]'
match 1 1 1 1 '[' '[[-\]]'
match 1 1 1 1 ']' '[[-\]]'
match 0 0 0 0 '-' '[[-\]]'

# Test recursion
match 1 1 1 1 '-adobe-courier-bold-o-normal--12-120-75-75-m-70-iso8859-1' '-*-*-*-*-*-*-12-*-*-*-m-*-*-*'
match 0 0 0 0 '-adobe-courier-bold-o-normal--12-120-75-75-X-70-iso8859-1' '-*-*-*-*-*-*-12-*-*-*-m-*-*-*'
match 0 0 0 0 '-adobe-courier-bold-o-normal--12-120-75-75-/-70-iso8859-1' '-*-*-*-*-*-*-12-*-*-*-m-*-*-*'
match 1 1 1 1 'XXX/adobe/courier/bold/o/normal//12/120/75/75/m/70/iso8859/1' 'XXX/*/*/*/*/*/*/12/*/*/*/m/*/*/*'
match 0 0 0 0 'XXX/adobe/courier/bold/o/normal//12/120/75/75/X/70/iso8859/1' 'XXX/*/*/*/*/*/*/12/*/*/*/m/*/*/*'
match 1 1 1 1 'abcd/abcdefg/abcdefghijk/abcdefghijklmnop.txt' '**/*a*b*g*n*t'
match 0 0 0 0 'abcd/abcdefg/abcdefghijk/abcdefghijklmnop.txtz' '**/*a*b*g*n*t'
match 0 0 0 0 foo '*/*/*'
match 0 0 0 0 foo/bar '*/*/*'
match 1 1 1 1 foo/bba/arr '*/*/*'
match 0 0 1 1 foo/bb/aa/rr '*/*/*'
match 1 1 1 1 foo/bb/aa/rr '**/**/**'
match 1 1 1 1 abcXdefXghi '*X*i'
match 0 0 1 1 ab/cXd/efXg/hi '*X*i'
match 1 1 1 1 ab/cXd/efXg/hi '*/*X*/*/*i'
match 1 1 1 1 ab/cXd/efXg/hi '**/*X*/**/*i'

# Extra pathmatch tests
match 0 0 0 0 foo fo
match 1 1 1 1 foo/bar foo/bar
match 1 1 1 1 foo/bar 'foo/*'
match 0 0 1 1 foo/bba/arr 'foo/*'
match 1 1 1 1 foo/bba/arr 'foo/**'
match 0 0 1 1 foo/bba/arr 'foo*'
match 0 0 1 1 \
      1 1 1 1 foo/bba/arr 'foo**'
match 0 0 1 1 foo/bba/arr 'foo/*arr'
match 0 0 1 1 foo/bba/arr 'foo/**arr'
match 0 0 0 0 foo/bba/arr 'foo/*z'
match 0 0 0 0 foo/bba/arr 'foo/**z'
match 0 0 1 1 foo/bar 'foo?bar'
match 0 0 1 1 foo/bar 'foo[/]bar'
match 0 0 1 1 foo/bar 'foo[^a-z]bar'
match 0 0 1 1 ab/cXd/efXg/hi '*Xg*i'

# Extra case-sensitivity tests
match 0 1 0 1 'a' '[A-Z]'
match 1 1 1 1 'A' '[A-Z]'
match 0 1 0 1 'A' '[a-z]'
match 1 1 1 1 'a' '[a-z]'
match 0 1 0 1 'a' '[[:upper:]]'
match 1 1 1 1 'A' '[[:upper:]]'
match 0 1 0 1 'A' '[[:lower:]]'
match 1 1 1 1 'a' '[[:lower:]]'
match 0 1 0 1 'A' '[B-Za]'
match 1 1 1 1 'a' '[B-Za]'
match 0 1 0 1 'A' '[B-a]'
match 1 1 1 1 'a' '[B-a]'
match 0 1 0 1 'z' '[Z-y]'
match 1 1 1 1 'Z' '[Z-y]'

test_done
