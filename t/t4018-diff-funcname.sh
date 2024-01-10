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

test_expect_success 'setup: test-tool userdiff' '
	# Make sure additions to builtin_drivers are sorted
	test_when_finished "rm builtin-drivers.sorted" &&
	test-tool userdiff list-builtin-drivers >builtin-drivers &&
	test_file_not_empty builtin-drivers &&
	sort <builtin-drivers >builtin-drivers.sorted &&
	test_cmp builtin-drivers.sorted builtin-drivers &&

	# Ditto, but "custom" requires the .git directory and config
	# to be setup and read.
	test_when_finished "rm custom-drivers.sorted" &&
	test-tool userdiff list-custom-drivers >custom-drivers &&
	test_file_not_empty custom-drivers &&
	sort <custom-drivers >custom-drivers.sorted &&
	test_cmp custom-drivers.sorted custom-drivers
'

diffpatterns="
	$(cat builtin-drivers)
	$(cat custom-drivers)
"

for p in $diffpatterns
do
	test_expect_success "builtin $p pattern compiles" '
		echo "*.java diff=$p" >.gitattributes &&
		test_expect_code 1 git diff --no-index \
			A.java B.java 2>msg &&
		test_grep ! fatal msg &&
		test_grep ! error msg
	'
	test_expect_success "builtin $p wordRegex pattern compiles" '
		echo "*.java diff=$p" >.gitattributes &&
		test_expect_code 1 git diff --no-index --word-diff \
			A.java B.java 2>msg &&
		test_grep ! fatal msg &&
		test_grep ! error msg
	'

	test_expect_success "builtin $p pattern compiles on bare repo with --attr-source" '
		test_when_finished "rm -rf bare.git" &&
		git checkout -B master &&
		git add . &&
		echo "*.java diff=notexist" >.gitattributes &&
		git add .gitattributes &&
		git commit -am "changing gitattributes" &&
		git checkout -B branchA &&
		echo "*.java diff=$p" >.gitattributes &&
		git add .gitattributes &&
		git commit -am "changing gitattributes" &&
		git clone --bare --no-local . bare.git &&
		git -C bare.git symbolic-ref HEAD refs/heads/master &&
		test_expect_code 1 git -C bare.git --attr-source=branchA \
			diff --exit-code HEAD:A.java HEAD:B.java 2>msg &&
		test_grep ! fatal msg &&
		test_grep ! error msg
	'
done

test_expect_success 'last regexp must not be negated' '
	echo "*.java diff=java" >.gitattributes &&
	test_config diff.java.funcname "!static" &&
	test_expect_code 128 git diff --no-index A.java B.java 2>msg &&
	test_grep ": Last expression must not be negated:" msg
'

test_expect_success 'setup hunk header tests' '
	for i in $diffpatterns
	do
		echo "$i-* diff=$i" || return 1
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
	test_expect_success "hunk header: $i" "
		git diff -U1 $i >actual &&
		grep '@@ .* @@.*RIGHT' actual
	"
done

test_done
