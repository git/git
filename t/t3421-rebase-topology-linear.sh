#!/bin/sh

test_description='basic rebase topology tests'
. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-rebase.sh

# a---b---c
#      \
#       d---e
test_expect_success 'setup' '
	test_commit a &&
	test_commit b &&
	test_commit c &&
	git checkout b &&
	test_commit d &&
	test_commit e
'

test_run_rebase () {
	result=$1
	shift
	test_expect_$result "simple rebase $*" "
		reset_rebase &&
		git rebase $* c e &&
		test_cmp_rev c HEAD~2 &&
		test_linear_range 'd e' c..
	"
}
test_run_rebase success ''
test_run_rebase success -m
test_run_rebase success -i
test_have_prereq !REBASE_P || test_run_rebase success -p

test_run_rebase () {
	result=$1
	shift
	test_expect_$result "rebase $* is no-op if upstream is an ancestor" "
		reset_rebase &&
		git rebase $* b e &&
		test_cmp_rev e HEAD
	"
}
test_run_rebase success ''
test_run_rebase success -m
test_run_rebase success -i
test_have_prereq !REBASE_P || test_run_rebase success -p

test_run_rebase () {
	result=$1
	shift
	test_expect_$result "rebase $* -f rewrites even if upstream is an ancestor" "
		reset_rebase &&
		git rebase $* -f b e &&
		! test_cmp_rev e HEAD &&
		test_cmp_rev b HEAD~2 &&
		test_linear_range 'd e' b..
	"
}
test_run_rebase success ''
test_run_rebase success -m
test_run_rebase success -i
test_have_prereq !REBASE_P || test_run_rebase failure -p

test_run_rebase () {
	result=$1
	shift
	test_expect_$result "rebase $* fast-forwards from ancestor of upstream" "
		reset_rebase &&
		git rebase $* e b &&
		test_cmp_rev e HEAD
	"
}
test_run_rebase success ''
test_run_rebase success -m
test_run_rebase success -i
test_have_prereq !REBASE_P || test_run_rebase success -p

#       f
#      /
# a---b---c---g---h
#      \
#       d---gp--i
#
# gp = cherry-picked g
# h = reverted g
#
# Reverted patches are there for tests to be able to check if a commit
# that introduced the same change as another commit is
# dropped. Without reverted commits, we could get false positives
# because applying the patch succeeds, but simply results in no
# changes.
test_expect_success 'setup of linear history for range selection tests' '
	git checkout c &&
	test_commit g &&
	revert h g &&
	git checkout d &&
	cherry_pick gp g &&
	test_commit i &&
	git checkout b &&
	test_commit f
'

test_run_rebase () {
	result=$1
	shift
	test_expect_$result "rebase $* drops patches in upstream" "
		reset_rebase &&
		git rebase $* h i &&
		test_cmp_rev h HEAD~2 &&
		test_linear_range 'd i' h..
	"
}
test_run_rebase success ''
test_run_rebase failure -m
test_run_rebase success -i
test_have_prereq !REBASE_P || test_run_rebase success -p

test_run_rebase () {
	result=$1
	shift
	test_expect_$result "rebase $* can drop last patch if in upstream" "
		reset_rebase &&
		git rebase $* h gp &&
		test_cmp_rev h HEAD^ &&
		test_linear_range 'd' h..
	"
}
test_run_rebase success ''
test_run_rebase failure -m
test_run_rebase success -i
test_have_prereq !REBASE_P || test_run_rebase success -p

test_run_rebase () {
	result=$1
	shift
	test_expect_$result "rebase $* --onto drops patches in upstream" "
		reset_rebase &&
		git rebase $* --onto f h i &&
		test_cmp_rev f HEAD~2 &&
		test_linear_range 'd i' f..
	"
}
test_run_rebase success ''
test_run_rebase failure -m
test_run_rebase success -i
test_have_prereq !REBASE_P || test_run_rebase success -p

test_run_rebase () {
	result=$1
	shift
	test_expect_$result "rebase $* --onto does not drop patches in onto" "
		reset_rebase &&
		git rebase $* --onto h f i &&
		test_cmp_rev h HEAD~3 &&
		test_linear_range 'd gp i' h..
	"
}
test_run_rebase success ''
test_run_rebase success -m
test_run_rebase success -i
test_have_prereq !REBASE_P || test_run_rebase success -p

# a---b---c---j!
#      \
#       d---k!--l
#
# ! = empty
test_expect_success 'setup of linear history for empty commit tests' '
	git checkout c &&
	make_empty j &&
	git checkout d &&
	make_empty k &&
	test_commit l
'

