#!/bin/sh

test_description='test cherry-pick and revert with renames

  --
   + rename2: renames oops to opos
  +  rename1: renames oops to spoo
  +  added:   adds extra line to oops
  ++ initial: has lines in oops

'

. ./test-lib.sh

test_expect_success setup '

	for l in a b c d e f g h i j k l m n o
	do
		echo $l$l$l$l$l$l$l$l$l
	done >oops &&

	test_tick &&
	git add oops &&
	git commit -m initial &&
	git tag initial &&

	test_tick &&
	echo "Add extra line at the end" >>oops &&
	git commit -a -m added &&
	git tag added &&

	test_tick &&
	git mv oops spoo &&
	git commit -m rename1 &&
	git tag rename1 &&

	test_tick &&
	git checkout -b side initial &&
	git mv oops opos &&
	git commit -m rename2 &&
	git tag rename2
'

test_expect_success 'cherry-pick after renaming branch' '

	git checkout rename2 &&
	EDITOR=: VISUAL=: git cherry-pick added &&
	test -f opos &&
	grep "Add extra line at the end" opos

'

test_expect_success 'revert after renaming branch' '

	git checkout rename1 &&
	EDITOR=: VISUAL=: git revert added &&
	test -f spoo &&
	! grep "Add extra line at the end" spoo

'

test_done
