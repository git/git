#!/bin/sh
#
# Copyright (c) 2006 Junio C Hamano
#

test_description='i18n settings and format-patch | am pipe'

. ./test-lib.sh

check_encoding () {
	# Make sure characters are not corrupted
	cnt="$1" header="$2" i=1 j=0 bad=0
	while test "$i" -le $cnt
	do
		git format-patch --encoding=UTF-8 --stdout HEAD~$i..HEAD~$j |
		grep "^From: =?UTF-8?q?=C3=81=C3=A9=C3=AD=20=C3=B3=C3=BA?=" &&
		git cat-file commit HEAD~$j |
		case "$header" in
		8859)
			grep "^encoding ISO8859-1" ;;
		*)
			grep "^encoding ISO8859-1"; test "$?" != 0 ;;
		esac || {
			bad=1
			break
		}
		j=$i
		i=$(($i+1))
	done
	(exit $bad)
}

test_expect_success setup '
	git config i18n.commitencoding UTF-8 &&

	# use UTF-8 in author and committer name to match the
	# i18n.commitencoding settings
	. "$TEST_DIRECTORY"/t3901-utf8.txt &&

	test_tick &&
	echo "$GIT_AUTHOR_NAME" >mine &&
	git add mine &&
	git commit -s -m "Initial commit" &&

	test_tick &&
	echo Hello world >mine &&
	git add mine &&
	git commit -s -m "Second on main" &&

	# the first commit on the side branch is UTF-8
	test_tick &&
	git checkout -b side master^ &&
	echo Another file >yours &&
	git add yours &&
	git commit -s -m "Second on side" &&

	# the second one on the side branch is ISO-8859-1
	git config i18n.commitencoding ISO8859-1 &&
	# use author and committer name in ISO-8859-1 to match it.
	. "$TEST_DIRECTORY"/t3901-8859-1.txt &&
	test_tick &&
	echo Yet another >theirs &&
	git add theirs &&
	git commit -s -m "Third on side" &&

	# Back to default
	git config i18n.commitencoding UTF-8
'

test_expect_success 'format-patch output (ISO-8859-1)' '
	git config i18n.logoutputencoding ISO8859-1 &&

	git format-patch --stdout master..HEAD^ >out-l1 &&
	git format-patch --stdout HEAD^ >out-l2 &&
	grep "^Content-Type: text/plain; charset=ISO8859-1" out-l1 &&
	grep "^From: =?ISO8859-1?q?=C1=E9=ED=20=F3=FA?=" out-l1 &&
	grep "^Content-Type: text/plain; charset=ISO8859-1" out-l2 &&
	grep "^From: =?ISO8859-1?q?=C1=E9=ED=20=F3=FA?=" out-l2
'

test_expect_success 'format-patch output (UTF-8)' '
	git config i18n.logoutputencoding UTF-8 &&

	git format-patch --stdout master..HEAD^ >out-u1 &&
	git format-patch --stdout HEAD^ >out-u2 &&
	grep "^Content-Type: text/plain; charset=UTF-8" out-u1 &&
	grep "^From: =?UTF-8?q?=C3=81=C3=A9=C3=AD=20=C3=B3=C3=BA?=" out-u1 &&
	grep "^Content-Type: text/plain; charset=UTF-8" out-u2 &&
	grep "^From: =?UTF-8?q?=C3=81=C3=A9=C3=AD=20=C3=B3=C3=BA?=" out-u2
'

test_expect_success 'rebase (U/U)' '
	# We want the result of rebase in UTF-8
	git config i18n.commitencoding UTF-8 &&

	# The test is about logoutputencoding not affecting the
	# final outcome -- it is used internally to generate the
	# patch and the log.

	git config i18n.logoutputencoding UTF-8 &&

	# The result will be committed by GIT_COMMITTER_NAME --
	# we want UTF-8 encoded name.
	. "$TEST_DIRECTORY"/t3901-utf8.txt &&
	git checkout -b test &&
	git rebase master &&

	check_encoding 2
'

test_expect_success 'rebase (U/L)' '
	git config i18n.commitencoding UTF-8 &&
	git config i18n.logoutputencoding ISO8859-1 &&
	. "$TEST_DIRECTORY"/t3901-utf8.txt &&

	git reset --hard side &&
	git rebase master &&

	check_encoding 2