test_run_rebase () {
	result=$1
	shift
	test_expect_$result "rebase $* drops empty commit" "
		reset_rebase &&
		git rebase $* c l &&
		test_cmp_rev c HEAD~2 &&
		test_linear_range 'd l' c..
	"
}
test_run_rebase success ''
test_run_rebase success -m
test_run_rebase success -i
test_have_prereq !REBASE_P || test_run_rebase success -p

test_run_rebase () {
	result=$1
	shift
	test_expect_$result "rebase $* --keep-empty" "
		reset_rebase &&
		git rebase $* --keep-empty c l &&
		test_cmp_rev c HEAD~3 &&
		test_linear_range 'd k l' c..
	"
}
test_run_rebase success ''
test_run_rebase success -m
test_run_rebase success -i
test_have_prereq !REBASE_P || test_run_rebase failure -p

test_run_rebase () {
	result=$1
	shift
	test_expect_$result "rebase $* --keep-empty keeps empty even if already in upstream" "
		reset_rebase &&
		git rebase $* --keep-empty j l &&
		test_cmp_rev j HEAD~3 &&
		test_linear_range 'd k l' j..
	"
}
test_run_rebase success ''
test_run_rebase success -m
test_run_rebase success -i
test_have_prereq !REBASE_P || test_run_rebase failure -p
test_run_rebase success --rebase-merges

#       m
#      /
# a---b---c---g
#
# x---y---bp
#
# bp = cherry-picked b
# m = reverted b
#
# Reverted patches are there for tests to be able to check if a commit
# that introduced the same change as another commit is
# dropped. Without reverted commits, we could get false positives
# because applying the patch succeeds, but simply results in no
# changes.
test_expect_success 'setup of linear history for test involving root' '
	git checkout b &&
	revert m b &&
	git checkout --orphan disjoint &&
	git rm -rf . &&
	test_commit x &&
	test_commit y &&
	cherry_pick bp b
'

test_run_rebase () {
	result=$1
	shift
	test_expect_$result "rebase $* --onto --root" "
		reset_rebase &&
		git rebase $* --onto c --root y &&
		test_cmp_rev c HEAD~2 &&
		test_linear_range 'x y' c..
	"
}
test_run_rebase success ''
test_run_rebase success -m
test_run_rebase success -i
test_have_prereq !REBASE_P || test_run_rebase success -p

test_run_rebase () {
	result=$1
	shift
	test_expect_$result "rebase $* without --onto --root with disjoint history" "
		reset_rebase &&
		git rebase $* c y &&
		test_cmp_rev c HEAD~2 &&
		test_linear_range 'x y' c..
	"
}
test_run_rebase success ''
test_run_rebase success -m
test_run_rebase success -i
test_have_prereq !REBASE_P || test_run_rebase failure -p

test_run_rebase () {
	result=$1
	shift
	test_expect_$result "rebase $* --onto --root drops patch in onto" "
		reset_rebase &&
		git rebase $* --onto m --root bp &&
		test_cmp_rev m HEAD~2 &&
		test_linear_range 'x y' m..
	"
}
test_run_rebase success ''
test_run_rebase failure -m
test_run_rebase success -i
test_have_prereq !REBASE_P || test_run_rebase success -p

test_run_rebase () {
	result=$1
	shift
	test_expect_$result "rebase $* --onto --root with merge-base does not go to root" "
		reset_rebase &&
		git rebase $* --onto m --root g &&
		test_cmp_rev m HEAD~2 &&
		test_linear_range 'c g' m..
	"
}

test_run_rebase success ''
test_run_rebase success -m
test_run_rebase success -i
test_have_prereq !REBASE_P || test_run_rebase failure -p

test_run_rebase () {
	result=$1
	shift
	test_expect_$result "rebase $* without --onto --root with disjoint history drops patch in onto" "
		reset_rebase &&
		git rebase $* m bp &&
		test_cmp_rev m HEAD~2 &&
		test_linear_range 'x y' m..
	"
}
test_run_rebase success ''
test_run_rebase failure -m
test_run_rebase success -i
test_have_prereq !REBASE_P || test_run_rebase failure -p

test_run_rebase () {
	result=$1
	shift
	test_expect_$result "rebase $* --root on linear history is a no-op" "
		reset_rebase &&
		git rebase $* --root c &&
		test_cmp_rev c HEAD
	"
}
test_run_rebase success ''
test_run_rebase success -m
test_run_rebase success -i
test_have_prereq !REBASE_P || test_run_rebase failure -p

test_run_rebase () {
	result=$1
	shift
	test_expect_$result "rebase $* -f --root on linear history causes re-write" "
		reset_rebase &&
		git rebase $* -f --root c &&
		! test_cmp_rev a HEAD~2 &&
		test_linear_range 'a b c' HEAD
	"
}
test_run_rebase success ''
test_run_rebase success -m
test_run_rebase success -i
test_have_prereq !REBASE_P || test_run_rebase success -p

test_done
