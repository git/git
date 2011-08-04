#!/bin/sh
#
# Copyright (c) 2007 Christian Couder
#
test_description='Tests git bisect functionality'

exec </dev/null

. ./test-lib.sh

add_line_into_file()
{
    _line=$1
    _file=$2

    if [ -f "$_file" ]; then
        echo "$_line" >> $_file || return $?
        MSG="Add <$_line> into <$_file>."
    else
        echo "$_line" > $_file || return $?
        git add $_file || return $?
        MSG="Create file <$_file> with <$_line> inside."
    fi

    test_tick
    git commit --quiet -m "$MSG" $_file
}

HASH1=
HASH2=
HASH3=
HASH4=

test_expect_success 'set up basic repo with 1 file (hello) and 4 commits' '
     add_line_into_file "1: Hello World" hello &&
     HASH1=$(git rev-parse --verify HEAD) &&
     add_line_into_file "2: A new day for git" hello &&
     HASH2=$(git rev-parse --verify HEAD) &&
     add_line_into_file "3: Another new day for git" hello &&
     HASH3=$(git rev-parse --verify HEAD) &&
     add_line_into_file "4: Ciao for now" hello &&
     HASH4=$(git rev-parse --verify HEAD)
'

test_expect_success 'bisect starts with only one bad' '
	git bisect reset &&
	git bisect start &&
	git bisect bad $HASH4 &&
	git bisect next
'

test_expect_success 'bisect does not start with only one good' '
	git bisect reset &&
	git bisect start &&
	git bisect good $HASH1 || return 1

	if git bisect next
	then
		echo Oops, should have failed.
		false
	else
		:
	fi
'

test_expect_success 'bisect start with one bad and good' '
	git bisect reset &&
	git bisect start &&
	git bisect good $HASH1 &&
	git bisect bad $HASH4 &&
	git bisect next
'

