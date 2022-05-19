#!/bin/sh
#
# Copyright (c) 2007 Christian Couder
#
test_description='Tests but bisect functionality'

exec </dev/null

BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME

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
        but add $_file || return $?
        MSG="Create file <$_file> with <$_line> inside."
    fi

    test_tick
    but cummit --quiet -m "$MSG" $_file
}

HASH1=
HASH2=
HASH3=
HASH4=

test_expect_success 'set up basic repo with 1 file (hello) and 4 cummits' '
     add_line_into_file "1: Hello World" hello &&
     HASH1=$(but rev-parse --verify HEAD) &&
     add_line_into_file "2: A new day for but" hello &&
     HASH2=$(but rev-parse --verify HEAD) &&
     add_line_into_file "3: Another new day for but" hello &&
     HASH3=$(but rev-parse --verify HEAD) &&
     add_line_into_file "4: Ciao for now" hello &&
     HASH4=$(but rev-parse --verify HEAD)
'

test_expect_success 'bisect starts with only one bad' '
	but bisect reset &&
	but bisect start &&
	but bisect bad $HASH4 &&
	but bisect next
'

test_expect_success 'bisect does not start with only one good' '
	but bisect reset &&
	but bisect start &&
	but bisect good $HASH1 &&
	test_must_fail but bisect next
'

test_expect_success 'bisect start with one bad and good' '
	but bisect reset &&
	but bisect start &&
	but bisect good $HASH1 &&
	but bisect bad $HASH4 &&
	but bisect next
'

