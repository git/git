#!/bin/sh

test_description='test git rev-list --cherry-pick -- file'

. ./test-lib.sh

# A---B
#  \
#   \
#    C
#
# B changes a file foo.c, adding a line of text.  C changes foo.c as
# well as bar.c, but the change in foo.c was identical to change B.

test_expect_success setup '
	echo Hallo > foo &&
	git add foo &&
	test_tick &&
	git commit -m "A" &&
	git tag A &&
	git checkout -b branch &&
	echo Bello > foo &&
	echo Cello > bar &&
	git add foo bar &&
	test_tick &&
	git commit -m "C" &&
	git tag C &&
	git checkout master &&
	git checkout branch foo &&
	test_tick &&
	git commit -m "B" &&
	git tag B
'

test_expect_success '--cherry-pick foo comes up empty' '
	test -z "$(git rev-list --left-right --cherry-pick B...C -- foo)"
'

test_expect_success '--cherry-pick bar does not come up empty' '
	! test -z "$(git rev-list --left-right --cherry-pick B...C -- bar)"
'

test_done