test_expect_success 'bisect fails if given any junk instead of revs' '
	git bisect reset &&
	test_must_fail git bisect start foo $HASH1 -- &&
	test_must_fail git bisect start $HASH4 $HASH1 bar -- &&
	test -z "$(git for-each-ref "refs/bisect/*")" &&
	test -z "$(ls .git/BISECT_* 2>/dev/null)" &&
	git bisect start &&
	test_must_fail git bisect good foo $HASH1 &&
	test_must_fail git bisect good $HASH1 bar &&
	test_must_fail git bisect bad frotz &&
	test_must_fail git bisect bad $HASH3 $HASH4 &&
	test_must_fail git bisect skip bar $HASH3 &&
	test_must_fail git bisect skip $HASH1 foo &&
	test -z "$(git for-each-ref "refs/bisect/*")" &&
	git bisect good $HASH1 &&
	git bisect bad $HASH4
'

test_expect_success 'bisect reset: back in the master branch' '
	git bisect reset &&
	echo "* master" > branch.expect &&
	git branch > branch.output &&
	cmp branch.expect branch.output
'

test_expect_success 'bisect reset: back in another branch' '
	git checkout -b other &&
	git bisect start &&
	git bisect good $HASH1 &&
	git bisect bad $HASH3 &&
	git bisect reset &&
	echo "  master" > branch.expect &&
	echo "* other" >> branch.expect &&
	git branch > branch.output &&
	cmp branch.expect branch.output
'

test_expect_success 'bisect reset when not bisecting' '
	git bisect reset &&
	git branch > branch.output &&
	cmp branch.expect branch.output
'

test_expect_success 'bisect reset removes packed refs' '
	git bisect reset &&
	git bisect start &&
	git bisect good $HASH1 &&
	git bisect bad $HASH3 &&
	git pack-refs --all --prune &&
	git bisect next &&
	git bisect reset &&
	test -z "$(git for-each-ref "refs/bisect/*")" &&
	test -z "$(git for-each-ref "refs/heads/bisect")"
'

test_expect_success 'bisect start: back in good branch' '
	git branch > branch.output &&
	grep "* other" branch.output > /dev/null &&
	git bisect start $HASH4 $HASH1 -- &&
	git bisect good &&
	git bisect start $HASH4 $HASH1 -- &&
	git bisect bad &&
	git bisect reset &&
	git branch > branch.output &&
	grep "* other" branch.output > /dev/null
'

test_expect_success 'bisect start: no ".git/BISECT_START" created if junk rev' '
	git bisect reset &&
	test_must_fail git bisect start $HASH4 foo -- &&
	git branch > branch.output &&
	grep "* other" branch.output > /dev/null &&
	test_must_fail test -e .git/BISECT_START
'

test_expect_success 'bisect start: existing ".git/BISECT_START" not modified if junk rev' '
	git bisect start $HASH4 $HASH1 -- &&
	git bisect good &&
	cp .git/BISECT_START saved &&
	test_must_fail git bisect start $HASH4 foo -- &&
	git branch > branch.output &&
	grep "* (no branch)" branch.output > /dev/null &&
	test_cmp saved .git/BISECT_START
'
test_expect_success 'bisect start: no ".git/BISECT_START" if mistaken rev' '
	git bisect start $HASH4 $HASH1 -- &&
	git bisect good &&
	test_must_fail git bisect start $HASH1 $HASH4 -- &&
	git branch > branch.output &&
	grep "* other" branch.output > /dev/null &&
	test_must_fail test -e .git/BISECT_START
'

test_expect_success 'bisect start: no ".git/BISECT_START" if checkout error' '
	echo "temp stuff" > hello &&
	test_must_fail git bisect start $HASH4 $HASH1 -- &&
	git branch &&
	git branch > branch.output &&
	grep "* other" branch.output > /dev/null &&
	test_must_fail test -e .git/BISECT_START &&
	test -z "$(git for-each-ref "refs/bisect/*")" &&
	git checkout HEAD hello
'

# $HASH1 is good, $HASH4 is bad, we skip $HASH3
# but $HASH2 is bad,
# so we should find $HASH2 as the first bad commit
test_expect_success 'bisect skip: successfull result' '
	git bisect reset &&
	git bisect start $HASH4 $HASH1 &&
	git bisect skip &&
	git bisect bad > my_bisect_log.txt &&
	grep "$HASH2 is the first bad commit" my_bisect_log.txt &&
	git bisect reset
'

# $HASH1 is good, $HASH4 is bad, we skip $HASH3 and $HASH2
# so we should not be able to tell the first bad commit
# among $HASH2, $HASH3 and $HASH4
test_expect_success 'bisect skip: cannot tell between 3 commits' '
	git bisect start $HASH4 $HASH1 &&
	git bisect skip || return 1

	if git bisect skip > my_bisect_log.txt
	then
		echo Oops, should have failed.
		false
	else
		test $? -eq 2 &&
		grep "first bad commit could be any of" my_bisect_log.txt &&
		! grep $HASH1 my_bisect_log.txt &&
		grep $HASH2 my_bisect_log.txt &&
		grep $HASH3 my_bisect_log.txt &&
		grep $HASH4 my_bisect_log.txt &&
		git bisect reset
	fi
'

# $HASH1 is good, $HASH4 is bad, we skip $HASH3
# but $HASH2 is good,
# so we should not be able to tell the first bad commit
# among $HASH3 and $HASH4
test_expect_success 'bisect skip: cannot tell between 2 commits' '
	git bisect start $HASH4 $HASH1 &&
	git bisect skip || return 1

	if git bisect good > my_bisect_log.txt
	then
		echo Oops, should have failed.
		false
	else
		test $? -eq 2 &&
		grep "first bad commit could be any of" my_bisect_log.txt &&
		! grep $HASH1 my_bisect_log.txt &&
		! grep $HASH2 my_bisect_log.txt &&
		grep $HASH3 my_bisect_log.txt &&
		grep $HASH4 my_bisect_log.txt &&
		git bisect reset
	fi
'

# $HASH1 is good, $HASH4 is both skipped and bad, we skip $HASH3
# and $HASH2 is good,
# so we should not be able to tell the first bad commit
# among $HASH3 and $HASH4
test_expect_success 'bisect skip: with commit both bad and skipped' '
	git bisect start &&
	git bisect skip &&
	git bisect bad &&
	git bisect good $HASH1 &&
	git bisect skip &&
	if git bisect good > my_bisect_log.txt
	then
		echo Oops, should have failed.
		false
	else
		test $? -eq 2 &&
		grep "first bad commit could be any of" my_bisect_log.txt &&
		! grep $HASH1 my_bisect_log.txt &&
		! grep $HASH2 my_bisect_log.txt &&
		grep $HASH3 my_bisect_log.txt &&
		grep $HASH4 my_bisect_log.txt &&
		git bisect reset
	fi
'

# We want to automatically find the commit that
# introduced "Another" into hello.
test_expect_success \
    '"git bisect run" simple case' \
    'echo "#"\!"/bin/sh" > test_script.sh &&
     echo "grep Another hello > /dev/null" >> test_script.sh &&
     echo "test \$? -ne 0" >> test_script.sh &&
     chmod +x test_script.sh &&
     git bisect start &&
     git bisect good $HASH1 &&
     git bisect bad $HASH4 &&
     git bisect run ./test_script.sh > my_bisect_log.txt &&
     grep "$HASH3 is the first bad commit" my_bisect_log.txt &&
     git bisect reset'

# We want to automatically find the commit that
# introduced "Ciao" into hello.
test_expect_success \
    '"git bisect run" with more complex "git bisect start"' \
    'echo "#"\!"/bin/sh" > test_script.sh &&
     echo "grep Ciao hello > /dev/null" >> test_script.sh &&
     echo "test \$? -ne 0" >> test_script.sh &&
     chmod +x test_script.sh &&
     git bisect start $HASH4 $HASH1 &&
     git bisect run ./test_script.sh > my_bisect_log.txt &&
     grep "$HASH4 is the first bad commit" my_bisect_log.txt &&
     git bisect reset'

# $HASH1 is good, $HASH5 is bad, we skip $HASH3
# but $HASH4 is good,
# so we should find $HASH5 as the first bad commit
HASH5=
test_expect_success 'bisect skip: add line and then a new test' '
	add_line_into_file "5: Another new line." hello &&
	HASH5=$(git rev-parse --verify HEAD) &&
	git bisect start $HASH5 $HASH1 &&
	git bisect skip &&
	git bisect good > my_bisect_log.txt &&
	grep "$HASH5 is the first bad commit" my_bisect_log.txt &&
	git bisect log > log_to_replay.txt &&
	git bisect reset
'

test_expect_success 'bisect skip and bisect replay' '
	git bisect replay log_to_replay.txt > my_bisect_log.txt &&
	grep "$HASH5 is the first bad commit" my_bisect_log.txt &&
	git bisect reset
'

HASH6=
test_expect_success 'bisect run & skip: cannot tell between 2' '
	add_line_into_file "6: Yet a line." hello &&
	HASH6=$(git rev-parse --verify HEAD) &&
	echo "#"\!"/bin/sh" > test_script.sh &&
	echo "sed -ne \\\$p hello | grep Ciao > /dev/null && exit 125" >> test_script.sh &&
	echo "grep line hello > /dev/null" >> test_script.sh &&
	echo "test \$? -ne 0" >> test_script.sh &&
	chmod +x test_script.sh &&
	git bisect start $HASH6 $HASH1 &&
	if git bisect run ./test_script.sh > my_bisect_log.txt
	then
		echo Oops, should have failed.
		false
	else
		test $? -eq 2 &&
		grep "first bad commit could be any of" my_bisect_log.txt &&
		! grep $HASH3 my_bisect_log.txt &&
		! grep $HASH6 my_bisect_log.txt &&
		grep $HASH4 my_bisect_log.txt &&
		grep $HASH5 my_bisect_log.txt
	fi
'

HASH7=
test_expect_success 'bisect run & skip: find first bad' '
	git bisect reset &&
	add_line_into_file "7: Should be the last line." hello &&
	HASH7=$(git rev-parse --verify HEAD) &&
	echo "#"\!"/bin/sh" > test_script.sh &&
	echo "sed -ne \\\$p hello | grep Ciao > /dev/null && exit 125" >> test_script.sh &&
	echo "sed -ne \\\$p hello | grep day > /dev/null && exit 125" >> test_script.sh &&
	echo "grep Yet hello > /dev/null" >> test_script.sh &&
	echo "test \$? -ne 0" >> test_script.sh &&
	chmod +x test_script.sh &&
	git bisect start $HASH7 $HASH1 &&
	git bisect run ./test_script.sh > my_bisect_log.txt &&
	grep "$HASH6 is the first bad commit" my_bisect_log.txt
'

test_expect_success 'bisect skip only one range' '
	git bisect reset &&
	git bisect start $HASH7 $HASH1 &&
	git bisect skip $HASH1..$HASH5 &&
	test "$HASH6" = "$(git rev-parse --verify HEAD)" &&
	test_must_fail git bisect bad > my_bisect_log.txt &&
	grep "first bad commit could be any of" my_bisect_log.txt
'

test_expect_success 'bisect skip many ranges' '
	git bisect start $HASH7 $HASH1 &&
	test "$HASH4" = "$(git rev-parse --verify HEAD)" &&
	git bisect skip $HASH2 $HASH2.. ..$HASH5 &&
	test "$HASH6" = "$(git rev-parse --verify HEAD)" &&
	test_must_fail git bisect bad > my_bisect_log.txt &&
	grep "first bad commit could be any of" my_bisect_log.txt
'

test_expect_success 'bisect starting with a detached HEAD' '
	git bisect reset &&
	git checkout master^ &&
	HEAD=$(git rev-parse --verify HEAD) &&
	git bisect start &&
	test $HEAD = $(cat .git/BISECT_START) &&
	git bisect reset &&
	test $HEAD = $(git rev-parse --verify HEAD)
'

test_expect_success 'bisect errors out if bad and good are mistaken' '
	git bisect reset &&
	test_must_fail git bisect start $HASH2 $HASH4 2> rev_list_error &&
	grep "mistake good and bad" rev_list_error &&
	git bisect reset
'

test_expect_success 'bisect does not create a "bisect" branch' '
	git bisect reset &&
	git bisect start $HASH7 $HASH1 &&
	git branch bisect &&
	rev_hash4=$(git rev-parse --verify HEAD) &&
	test "$rev_hash4" = "$HASH4" &&
	git branch -D bisect &&
	git bisect good &&
	git branch bisect &&
	rev_hash6=$(git rev-parse --verify HEAD) &&
	test "$rev_hash6" = "$HASH6" &&
	git bisect good > my_bisect_log.txt &&
	grep "$HASH7 is the first bad commit" my_bisect_log.txt &&
	git bisect reset &&
	rev_hash6=$(git rev-parse --verify bisect) &&
	test "$rev_hash6" = "$HASH6" &&
	git branch -D bisect
'

# This creates a "side" branch to test "siblings" cases.
#
# H1-H2-H3-H4-H5-H6-H7  <--other
#            \
#             S5-S6-S7  <--side
#
test_expect_success 'side branch creation' '
	git bisect reset &&
	git checkout -b side $HASH4 &&
	add_line_into_file "5(side): first line on a side branch" hello2 &&
	SIDE_HASH5=$(git rev-parse --verify HEAD) &&
	add_line_into_file "6(side): second line on a side branch" hello2 &&
	SIDE_HASH6=$(git rev-parse --verify HEAD) &&
	add_line_into_file "7(side): third line on a side branch" hello2 &&
	SIDE_HASH7=$(git rev-parse --verify HEAD)
'

test_expect_success 'good merge base when good and bad are siblings' '
	git bisect start "$HASH7" "$SIDE_HASH7" > my_bisect_log.txt &&
	grep "merge base must be tested" my_bisect_log.txt &&
	grep $HASH4 my_bisect_log.txt &&
	git bisect good > my_bisect_log.txt &&
	test_must_fail grep "merge base must be tested" my_bisect_log.txt &&
	grep $HASH6 my_bisect_log.txt &&
	git bisect reset
'
test_expect_success 'skipped merge base when good and bad are siblings' '
	git bisect start "$SIDE_HASH7" "$HASH7" > my_bisect_log.txt &&
	grep "merge base must be tested" my_bisect_log.txt &&
	grep $HASH4 my_bisect_log.txt &&
	git bisect skip > my_bisect_log.txt 2>&1 &&
	grep "warning" my_bisect_log.txt &&
	grep $SIDE_HASH6 my_bisect_log.txt &&
	git bisect reset
'

test_expect_success 'bad merge base when good and bad are siblings' '
	git bisect start "$HASH7" HEAD > my_bisect_log.txt &&
	grep "merge base must be tested" my_bisect_log.txt &&
	grep $HASH4 my_bisect_log.txt &&
	test_must_fail git bisect bad > my_bisect_log.txt 2>&1 &&
	grep "merge base $HASH4 is bad" my_bisect_log.txt &&
	grep "fixed between $HASH4 and \[$SIDE_HASH7\]" my_bisect_log.txt &&
	git bisect reset
'

# This creates a few more commits (A and B) to test "siblings" cases
# when a good and a bad rev have many merge bases.
#
# We should have the following:
#
# H1-H2-H3-H4-H5-H6-H7
#            \  \     \
#             S5-A     \
#              \        \
#               S6-S7----B
#
# And there A and B have 2 merge bases (S5 and H5) that should be
# reported by "git merge-base --all A B".
#
test_expect_success 'many merge bases creation' '
	git checkout "$SIDE_HASH5" &&
	git merge -m "merge HASH5 and SIDE_HASH5" "$HASH5" &&
	A_HASH=$(git rev-parse --verify HEAD) &&
	git checkout side &&
	git merge -m "merge HASH7 and SIDE_HASH7" "$HASH7" &&
	B_HASH=$(git rev-parse --verify HEAD) &&
	git merge-base --all "$A_HASH" "$B_HASH" > merge_bases.txt &&
	test $(wc -l < merge_bases.txt) = "2" &&
	grep "$HASH5" merge_bases.txt &&
	grep "$SIDE_HASH5" merge_bases.txt
'

test_expect_success 'good merge bases when good and bad are siblings' '
	git bisect start "$B_HASH" "$A_HASH" > my_bisect_log.txt &&
	grep "merge base must be tested" my_bisect_log.txt &&
	git bisect good > my_bisect_log2.txt &&
	grep "merge base must be tested" my_bisect_log2.txt &&
	{
		{
			grep "$SIDE_HASH5" my_bisect_log.txt &&
			grep "$HASH5" my_bisect_log2.txt
		} || {
			grep "$SIDE_HASH5" my_bisect_log2.txt &&
			grep "$HASH5" my_bisect_log.txt
		}
	} &&
	git bisect reset
'

test_expect_success 'optimized merge base checks' '
	git bisect start "$HASH7" "$SIDE_HASH7" > my_bisect_log.txt &&
	grep "merge base must be tested" my_bisect_log.txt &&
	grep "$HASH4" my_bisect_log.txt &&
	git bisect good > my_bisect_log2.txt &&
	test -f ".git/BISECT_ANCESTORS_OK" &&
	test "$HASH6" = $(git rev-parse --verify HEAD) &&
	git bisect bad > my_bisect_log3.txt &&
	git bisect good "$A_HASH" > my_bisect_log4.txt &&
	grep "merge base must be tested" my_bisect_log4.txt &&
	test_must_fail test -f ".git/BISECT_ANCESTORS_OK"
'

# This creates another side branch called "parallel" with some files
# in some directories, to test bisecting with paths.
#
# We should have the following:
#
#    P1-P2-P3-P4-P5-P6-P7
#   /        /        /
# H1-H2-H3-H4-H5-H6-H7
#            \  \     \
#             S5-A     \
#              \        \
#               S6-S7----B
#
test_expect_success '"parallel" side branch creation' '
	git bisect reset &&
	git checkout -b parallel $HASH1 &&
	mkdir dir1 dir2 &&
	add_line_into_file "1(para): line 1 on parallel branch" dir1/file1 &&
	PARA_HASH1=$(git rev-parse --verify HEAD) &&
	add_line_into_file "2(para): line 2 on parallel branch" dir2/file2 &&
	PARA_HASH2=$(git rev-parse --verify HEAD) &&
	add_line_into_file "3(para): line 3 on parallel branch" dir2/file3 &&
	PARA_HASH3=$(git rev-parse --verify HEAD) &&
	git merge -m "merge HASH4 and PARA_HASH3" "$HASH4" &&
	PARA_HASH4=$(git rev-parse --verify HEAD) &&
	add_line_into_file "5(para): add line on parallel branch" dir1/file1 &&
	PARA_HASH5=$(git rev-parse --verify HEAD) &&
	add_line_into_file "6(para): add line on parallel branch" dir2/file2 &&
	PARA_HASH6=$(git rev-parse --verify HEAD) &&
	git merge -m "merge HASH7 and PARA_HASH6" "$HASH7" &&
	PARA_HASH7=$(git rev-parse --verify HEAD)
'

test_expect_success 'restricting bisection on one dir' '
	git bisect reset &&
	git bisect start HEAD $HASH1 -- dir1 &&
	para1=$(git rev-parse --verify HEAD) &&
	test "$para1" = "$PARA_HASH1" &&
	git bisect bad > my_bisect_log.txt &&
	grep "$PARA_HASH1 is the first bad commit" my_bisect_log.txt
'

test_expect_success 'restricting bisection on one dir and a file' '
	git bisect reset &&
	git bisect start HEAD $HASH1 -- dir1 hello &&
	para4=$(git rev-parse --verify HEAD) &&
	test "$para4" = "$PARA_HASH4" &&
	git bisect bad &&
	hash3=$(git rev-parse --verify HEAD) &&
	test "$hash3" = "$HASH3" &&
	git bisect good &&
	hash4=$(git rev-parse --verify HEAD) &&
	test "$hash4" = "$HASH4" &&
	git bisect good &&
	para1=$(git rev-parse --verify HEAD) &&
	test "$para1" = "$PARA_HASH1" &&
	git bisect good > my_bisect_log.txt &&
	grep "$PARA_HASH4 is the first bad commit" my_bisect_log.txt
'

test_expect_success 'skipping away from skipped commit' '
	git bisect start $PARA_HASH7 $HASH1 &&
	para4=$(git rev-parse --verify HEAD) &&
	test "$para4" = "$PARA_HASH4" &&
        git bisect skip &&
	hash7=$(git rev-parse --verify HEAD) &&
	test "$hash7" = "$HASH7" &&
        git bisect skip &&
	para3=$(git rev-parse --verify HEAD) &&
	test "$para3" = "$PARA_HASH3"
'

test_expect_success 'erroring out when using bad path parameters' '
	test_must_fail git bisect start $PARA_HASH7 $HASH1 -- foobar 2> error.txt &&
	grep "bad path parameters" error.txt
'

#
#
test_done