test_expect_success 'bisect fails if given any junk instead of revs' '
	but bisect reset &&
	test_must_fail but bisect start foo $HASH1 -- &&
	test_must_fail but bisect start $HASH4 $HASH1 bar -- &&
	test -z "$(but for-each-ref "refs/bisect/*")" &&
	test -z "$(ls .but/BISECT_* 2>/dev/null)" &&
	but bisect start &&
	test_must_fail but bisect good foo $HASH1 &&
	test_must_fail but bisect good $HASH1 bar &&
	test_must_fail but bisect bad frotz &&
	test_must_fail but bisect bad $HASH3 $HASH4 &&
	test_must_fail but bisect skip bar $HASH3 &&
	test_must_fail but bisect skip $HASH1 foo &&
	test -z "$(but for-each-ref "refs/bisect/*")" &&
	but bisect good $HASH1 &&
	but bisect bad $HASH4
'

test_expect_success 'bisect start without -- takes unknown arg as pathspec' '
	but bisect reset &&
	but bisect start foo bar &&
	grep foo ".but/BISECT_NAMES" &&
	grep bar ".but/BISECT_NAMES"
'

test_expect_success 'bisect reset: back in the main branch' '
	but bisect reset &&
	echo "* main" > branch.expect &&
	but branch > branch.output &&
	cmp branch.expect branch.output
'

test_expect_success 'bisect reset: back in another branch' '
	but checkout -b other &&
	but bisect start &&
	but bisect good $HASH1 &&
	but bisect bad $HASH3 &&
	but bisect reset &&
	echo "  main" > branch.expect &&
	echo "* other" >> branch.expect &&
	but branch > branch.output &&
	cmp branch.expect branch.output
'

test_expect_success 'bisect reset when not bisecting' '
	but bisect reset &&
	but branch > branch.output &&
	cmp branch.expect branch.output
'

test_expect_success 'bisect reset removes packed refs' '
	but bisect reset &&
	but bisect start &&
	but bisect good $HASH1 &&
	but bisect bad $HASH3 &&
	but pack-refs --all --prune &&
	but bisect next &&
	but bisect reset &&
	test -z "$(but for-each-ref "refs/bisect/*")" &&
	test -z "$(but for-each-ref "refs/heads/bisect")"
'

test_expect_success 'bisect reset removes bisect state after --no-checkout' '
	but bisect reset &&
	but bisect start --no-checkout &&
	but bisect good $HASH1 &&
	but bisect bad $HASH3 &&
	but bisect next &&
	but bisect reset &&
	test -z "$(but for-each-ref "refs/bisect/*")" &&
	test -z "$(but for-each-ref "refs/heads/bisect")" &&
	test -z "$(but for-each-ref "BISECT_HEAD")"
'

test_expect_success 'bisect start: back in good branch' '
	but branch > branch.output &&
	grep "* other" branch.output > /dev/null &&
	but bisect start $HASH4 $HASH1 -- &&
	but bisect good &&
	but bisect start $HASH4 $HASH1 -- &&
	but bisect bad &&
	but bisect reset &&
	but branch > branch.output &&
	grep "* other" branch.output > /dev/null
'

test_expect_success 'bisect start: no ".but/BISECT_START" created if junk rev' '
	but bisect reset &&
	test_must_fail but bisect start $HASH4 foo -- &&
	but branch > branch.output &&
	grep "* other" branch.output > /dev/null &&
	test_path_is_missing .but/BISECT_START
'

test_expect_success 'bisect start: existing ".but/BISECT_START" not modified if junk rev' '
	but bisect start $HASH4 $HASH1 -- &&
	but bisect good &&
	cp .but/BISECT_START saved &&
	test_must_fail but bisect start $HASH4 foo -- &&
	but branch > branch.output &&
	test_i18ngrep "* (no branch, bisect started on other)" branch.output > /dev/null &&
	test_cmp saved .but/BISECT_START
'
test_expect_success 'bisect start: no ".but/BISECT_START" if mistaken rev' '
	but bisect start $HASH4 $HASH1 -- &&
	but bisect good &&
	test_must_fail but bisect start $HASH1 $HASH4 -- &&
	but branch > branch.output &&
	grep "* other" branch.output > /dev/null &&
	test_path_is_missing .but/BISECT_START
'

test_expect_success 'bisect start: no ".but/BISECT_START" if checkout error' '
	echo "temp stuff" > hello &&
	test_must_fail but bisect start $HASH4 $HASH1 -- &&
	but branch &&
	but branch > branch.output &&
	grep "* other" branch.output > /dev/null &&
	test_path_is_missing .but/BISECT_START &&
	test -z "$(but for-each-ref "refs/bisect/*")" &&
	but checkout HEAD hello
'

# $HASH1 is good, $HASH4 is bad, we skip $HASH3
# but $HASH2 is bad,
# so we should find $HASH2 as the first bad cummit
test_expect_success 'bisect skip: successful result' '
	test_when_finished but bisect reset &&
	but bisect reset &&
	but bisect start $HASH4 $HASH1 &&
	but bisect skip &&
	but bisect bad > my_bisect_log.txt &&
	grep "$HASH2 is the first bad cummit" my_bisect_log.txt
'

# $HASH1 is good, $HASH4 is bad, we skip $HASH3 and $HASH2
# so we should not be able to tell the first bad cummit
# among $HASH2, $HASH3 and $HASH4
test_expect_success 'bisect skip: cannot tell between 3 cummits' '
	test_when_finished but bisect reset &&
	but bisect start $HASH4 $HASH1 &&
	but bisect skip &&
	test_expect_code 2 but bisect skip >my_bisect_log.txt &&
	grep "first bad cummit could be any of" my_bisect_log.txt &&
	! grep $HASH1 my_bisect_log.txt &&
	grep $HASH2 my_bisect_log.txt &&
	grep $HASH3 my_bisect_log.txt &&
	grep $HASH4 my_bisect_log.txt
'

# $HASH1 is good, $HASH4 is bad, we skip $HASH3
# but $HASH2 is good,
# so we should not be able to tell the first bad cummit
# among $HASH3 and $HASH4
test_expect_success 'bisect skip: cannot tell between 2 cummits' '
	test_when_finished but bisect reset &&
	but bisect start $HASH4 $HASH1 &&
	but bisect skip &&
	test_expect_code 2 but bisect good >my_bisect_log.txt &&
	grep "first bad cummit could be any of" my_bisect_log.txt &&
	! grep $HASH1 my_bisect_log.txt &&
	! grep $HASH2 my_bisect_log.txt &&
	grep $HASH3 my_bisect_log.txt &&
	grep $HASH4 my_bisect_log.txt
'

# $HASH1 is good, $HASH4 is both skipped and bad, we skip $HASH3
# and $HASH2 is good,
# so we should not be able to tell the first bad cummit
# among $HASH3 and $HASH4
test_expect_success 'bisect skip: with cummit both bad and skipped' '
	test_when_finished but bisect reset &&
	but bisect start &&
	but bisect skip &&
	but bisect bad &&
	but bisect good $HASH1 &&
	but bisect skip &&
	test_expect_code 2 but bisect good >my_bisect_log.txt &&
	grep "first bad cummit could be any of" my_bisect_log.txt &&
	! grep $HASH1 my_bisect_log.txt &&
	! grep $HASH2 my_bisect_log.txt &&
	grep $HASH3 my_bisect_log.txt &&
	grep $HASH4 my_bisect_log.txt
'

# We want to automatically find the cummit that
# added "Another" into hello.
test_expect_success '"but bisect run" simple case' '
	write_script test_script.sh <<-\EOF &&
	! grep Another hello >/dev/null
	EOF
	but bisect start &&
	but bisect good $HASH1 &&
	but bisect bad $HASH4 &&
	but bisect run ./test_script.sh >my_bisect_log.txt &&
	grep "$HASH3 is the first bad cummit" my_bisect_log.txt &&
	but bisect reset
'

# We want to automatically find the cummit that
# added "Ciao" into hello.
test_expect_success '"but bisect run" with more complex "but bisect start"' '
	write_script test_script.sh <<-\EOF &&
	! grep Ciao hello >/dev/null
	EOF
	but bisect start $HASH4 $HASH1 &&
	but bisect run ./test_script.sh >my_bisect_log.txt &&
	grep "$HASH4 is the first bad cummit" my_bisect_log.txt &&
	but bisect reset
'

test_expect_success 'bisect run accepts exit code 126 as bad' '
	test_when_finished "but bisect reset" &&
	write_script test_script.sh <<-\EOF &&
	! grep Another hello || exit 126 >/dev/null
	EOF
	but bisect start &&
	but bisect good $HASH1 &&
	but bisect bad $HASH4 &&
	but bisect run ./test_script.sh >my_bisect_log.txt &&
	grep "$HASH3 is the first bad cummit" my_bisect_log.txt
'

test_expect_success POSIXPERM 'bisect run fails with non-executable test script' '
	test_when_finished "but bisect reset" &&
	>not-executable.sh &&
	chmod -x not-executable.sh &&
	but bisect start &&
	but bisect good $HASH1 &&
	but bisect bad $HASH4 &&
	test_must_fail but bisect run ./not-executable.sh >my_bisect_log.txt &&
	! grep "is the first bad cummit" my_bisect_log.txt
'

test_expect_success 'bisect run accepts exit code 127 as bad' '
	test_when_finished "but bisect reset" &&
	write_script test_script.sh <<-\EOF &&
	! grep Another hello || exit 127 >/dev/null
	EOF
	but bisect start &&
	but bisect good $HASH1 &&
	but bisect bad $HASH4 &&
	but bisect run ./test_script.sh >my_bisect_log.txt &&
	grep "$HASH3 is the first bad cummit" my_bisect_log.txt
'

test_expect_success 'bisect run fails with missing test script' '
	test_when_finished "but bisect reset" &&
	rm -f does-not-exist.sh &&
	but bisect start &&
	but bisect good $HASH1 &&
	but bisect bad $HASH4 &&
	test_must_fail but bisect run ./does-not-exist.sh >my_bisect_log.txt &&
	! grep "is the first bad cummit" my_bisect_log.txt
'

# $HASH1 is good, $HASH5 is bad, we skip $HASH3
# but $HASH4 is good,
# so we should find $HASH5 as the first bad cummit
HASH5=
test_expect_success 'bisect skip: add line and then a new test' '
	add_line_into_file "5: Another new line." hello &&
	HASH5=$(but rev-parse --verify HEAD) &&
	but bisect start $HASH5 $HASH1 &&
	but bisect skip &&
	but bisect good > my_bisect_log.txt &&
	grep "$HASH5 is the first bad cummit" my_bisect_log.txt &&
	but bisect log > log_to_replay.txt &&
	but bisect reset
'

test_expect_success 'bisect skip and bisect replay' '
	but bisect replay log_to_replay.txt > my_bisect_log.txt &&
	grep "$HASH5 is the first bad cummit" my_bisect_log.txt &&
	but bisect reset
'

HASH6=
test_expect_success 'bisect run & skip: cannot tell between 2' '
	add_line_into_file "6: Yet a line." hello &&
	HASH6=$(but rev-parse --verify HEAD) &&
	write_script test_script.sh <<-\EOF &&
	sed -ne \$p hello | grep Ciao >/dev/null && exit 125
	! grep line hello >/dev/null
	EOF
	but bisect start $HASH6 $HASH1 &&
	test_expect_code 2 but bisect run ./test_script.sh >my_bisect_log.txt &&
	grep "first bad cummit could be any of" my_bisect_log.txt &&
	! grep $HASH3 my_bisect_log.txt &&
	! grep $HASH6 my_bisect_log.txt &&
	grep $HASH4 my_bisect_log.txt &&
	grep $HASH5 my_bisect_log.txt
'

HASH7=
test_expect_success 'bisect run & skip: find first bad' '
	but bisect reset &&
	add_line_into_file "7: Should be the last line." hello &&
	HASH7=$(but rev-parse --verify HEAD) &&
	write_script test_script.sh <<-\EOF &&
	sed -ne \$p hello | grep Ciao >/dev/null && exit 125
	sed -ne \$p hello | grep day >/dev/null && exit 125
	! grep Yet hello >/dev/null
	EOF
	but bisect start $HASH7 $HASH1 &&
	but bisect run ./test_script.sh >my_bisect_log.txt &&
	grep "$HASH6 is the first bad cummit" my_bisect_log.txt
'

test_expect_success 'bisect skip only one range' '
	but bisect reset &&
	but bisect start $HASH7 $HASH1 &&
	but bisect skip $HASH1..$HASH5 &&
	test "$HASH6" = "$(but rev-parse --verify HEAD)" &&
	test_must_fail but bisect bad > my_bisect_log.txt &&
	grep "first bad cummit could be any of" my_bisect_log.txt
'

test_expect_success 'bisect skip many ranges' '
	but bisect start $HASH7 $HASH1 &&
	test "$HASH4" = "$(but rev-parse --verify HEAD)" &&
	but bisect skip $HASH2 $HASH2.. ..$HASH5 &&
	test "$HASH6" = "$(but rev-parse --verify HEAD)" &&
	test_must_fail but bisect bad > my_bisect_log.txt &&
	grep "first bad cummit could be any of" my_bisect_log.txt
'

test_expect_success 'bisect starting with a detached HEAD' '
	but bisect reset &&
	but checkout main^ &&
	HEAD=$(but rev-parse --verify HEAD) &&
	but bisect start &&
	test $HEAD = $(cat .but/BISECT_START) &&
	but bisect reset &&
	test $HEAD = $(but rev-parse --verify HEAD)
'

test_expect_success 'bisect errors out if bad and good are mistaken' '
	but bisect reset &&
	test_must_fail but bisect start $HASH2 $HASH4 2> rev_list_error &&
	test_i18ngrep "mistook good and bad" rev_list_error &&
	but bisect reset
'

test_expect_success 'bisect does not create a "bisect" branch' '
	but bisect reset &&
	but bisect start $HASH7 $HASH1 &&
	but branch bisect &&
	rev_hash4=$(but rev-parse --verify HEAD) &&
	test "$rev_hash4" = "$HASH4" &&
	but branch -D bisect &&
	but bisect good &&
	but branch bisect &&
	rev_hash6=$(but rev-parse --verify HEAD) &&
	test "$rev_hash6" = "$HASH6" &&
	but bisect good > my_bisect_log.txt &&
	grep "$HASH7 is the first bad cummit" my_bisect_log.txt &&
	but bisect reset &&
	rev_hash6=$(but rev-parse --verify bisect) &&
	test "$rev_hash6" = "$HASH6" &&
	but branch -D bisect
'

# This creates a "side" branch to test "siblings" cases.
#
# H1-H2-H3-H4-H5-H6-H7  <--other
#            \
#             S5-S6-S7  <--side
#
test_expect_success 'side branch creation' '
	but bisect reset &&
	but checkout -b side $HASH4 &&
	add_line_into_file "5(side): first line on a side branch" hello2 &&
	SIDE_HASH5=$(but rev-parse --verify HEAD) &&
	add_line_into_file "6(side): second line on a side branch" hello2 &&
	SIDE_HASH6=$(but rev-parse --verify HEAD) &&
	add_line_into_file "7(side): third line on a side branch" hello2 &&
	SIDE_HASH7=$(but rev-parse --verify HEAD)
'

test_expect_success 'good merge base when good and bad are siblings' '
	but bisect start "$HASH7" "$SIDE_HASH7" > my_bisect_log.txt &&
	test_i18ngrep "merge base must be tested" my_bisect_log.txt &&
	grep $HASH4 my_bisect_log.txt &&
	but bisect good > my_bisect_log.txt &&
	! grep "merge base must be tested" my_bisect_log.txt &&
	grep $HASH6 my_bisect_log.txt &&
	but bisect reset
'
test_expect_success 'skipped merge base when good and bad are siblings' '
	but bisect start "$SIDE_HASH7" "$HASH7" > my_bisect_log.txt &&
	test_i18ngrep "merge base must be tested" my_bisect_log.txt &&
	grep $HASH4 my_bisect_log.txt &&
	but bisect skip > my_bisect_log.txt 2>&1 &&
	grep "warning" my_bisect_log.txt &&
	grep $SIDE_HASH6 my_bisect_log.txt &&
	but bisect reset
'

test_expect_success 'bad merge base when good and bad are siblings' '
	but bisect start "$HASH7" HEAD > my_bisect_log.txt &&
	test_i18ngrep "merge base must be tested" my_bisect_log.txt &&
	grep $HASH4 my_bisect_log.txt &&
	test_must_fail but bisect bad > my_bisect_log.txt 2>&1 &&
	test_i18ngrep "merge base $HASH4 is bad" my_bisect_log.txt &&
	test_i18ngrep "fixed between $HASH4 and \[$SIDE_HASH7\]" my_bisect_log.txt &&
	but bisect reset
'

# This creates a few more cummits (A and B) to test "siblings" cases
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
# reported by "but merge-base --all A B".
#
test_expect_success 'many merge bases creation' '
	but checkout "$SIDE_HASH5" &&
	but merge -m "merge HASH5 and SIDE_HASH5" "$HASH5" &&
	A_HASH=$(but rev-parse --verify HEAD) &&
	but checkout side &&
	but merge -m "merge HASH7 and SIDE_HASH7" "$HASH7" &&
	B_HASH=$(but rev-parse --verify HEAD) &&
	but merge-base --all "$A_HASH" "$B_HASH" > merge_bases.txt &&
	test_line_count = 2 merge_bases.txt &&
	grep "$HASH5" merge_bases.txt &&
	grep "$SIDE_HASH5" merge_bases.txt
'

# We want to automatically find the merge that
# added "line" into hello.
test_expect_success '"but bisect run --first-parent" simple case' '
	but rev-list --first-parent $B_HASH ^$HASH4 >first_parent_chain.txt &&
	write_script test_script.sh <<-\EOF &&
	grep $(but rev-parse HEAD) first_parent_chain.txt || exit -1
	! grep line hello >/dev/null
	EOF
	but bisect start --first-parent &&
	test_path_is_file ".but/BISECT_FIRST_PARENT" &&
	but bisect good $HASH4 &&
	but bisect bad $B_HASH &&
	but bisect run ./test_script.sh >my_bisect_log.txt &&
	grep "$B_HASH is the first bad cummit" my_bisect_log.txt &&
	but bisect reset &&
	test_path_is_missing .but/BISECT_FIRST_PARENT
'

test_expect_success 'good merge bases when good and bad are siblings' '
	but bisect start "$B_HASH" "$A_HASH" > my_bisect_log.txt &&
	test_i18ngrep "merge base must be tested" my_bisect_log.txt &&
	but bisect good > my_bisect_log2.txt &&
	test_i18ngrep "merge base must be tested" my_bisect_log2.txt &&
	{
		{
			grep "$SIDE_HASH5" my_bisect_log.txt &&
			grep "$HASH5" my_bisect_log2.txt
		} || {
			grep "$SIDE_HASH5" my_bisect_log2.txt &&
			grep "$HASH5" my_bisect_log.txt
		}
	} &&
	but bisect reset
'

test_expect_success 'optimized merge base checks' '
	but bisect start "$HASH7" "$SIDE_HASH7" > my_bisect_log.txt &&
	test_i18ngrep "merge base must be tested" my_bisect_log.txt &&
	grep "$HASH4" my_bisect_log.txt &&
	but bisect good > my_bisect_log2.txt &&
	test -f ".but/BISECT_ANCESTORS_OK" &&
	test "$HASH6" = $(but rev-parse --verify HEAD) &&
	but bisect bad &&
	but bisect good "$A_HASH" > my_bisect_log4.txt &&
	test_i18ngrep "merge base must be tested" my_bisect_log4.txt &&
	test_path_is_missing ".but/BISECT_ANCESTORS_OK"
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
	but bisect reset &&
	but checkout -b parallel $HASH1 &&
	mkdir dir1 dir2 &&
	add_line_into_file "1(para): line 1 on parallel branch" dir1/file1 &&
	PARA_HASH1=$(but rev-parse --verify HEAD) &&
	add_line_into_file "2(para): line 2 on parallel branch" dir2/file2 &&
	PARA_HASH2=$(but rev-parse --verify HEAD) &&
	add_line_into_file "3(para): line 3 on parallel branch" dir2/file3 &&
	PARA_HASH3=$(but rev-parse --verify HEAD) &&
	but merge -m "merge HASH4 and PARA_HASH3" "$HASH4" &&
	PARA_HASH4=$(but rev-parse --verify HEAD) &&
	add_line_into_file "5(para): add line on parallel branch" dir1/file1 &&
	PARA_HASH5=$(but rev-parse --verify HEAD) &&
	add_line_into_file "6(para): add line on parallel branch" dir2/file2 &&
	PARA_HASH6=$(but rev-parse --verify HEAD) &&
	but merge -m "merge HASH7 and PARA_HASH6" "$HASH7" &&
	PARA_HASH7=$(but rev-parse --verify HEAD)
'

test_expect_success 'restricting bisection on one dir' '
	but bisect reset &&
	but bisect start HEAD $HASH1 -- dir1 &&
	para1=$(but rev-parse --verify HEAD) &&
	test "$para1" = "$PARA_HASH1" &&
	but bisect bad > my_bisect_log.txt &&
	grep "$PARA_HASH1 is the first bad cummit" my_bisect_log.txt
'

test_expect_success 'restricting bisection on one dir and a file' '
	but bisect reset &&
	but bisect start HEAD $HASH1 -- dir1 hello &&
	para4=$(but rev-parse --verify HEAD) &&
	test "$para4" = "$PARA_HASH4" &&
	but bisect bad &&
	hash3=$(but rev-parse --verify HEAD) &&
	test "$hash3" = "$HASH3" &&
	but bisect good &&
	hash4=$(but rev-parse --verify HEAD) &&
	test "$hash4" = "$HASH4" &&
	but bisect good &&
	para1=$(but rev-parse --verify HEAD) &&
	test "$para1" = "$PARA_HASH1" &&
	but bisect good > my_bisect_log.txt &&
	grep "$PARA_HASH4 is the first bad cummit" my_bisect_log.txt
'

test_expect_success 'skipping away from skipped cummit' '
	but bisect start $PARA_HASH7 $HASH1 &&
	para4=$(but rev-parse --verify HEAD) &&
	test "$para4" = "$PARA_HASH4" &&
        but bisect skip &&
	hash7=$(but rev-parse --verify HEAD) &&
	test "$hash7" = "$HASH7" &&
        but bisect skip &&
	para3=$(but rev-parse --verify HEAD) &&
	test "$para3" = "$PARA_HASH3"
'

test_expect_success 'erroring out when using bad path arguments' '
	test_must_fail but bisect start $PARA_HASH7 $HASH1 -- foobar 2> error.txt &&
	test_i18ngrep "bad path arguments" error.txt
'

test_expect_success 'test bisection on bare repo - --no-checkout specified' '
	but clone --bare . bare.nocheckout &&
	(
		cd bare.nocheckout &&
		but bisect start --no-checkout &&
		but bisect good $HASH1 &&
		but bisect bad $HASH4 &&
		but bisect run eval \
			"test \$(but rev-list BISECT_HEAD ^$HASH2 --max-count=1 | wc -l) = 0" \
			>../nocheckout.log
	) &&
	grep "$HASH3 is the first bad cummit" nocheckout.log
'


test_expect_success 'test bisection on bare repo - --no-checkout defaulted' '
	but clone --bare . bare.defaulted &&
	(
		cd bare.defaulted &&
		but bisect start &&
		but bisect good $HASH1 &&
		but bisect bad $HASH4 &&
		but bisect run eval \
			"test \$(but rev-list BISECT_HEAD ^$HASH2 --max-count=1 | wc -l) = 0" \
			>../defaulted.log
	) &&
	grep "$HASH3 is the first bad cummit" defaulted.log
'

#
# This creates a broken branch which cannot be checked out because
# the tree created has been deleted.
#
# H1-H2-H3-H4-H5-H6-H7  <--other
#            \
#             S5-S6'-S7'-S8'-S9  <--broken
#
# cummits marked with ' have a missing tree.
#
test_expect_success 'broken branch creation' '
	but bisect reset &&
	but checkout -b broken $HASH4 &&
	but tag BROKEN_HASH4 $HASH4 &&
	add_line_into_file "5(broken): first line on a broken branch" hello2 &&
	but tag BROKEN_HASH5 &&
	mkdir missing &&
	:> missing/MISSING &&
	but add missing/MISSING &&
	but cummit -m "6(broken): Added file that will be deleted" &&
	but tag BROKEN_HASH6 &&
	deleted=$(but rev-parse --verify HEAD:missing) &&
	add_line_into_file "7(broken): second line on a broken branch" hello2 &&
	but tag BROKEN_HASH7 &&
	add_line_into_file "8(broken): third line on a broken branch" hello2 &&
	but tag BROKEN_HASH8 &&
	but rm missing/MISSING &&
	but cummit -m "9(broken): Remove missing file" &&
	but tag BROKEN_HASH9 &&
	rm .but/objects/$(test_oid_to_path $deleted)
'

echo "" > expected.ok
cat > expected.missing-tree.default <<EOF
fatal: unable to read tree $deleted
EOF

test_expect_success 'bisect fails if tree is broken on start cummit' '
	but bisect reset &&
	test_must_fail but bisect start BROKEN_HASH7 BROKEN_HASH4 2>error.txt &&
	test_cmp expected.missing-tree.default error.txt
'

test_expect_success 'bisect fails if tree is broken on trial cummit' '
	but bisect reset &&
	test_must_fail but bisect start BROKEN_HASH9 BROKEN_HASH4 2>error.txt &&
	but reset --hard broken &&
	but checkout broken &&
	test_cmp expected.missing-tree.default error.txt
'

check_same()
{
	echo "Checking $1 is the same as $2" &&
	test_cmp_rev "$1" "$2"
}

test_expect_success 'bisect: --no-checkout - start cummit bad' '
	but bisect reset &&
	but bisect start BROKEN_HASH7 BROKEN_HASH4 --no-checkout &&
	check_same BROKEN_HASH6 BISECT_HEAD &&
	but bisect reset
'

test_expect_success 'bisect: --no-checkout - trial cummit bad' '
	but bisect reset &&
	but bisect start broken BROKEN_HASH4 --no-checkout &&
	check_same BROKEN_HASH6 BISECT_HEAD &&
	but bisect reset
'

test_expect_success 'bisect: --no-checkout - target before breakage' '
	but bisect reset &&
	but bisect start broken BROKEN_HASH4 --no-checkout &&
	check_same BROKEN_HASH6 BISECT_HEAD &&
	but bisect bad BISECT_HEAD &&
	check_same BROKEN_HASH5 BISECT_HEAD &&
	but bisect bad BISECT_HEAD &&
	check_same BROKEN_HASH5 bisect/bad &&
	but bisect reset
'

test_expect_success 'bisect: --no-checkout - target in breakage' '
	but bisect reset &&
	but bisect start broken BROKEN_HASH4 --no-checkout &&
	check_same BROKEN_HASH6 BISECT_HEAD &&
	but bisect bad BISECT_HEAD &&
	check_same BROKEN_HASH5 BISECT_HEAD &&
	test_must_fail but bisect good BISECT_HEAD &&
	check_same BROKEN_HASH6 bisect/bad &&
	but bisect reset
'

test_expect_success 'bisect: --no-checkout - target after breakage' '
	but bisect reset &&
	but bisect start broken BROKEN_HASH4 --no-checkout &&
	check_same BROKEN_HASH6 BISECT_HEAD &&
	but bisect good BISECT_HEAD &&
	check_same BROKEN_HASH8 BISECT_HEAD &&
	test_must_fail but bisect good BISECT_HEAD &&
	check_same BROKEN_HASH9 bisect/bad &&
	but bisect reset
'

test_expect_success 'bisect: demonstrate identification of damage boundary' "
	but bisect reset &&
	but checkout broken &&
	but bisect start broken main --no-checkout &&
	test_must_fail but bisect run \"\$SHELL_PATH\" -c '
		GOOD=\$(but for-each-ref \"--format=%(objectname)\" refs/bisect/good-*) &&
		but rev-list --objects BISECT_HEAD --not \$GOOD >tmp.\$\$ &&
		but pack-objects --stdout >/dev/null < tmp.\$\$
		rc=\$?
		rm -f tmp.\$\$
		test \$rc = 0' &&
	check_same BROKEN_HASH6 bisect/bad &&
	but bisect reset
"

cat > expected.bisect-log <<EOF
# bad: [$HASH4] Add <4: Ciao for now> into <hello>.
# good: [$HASH2] Add <2: A new day for but> into <hello>.
but bisect start '$HASH4' '$HASH2'
# good: [$HASH3] Add <3: Another new day for but> into <hello>.
but bisect good $HASH3
# first bad cummit: [$HASH4] Add <4: Ciao for now> into <hello>.
EOF

test_expect_success 'bisect log: successful result' '
	but bisect reset &&
	but bisect start $HASH4 $HASH2 &&
	but bisect good &&
	but bisect log >bisect-log.txt &&
	test_cmp expected.bisect-log bisect-log.txt &&
	but bisect reset
'

cat > expected.bisect-skip-log <<EOF
# bad: [$HASH4] Add <4: Ciao for now> into <hello>.
# good: [$HASH2] Add <2: A new day for but> into <hello>.
but bisect start '$HASH4' '$HASH2'
# skip: [$HASH3] Add <3: Another new day for but> into <hello>.
but bisect skip $HASH3
# only skipped cummits left to test
# possible first bad cummit: [$HASH4] Add <4: Ciao for now> into <hello>.
# possible first bad cummit: [$HASH3] Add <3: Another new day for but> into <hello>.
EOF

test_expect_success 'bisect log: only skip cummits left' '
	but bisect reset &&
	but bisect start $HASH4 $HASH2 &&
	test_must_fail but bisect skip &&
	but bisect log >bisect-skip-log.txt &&
	test_cmp expected.bisect-skip-log bisect-skip-log.txt &&
	but bisect reset
'

test_expect_success '"but bisect bad HEAD" behaves as "but bisect bad"' '
	but checkout parallel &&
	but bisect start HEAD $HASH1 &&
	but bisect good HEAD &&
	but bisect bad HEAD &&
	test "$HASH6" = $(but rev-parse --verify HEAD) &&
	but bisect reset
'

test_expect_success 'bisect starts with only one new' '
	but bisect reset &&
	but bisect start &&
	but bisect new $HASH4 &&
	but bisect next
'

test_expect_success 'bisect does not start with only one old' '
	but bisect reset &&
	but bisect start &&
	but bisect old $HASH1 &&
	test_must_fail but bisect next
'

test_expect_success 'bisect start with one new and old' '
	but bisect reset &&
	but bisect start &&
	but bisect old $HASH1 &&
	but bisect new $HASH4 &&
	but bisect new &&
	but bisect new >bisect_result &&
	grep "$HASH2 is the first new cummit" bisect_result &&
	but bisect log >log_to_replay.txt &&
	but bisect reset
'

test_expect_success 'bisect replay with old and new' '
	but bisect replay log_to_replay.txt >bisect_result &&
	grep "$HASH2 is the first new cummit" bisect_result &&
	but bisect reset
'

test_expect_success 'bisect replay with CRLF log' '
	append_cr <log_to_replay.txt >log_to_replay_crlf.txt &&
	but bisect replay log_to_replay_crlf.txt >bisect_result_crlf &&
	grep "$HASH2 is the first new cummit" bisect_result_crlf &&
	but bisect reset
'

test_expect_success 'bisect cannot mix old/new and good/bad' '
	but bisect start &&
	but bisect bad $HASH4 &&
	test_must_fail but bisect old $HASH1
'

test_expect_success 'bisect terms needs 0 or 1 argument' '
	but bisect reset &&
	test_must_fail but bisect terms only-one &&
	test_must_fail but bisect terms 1 2 &&
	test_must_fail but bisect terms 2>actual &&
	echo "error: no terms defined" >expected &&
	test_cmp expected actual
'

test_expect_success 'bisect terms shows good/bad after start' '
	but bisect reset &&
	but bisect start HEAD $HASH1 &&
	but bisect terms --term-good >actual &&
	echo good >expected &&
	test_cmp expected actual &&
	but bisect terms --term-bad >actual &&
	echo bad >expected &&
	test_cmp expected actual
'

test_expect_success 'bisect start with one term1 and term2' '
	but bisect reset &&
	but bisect start --term-old term2 --term-new term1 &&
	but bisect term2 $HASH1 &&
	but bisect term1 $HASH4 &&
	but bisect term1 &&
	but bisect term1 >bisect_result &&
	grep "$HASH2 is the first term1 cummit" bisect_result &&
	but bisect log >log_to_replay.txt &&
	but bisect reset
'

test_expect_success 'bisect replay with term1 and term2' '
	but bisect replay log_to_replay.txt >bisect_result &&
	grep "$HASH2 is the first term1 cummit" bisect_result &&
	but bisect reset
'

test_expect_success 'bisect start term1 term2' '
	but bisect reset &&
	but bisect start --term-new term1 --term-old term2 $HASH4 $HASH1 &&
	but bisect term1 &&
	but bisect term1 >bisect_result &&
	grep "$HASH2 is the first term1 cummit" bisect_result &&
	but bisect log >log_to_replay.txt &&
	but bisect reset
'

test_expect_success 'bisect cannot mix terms' '
	but bisect reset &&
	but bisect start --term-good term1 --term-bad term2 $HASH4 $HASH1 &&
	test_must_fail but bisect a &&
	test_must_fail but bisect b &&
	test_must_fail but bisect bad &&
	test_must_fail but bisect good &&
	test_must_fail but bisect new &&
	test_must_fail but bisect old
'

test_expect_success 'bisect terms rejects invalid terms' '
	but bisect reset &&
	test_must_fail but bisect start --term-good &&
	test_must_fail but bisect start --term-good invalid..term &&
	test_must_fail but bisect start --term-bad &&
	test_must_fail but bisect terms --term-bad invalid..term &&
	test_must_fail but bisect terms --term-good bad &&
	test_must_fail but bisect terms --term-good old &&
	test_must_fail but bisect terms --term-good skip &&
	test_must_fail but bisect terms --term-good reset &&
	test_path_is_missing .but/BISECT_TERMS
'

test_expect_success 'bisect start --term-* does store terms' '
	but bisect reset &&
	but bisect start --term-bad=one --term-good=two &&
	but bisect terms >actual &&
	cat <<-EOF >expected &&
	Your current terms are two for the old state
	and one for the new state.
	EOF
	test_cmp expected actual &&
	but bisect terms --term-bad >actual &&
	echo one >expected &&
	test_cmp expected actual &&
	but bisect terms --term-good >actual &&
	echo two >expected &&
	test_cmp expected actual
'

test_expect_success 'bisect start takes options and revs in any order' '
	but bisect reset &&
	but bisect start --term-good one $HASH4 \
		--term-good two --term-bad bad-term \
		$HASH1 --term-good three -- &&
	(but bisect terms --term-bad && but bisect terms --term-good) >actual &&
	printf "%s\n%s\n" bad-term three >expected &&
	test_cmp expected actual
'

# Bisect is started with --term-new and --term-old arguments,
# then skip. The HEAD should be changed.
test_expect_success 'bisect skip works with --term*' '
	but bisect reset &&
	but bisect start --term-new=fixed --term-old=unfixed HEAD $HASH1 &&
	hash_skipped_from=$(but rev-parse --verify HEAD) &&
	but bisect skip &&
	hash_skipped_to=$(but rev-parse --verify HEAD) &&
	test "$hash_skipped_from" != "$hash_skipped_to"
'

test_expect_success 'but bisect reset cleans bisection state properly' '
	but bisect reset &&
	but bisect start &&
	but bisect good $HASH1 &&
	but bisect bad $HASH4 &&
	but bisect reset &&
	test -z "$(but for-each-ref "refs/bisect/*")" &&
	test_path_is_missing ".but/BISECT_EXPECTED_REV" &&
	test_path_is_missing ".but/BISECT_ANCESTORS_OK" &&
	test_path_is_missing ".but/BISECT_LOG" &&
	test_path_is_missing ".but/BISECT_RUN" &&
	test_path_is_missing ".but/BISECT_TERMS" &&
	test_path_is_missing ".but/head-name" &&
	test_path_is_missing ".but/BISECT_HEAD" &&
	test_path_is_missing ".but/BISECT_START"
'

test_expect_success 'bisect handles annotated tags' '
	test_cummit cummit-one &&
	but tag -m foo tag-one &&
	test_cummit cummit-two &&
	but tag -m foo tag-two &&
	but bisect start &&
	but bisect good tag-one &&
	but bisect bad tag-two >output &&
	bad=$(but rev-parse --verify tag-two^{cummit}) &&
	grep "$bad is the first bad cummit" output
'

test_expect_success 'bisect run fails with exit code equals or greater than 128' '
	write_script test_script.sh <<-\EOF &&
	exit 128
	EOF
	test_must_fail but bisect run ./test_script.sh &&
	write_script test_script.sh <<-\EOF &&
	exit 255
	EOF
	test_must_fail but bisect run ./test_script.sh
'

test_expect_success 'bisect visualize with a filename with dash and space' '
	echo "My test line" >>"./-hello 2" &&
	but add -- "./-hello 2" &&
	but cummit --quiet -m "Add test line" -- "./-hello 2" &&
	but bisect visualize -p -- "-hello 2"
'

test_done