'

test_expect_success 'rebase (L/L)' '
	# In this test we want ISO-8859-1 encoded commits as the result
	git config i18n.commitencoding ISO8859-1 &&
	git config i18n.logoutputencoding ISO8859-1 &&
	. "$TEST_DIRECTORY"/t3901-8859-1.txt &&

	git reset --hard side &&
	git rebase master &&

	check_encoding 2 8859
'

test_expect_success 'rebase (L/U)' '
	# This is pathological -- use UTF-8 as intermediate form
	# to get ISO-8859-1 results.
	git config i18n.commitencoding ISO8859-1 &&
	git config i18n.logoutputencoding UTF-8 &&
	. "$TEST_DIRECTORY"/t3901-8859-1.txt &&

	git reset --hard side &&
	git rebase master &&

	check_encoding 2 8859
'

test_expect_success 'cherry-pick(U/U)' '
	# Both the commitencoding and logoutputencoding is set to UTF-8.

	git config i18n.commitencoding UTF-8 &&
	git config i18n.logoutputencoding UTF-8 &&
	. "$TEST_DIRECTORY"/t3901-utf8.txt &&

	git reset --hard master &&
	git cherry-pick side^ &&
	git cherry-pick side &&
	git revert HEAD &&

	check_encoding 3
'

test_expect_success 'cherry-pick(L/L)' '
	# Both the commitencoding and logoutputencoding is set to ISO-8859-1

	git config i18n.commitencoding ISO8859-1 &&
	git config i18n.logoutputencoding ISO8859-1 &&
	. "$TEST_DIRECTORY"/t3901-8859-1.txt &&

	git reset --hard master &&
	git cherry-pick side^ &&
	git cherry-pick side &&
	git revert HEAD &&

	check_encoding 3 8859
'

test_expect_success 'cherry-pick(U/L)' '
	# Commitencoding is set to UTF-8 but logoutputencoding is ISO-8859-1

	git config i18n.commitencoding UTF-8 &&
	git config i18n.logoutputencoding ISO8859-1 &&
	. "$TEST_DIRECTORY"/t3901-utf8.txt &&

	git reset --hard master &&
	git cherry-pick side^ &&
	git cherry-pick side &&
	git revert HEAD &&

	check_encoding 3
'

test_expect_success 'cherry-pick(L/U)' '
	# Again, the commitencoding is set to ISO-8859-1 but
	# logoutputencoding is set to UTF-8.

	git config i18n.commitencoding ISO8859-1 &&
	git config i18n.logoutputencoding UTF-8 &&
	. "$TEST_DIRECTORY"/t3901-8859-1.txt &&

	git reset --hard master &&
	git cherry-pick side^ &&
	git cherry-pick side &&
	git revert HEAD &&

	check_encoding 3 8859
'

test_expect_success 'rebase --merge (U/U)' '
	git config i18n.commitencoding UTF-8 &&
	git config i18n.logoutputencoding UTF-8 &&
	. "$TEST_DIRECTORY"/t3901-utf8.txt &&

	git reset --hard side &&
	git rebase --merge master &&

	check_encoding 2
'

test_expect_success 'rebase --merge (U/L)' '
	git config i18n.commitencoding UTF-8 &&
	git config i18n.logoutputencoding ISO8859-1 &&
	. "$TEST_DIRECTORY"/t3901-utf8.txt &&

	git reset --hard side &&
	git rebase --merge master &&

	check_encoding 2
'

test_expect_success 'rebase --merge (L/L)' '
	# In this test we want ISO-8859-1 encoded commits as the result
	git config i18n.commitencoding ISO8859-1 &&
	git config i18n.logoutputencoding ISO8859-1 &&
	. "$TEST_DIRECTORY"/t3901-8859-1.txt &&

	git reset --hard side &&
	git rebase --merge master &&

	check_encoding 2 8859
'

test_expect_success 'rebase --merge (L/U)' '
	# This is pathological -- use UTF-8 as intermediate form
	# to get ISO-8859-1 results.
	git config i18n.commitencoding ISO8859-1 &&
	git config i18n.logoutputencoding UTF-8 &&
	. "$TEST_DIRECTORY"/t3901-8859-1.txt &&

	git reset --hard side &&
	git rebase --merge master &&

	check_encoding 2 8859
'

test_done
