#!/bin/sh
#
# Copyright (c) 2007 Christian Couder
#
test_description='Tests git-bisect functionality'

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
    git-commit --quiet -m "$MSG" $_file
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

# $HASH1 is good, $HASH4 is bad, we skip $HASH3
# but $HASH2 is bad,
# so we should find $HASH2 as the first bad commit
test_expect_success 'bisect skip: successfull result' '
	git bisect reset &&
	git bisect start $HASH4 $HASH1 &&
	git bisect skip &&
	git bisect bad > my_bisect_log.txt &&
	grep "$HASH2 is first bad commit" my_bisect_log.txt &&
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
     grep "$HASH3 is first bad commit" my_bisect_log.txt &&
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
     grep "$HASH4 is first bad commit" my_bisect_log.txt &&
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
	grep "$HASH5 is first bad commit" my_bisect_log.txt &&
	git bisect log > log_to_replay.txt &&
	git bisect reset
'

test_expect_success 'bisect skip and bisect replay' '
	git bisect replay log_to_replay.txt > my_bisect_log.txt &&
	grep "$HASH5 is first bad commit" my_bisect_log.txt &&
	git bisect reset
'

HASH6=
test_expect_success 'bisect run & skip: cannot tell between 2' '
	add_line_into_file "6: Yet a line." hello &&
	HASH6=$(git rev-parse --verify HEAD) &&
	echo "#"\!"/bin/sh" > test_script.sh &&
	echo "tail -1 hello | grep Ciao > /dev/null && exit 125" >> test_script.sh &&
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
	echo "tail -1 hello | grep Ciao > /dev/null && exit 125" >> test_script.sh &&
	echo "tail -1 hello | grep day > /dev/null && exit 125" >> test_script.sh &&
	echo "grep Yet hello > /dev/null" >> test_script.sh &&
	echo "test \$? -ne 0" >> test_script.sh &&
	chmod +x test_script.sh &&
	git bisect start $HASH7 $HASH1 &&
	git bisect run ./test_script.sh > my_bisect_log.txt &&
	grep "$HASH6 is first bad commit" my_bisect_log.txt
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

#
#
test_done
