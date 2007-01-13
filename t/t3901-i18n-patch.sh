#!/bin/sh
#
# Copyright (c) 2006 Junio C Hamano
#

test_description='i18n settings and format-patch | am pipe'

. ./test-lib.sh

test_expect_success setup '
	git-repo-config i18n.commitencoding UTF-8 &&

	# use UTF-8 in author and committer name to match the
	# i18n.commitencoding settings
	. ../t3901-utf8.txt &&

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
	git-repo-config i18n.commitencoding ISO-8859-1 &&
	# use author and committer name in ISO-8859-1 to match it.
	. ../t3901-8859-1.txt &&
	test_tick &&
	echo Yet another >theirs &&
	git add theirs &&
	git commit -s -m "Third on side" &&

	# Back to default
	git-repo-config i18n.commitencoding UTF-8
'

test_expect_success 'format-patch output (ISO-8859-1)' '
	git-repo-config i18n.logoutputencoding ISO-8859-1 &&

	git format-patch --stdout master..HEAD^ >out-l1 &&
	git format-patch --stdout HEAD^ >out-l2 &&
	grep "^Content-Type: text/plain; charset=ISO-8859-1" out-l1 &&
	grep "^From: =?ISO-8859-1?q?=C1=E9=ED_=F3=FA?=" out-l1 &&
	grep "^Content-Type: text/plain; charset=ISO-8859-1" out-l2 &&
	grep "^From: =?ISO-8859-1?q?=C1=E9=ED_=F3=FA?=" out-l2
'

test_expect_success 'format-patch output (UTF-8)' '
	git repo-config i18n.logoutputencoding UTF-8 &&

	git format-patch --stdout master..HEAD^ >out-u1 &&
	git format-patch --stdout HEAD^ >out-u2 &&
	grep "^Content-Type: text/plain; charset=UTF-8" out-u1 &&
	grep "^From: =?UTF-8?q?=C3=81=C3=A9=C3=AD_=C3=B3=C3=BA?=" out-u1 &&
	grep "^Content-Type: text/plain; charset=UTF-8" out-u2 &&
	grep "^From: =?UTF-8?q?=C3=81=C3=A9=C3=AD_=C3=B3=C3=BA?=" out-u2
'

test_expect_success 'rebase (UTF-8)' '
	# We want the result of rebase in UTF-8
	git-repo-config i18n.commitencoding UTF-8 &&

	# The test is about logoutputencoding not affecting the
	# final outcome -- it is used internally to generate the
	# patch and the log.

	git repo-config i18n.logoutputencoding UTF-8 &&

	# The result will be committed by GIT_COMMITTER_NAME --
	# we want UTF-8 encoded name.
	. ../t3901-utf8.txt &&
	git checkout -b test &&
	git-rebase master &&

	# Check the results.
	git format-patch --stdout HEAD~2..HEAD^ >out-r1 &&
	git format-patch --stdout HEAD^ >out-r2 &&
	grep "^From: =?UTF-8?q?=C3=81=C3=A9=C3=AD_=C3=B3=C3=BA?=" out-r1 &&
	grep "^From: =?UTF-8?q?=C3=81=C3=A9=C3=AD_=C3=B3=C3=BA?=" out-r2

	! git-cat-file commit HEAD | grep "^encoding ISO-8859-1" &&
	! git-cat-file commit HEAD^ | grep "^encoding ISO-8859-1"
'

test_expect_success 'rebase (ISO-8859-1)' '
	git-repo-config i18n.commitencoding UTF-8 &&
	git repo-config i18n.logoutputencoding ISO-8859-1 &&
	. ../t3901-utf8.txt &&

	git reset --hard side &&
	git-rebase master &&

	git repo-config i18n.logoutputencoding UTF-8 &&
	git format-patch --stdout HEAD~2..HEAD^ >out-r1 &&
	git format-patch --stdout HEAD^ >out-r2 &&
	grep "^From: =?UTF-8?q?=C3=81=C3=A9=C3=AD_=C3=B3=C3=BA?=" out-r1 &&
	grep "^From: =?UTF-8?q?=C3=81=C3=A9=C3=AD_=C3=B3=C3=BA?=" out-r2 &&

	! git-cat-file commit HEAD | grep "^encoding ISO-8859-1" &&
	! git-cat-file commit HEAD^ | grep "^encoding ISO-8859-1"
'

test_expect_success 'rebase (ISO-8859-1)' '
	# In this test we want ISO-8859-1 encoded commits as the result
	git-repo-config i18n.commitencoding ISO-8859-1 &&
	git repo-config i18n.logoutputencoding ISO-8859-1 &&
	. ../t3901-8859-1.txt &&

	git reset --hard side &&
	git-rebase master &&

	# Make sure characters are not corrupted.
	git repo-config i18n.logoutputencoding UTF-8 &&
	git format-patch --stdout HEAD~2..HEAD^ >out-r1 &&
	git format-patch --stdout HEAD^ >out-r2 &&
	grep "^From: =?UTF-8?q?=C3=81=C3=A9=C3=AD_=C3=B3=C3=BA?=" out-r1 &&
	grep "^From: =?UTF-8?q?=C3=81=C3=A9=C3=AD_=C3=B3=C3=BA?=" out-r2 &&

	git-cat-file commit HEAD | grep "^encoding ISO-8859-1" &&
	git-cat-file commit HEAD^ | grep "^encoding ISO-8859-1"
'

test_expect_success 'rebase (UTF-8)' '
	# This is pathological -- use UTF-8 as intermediate form
	# to get ISO-8859-1 results.
	git-repo-config i18n.commitencoding ISO-8859-1 &&
	git repo-config i18n.logoutputencoding UTF-8 &&
	. ../t3901-8859-1.txt &&

	git reset --hard side &&
	git-rebase master &&

	# Make sure characters are not corrupted.
	git repo-config i18n.logoutputencoding UTF-8 &&
	git format-patch --stdout HEAD~2..HEAD^ >out-r1 &&
	git format-patch --stdout HEAD^ >out-r2 &&
	grep "^From: =?UTF-8?q?=C3=81=C3=A9=C3=AD_=C3=B3=C3=BA?=" out-r1 &&
	grep "^From: =?UTF-8?q?=C3=81=C3=A9=C3=AD_=C3=B3=C3=BA?=" out-r2 &&

	git-cat-file commit HEAD | grep "^encoding ISO-8859-1" &&
	git-cat-file commit HEAD^ | grep "^encoding ISO-8859-1"
'

test_done
