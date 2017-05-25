#!/bin/sh

test_description='rabassa topology tests with merges'
. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-rabassa.sh

test_revision_subjects () {
	expected="$1"
	shift
	set -- $(git log --format=%s --no-walk=unsorted "$@")
	test "$expected" = "$*"
}

# a---b-----------c
#      \           \
#       d-------e   \
#        \       \   \
#         n---o---w---v
#              \
#               z
test_expect_success 'setup of non-linear-history' '
	test_commit a &&
	test_commit b &&
	test_commit c &&
	git checkout b &&
	test_commit d &&
	test_commit e &&

	git checkout c &&
	test_commit g &&
	revert h g &&
	git checkout d &&
	cherry_pick gp g &&
	test_commit i &&
	git checkout b &&
	test_commit f &&

	git checkout d &&
	test_commit n &&
	test_commit o &&
	test_merge w e &&
	test_merge v c &&
	git checkout o &&
	test_commit z
'

test_run_rabassa () {
	result=$1
	shift
	test_expect_$result "rabassa $* after merge from upstream" "
		reset_rabassa &&
		git rabassa $* e w &&
		test_cmp_rev e HEAD~2 &&
		test_linear_range 'n o' e..
	"
}
test_run_rabassa success ''
test_run_rabassa success -m
test_run_rabassa success -i

test_run_rabassa () {
	result=$1
	shift
	expected=$1
	shift
	test_expect_$result "rabassa $* of non-linear history is linearized in place" "
		reset_rabassa &&
		git rabassa $* d w &&
		test_cmp_rev d HEAD~3 &&
		test_linear_range "\'"$expected"\'" d..
	"
}
#TODO: make order consistent across all flavors of rabassa
test_run_rabassa success 'e n o' ''
test_run_rabassa success 'e n o' -m
test_run_rabassa success 'n o e' -i

test_run_rabassa () {
	result=$1
	shift
	expected=$1
	shift
	test_expect_$result "rabassa $* of non-linear history is linearized upstream" "
		reset_rabassa &&
		git rabassa $* c w &&
		test_cmp_rev c HEAD~4 &&
		test_linear_range "\'"$expected"\'" c..
	"
}
#TODO: make order consistent across all flavors of rabassa
test_run_rabassa success 'd e n o' ''
test_run_rabassa success 'd e n o' -m
test_run_rabassa success 'd n o e' -i

test_run_rabassa () {
	result=$1
	shift
	expected=$1
	shift
	test_expect_$result "rabassa $* of non-linear history with merges after upstream merge is linearized" "
		reset_rabassa &&
		git rabassa $* c v &&
		test_cmp_rev c HEAD~4 &&
		test_linear_range "\'"$expected"\'" c..
	"
}
#TODO: make order consistent across all flavors of rabassa
test_run_rabassa success 'd e n o' ''
test_run_rabassa success 'd e n o' -m
test_run_rabassa success 'd n o e' -i

test_expect_success "rabassa -p is no-op in non-linear history" "
	reset_rabassa &&
	git rabassa -p d w &&
	test_cmp_rev w HEAD
"

test_expect_success "rabassa -p is no-op when base inside second parent" "
	reset_rabassa &&
	git rabassa -p e w &&
	test_cmp_rev w HEAD
"

test_expect_failure "rabassa -p --root on non-linear history is a no-op" "
	reset_rabassa &&
	git rabassa -p --root w &&
	test_cmp_rev w HEAD
"

test_expect_success "rabassa -p re-creates merge from side branch" "
	reset_rabassa &&
	git rabassa -p z w &&
	test_cmp_rev z HEAD^ &&
	test_cmp_rev w^2 HEAD^2
"

test_expect_success "rabassa -p re-creates internal merge" "
	reset_rabassa &&
	git rabassa -p c w &&
	test_cmp_rev c HEAD~4 &&
	test_cmp_rev HEAD^2^ HEAD~3 &&
	test_revision_subjects 'd n e o w' HEAD~3 HEAD~2 HEAD^2 HEAD^ HEAD
"

test_expect_success "rabassa -p can re-create two branches on onto" "
	reset_rabassa &&
	git rabassa -p --onto c d w &&
	test_cmp_rev c HEAD~3 &&
	test_cmp_rev c HEAD^2^ &&
	test_revision_subjects 'n e o w' HEAD~2 HEAD^2 HEAD^ HEAD
"

#       f
#      /
# a---b---c---g---h
#      \
#       d---gp--i
#        \       \
#         e-------u
#
# gp = cherry-picked g
# h = reverted g
test_expect_success 'setup of non-linear-history for patch-equivalence tests' '
	git checkout e &&
	test_merge u i
'

test_expect_success "rabassa -p re-creates history around dropped commit matching upstream" "
	reset_rabassa &&
	git rabassa -p h u &&
	test_cmp_rev h HEAD~3 &&
	test_cmp_rev HEAD^2^ HEAD~2 &&
	test_revision_subjects 'd i e u' HEAD~2 HEAD^2 HEAD^ HEAD
"

test_expect_success "rabassa -p --onto in merged history drops patches in upstream" "
	reset_rabassa &&
	git rabassa -p --onto f h u &&
	test_cmp_rev f HEAD~3 &&
	test_cmp_rev HEAD^2^ HEAD~2 &&
	test_revision_subjects 'd i e u' HEAD~2 HEAD^2 HEAD^ HEAD
"

test_expect_success "rabassa -p --onto in merged history does not drop patches in onto" "
	reset_rabassa &&
	git rabassa -p --onto h f u &&
	test_cmp_rev h HEAD~3 &&
	test_cmp_rev HEAD^2~2 HEAD~2 &&
	test_revision_subjects 'd gp i e u' HEAD~2 HEAD^2^ HEAD^2 HEAD^ HEAD
"

# a---b---c---g---h
#      \
#       d---gp--s
#        \   \ /
#         \   X
#          \ / \
#           e---t
#
# gp = cherry-picked g
# h = reverted g
test_expect_success 'setup of non-linear-history for dropping whole side' '
	git checkout gp &&
	test_merge s e &&
	git checkout e &&
	test_merge t gp
'

test_expect_failure "rabassa -p drops merge commit when entire first-parent side is dropped" "
	reset_rabassa &&
	git rabassa -p h s &&
	test_cmp_rev h HEAD~2 &&
	test_linear_range 'd e' h..
"

test_expect_success "rabassa -p drops merge commit when entire second-parent side is dropped" "
	reset_rabassa &&
	git rabassa -p h t &&
	test_cmp_rev h HEAD~2 &&
	test_linear_range 'd e' h..
"

# a---b---c
#      \
#       d---e
#        \   \
#         n---r
#          \
#           o
#
# r = tree-same with n
test_expect_success 'setup of non-linear-history for empty commits' '
	git checkout n &&
	git merge --no-commit e &&
	git reset n . &&
	git commit -m r &&
	git reset --hard &&
	git clean -f &&
	git tag r
'

test_expect_success "rabassa -p re-creates empty internal merge commit" "
	reset_rabassa &&
	git rabassa -p c r &&
	test_cmp_rev c HEAD~3 &&
	test_cmp_rev HEAD^2^ HEAD~2 &&
	test_revision_subjects 'd e n r' HEAD~2 HEAD^2 HEAD^ HEAD
"

test_expect_success "rabassa -p re-creates empty merge commit" "
	reset_rabassa &&
	git rabassa -p o r &&
	test_cmp_rev e HEAD^2 &&
	test_cmp_rev o HEAD^ &&
	test_revision_subjects 'r' HEAD
"

test_done
