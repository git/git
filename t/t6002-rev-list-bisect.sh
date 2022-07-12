#!/bin/sh
#
# Copyright (c) 2005 Jon Seymour
#
test_description='Tests git rev-list --bisect functionality'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-t6000.sh # t6xxx specific functions

# usage: test_bisection max-diff bisect-option head ^prune...
#
# e.g. test_bisection 1 --bisect l1 ^l0
#
test_bisection_diff()
{
	_max_diff=$1
	_bisect_option=$2
	shift 2
	_bisection=$(git rev-list $_bisect_option "$@")
	_list_size=$(git rev-list "$@" | wc -l)
        _head=$1
	shift 1
	_bisection_size=$(git rev-list $_bisection "$@" | wc -l)
	[ -n "$_list_size" -a -n "$_bisection_size" ] ||
	error "test_bisection_diff failed"

	# Test if bisection size is close to half of list size within
	# tolerance.
	#
	_bisect_err=$(expr $_list_size - $_bisection_size \* 2)
	test "$_bisect_err" -lt 0 && _bisect_err=$(expr 0 - $_bisect_err)
	_bisect_err=$(expr $_bisect_err / 2) ; # floor

	test_expect_success \
	"bisection diff $_bisect_option $_head $* <= $_max_diff" \
	'test $_bisect_err -le $_max_diff'
}

date >path0
git update-index --add path0
save_tag tree git write-tree
on_committer_date "00:00" hide_error save_tag root unique_commit root tree
on_committer_date "00:01" save_tag l0 unique_commit l0 tree -p root
on_committer_date "00:02" save_tag l1 unique_commit l1 tree -p l0
on_committer_date "00:03" save_tag l2 unique_commit l2 tree -p l1
on_committer_date "00:04" save_tag a0 unique_commit a0 tree -p l2
on_committer_date "00:05" save_tag a1 unique_commit a1 tree -p a0
on_committer_date "00:06" save_tag b1 unique_commit b1 tree -p a0
on_committer_date "00:07" save_tag c1 unique_commit c1 tree -p b1
on_committer_date "00:08" save_tag b2 unique_commit b2 tree -p b1
on_committer_date "00:09" save_tag b3 unique_commit b2 tree -p b2
on_committer_date "00:10" save_tag c2 unique_commit c2 tree -p c1 -p b2
on_committer_date "00:11" save_tag c3 unique_commit c3 tree -p c2
on_committer_date "00:12" save_tag a2 unique_commit a2 tree -p a1
on_committer_date "00:13" save_tag a3 unique_commit a3 tree -p a2
on_committer_date "00:14" save_tag b4 unique_commit b4 tree -p b3 -p a3
on_committer_date "00:15" save_tag a4 unique_commit a4 tree -p a3 -p b4 -p c3
on_committer_date "00:16" save_tag l3 unique_commit l3 tree -p a4
on_committer_date "00:17" save_tag l4 unique_commit l4 tree -p l3
on_committer_date "00:18" save_tag l5 unique_commit l5 tree -p l4
git update-ref HEAD $(tag l5)


#     E
#    / \
#   e1  |
#   |   |
#   e2  |
#   |   |
#   e3  |
#   |   |
#   e4  |
#   |   |
#   |   f1
#   |   |
#   |   f2
#   |   |
#   |   f3
#   |   |
#   |   f4
#   |   |
#   e5  |
#   |   |
#   e6  |
#   |   |
#   e7  |
#   |   |
#   e8  |
#    \ /
#     F


on_committer_date "00:00" hide_error save_tag F unique_commit F tree
on_committer_date "00:01" save_tag e8 unique_commit e8 tree -p F
on_committer_date "00:02" save_tag e7 unique_commit e7 tree -p e8
on_committer_date "00:03" save_tag e6 unique_commit e6 tree -p e7
on_committer_date "00:04" save_tag e5 unique_commit e5 tree -p e6
on_committer_date "00:05" save_tag f4 unique_commit f4 tree -p F
on_committer_date "00:06" save_tag f3 unique_commit f3 tree -p f4
on_committer_date "00:07" save_tag f2 unique_commit f2 tree -p f3
on_committer_date "00:08" save_tag f1 unique_commit f1 tree -p f2
on_committer_date "00:09" save_tag e4 unique_commit e4 tree -p e5
on_committer_date "00:10" save_tag e3 unique_commit e3 tree -p e4
on_committer_date "00:11" save_tag e2 unique_commit e2 tree -p e3
on_committer_date "00:12" save_tag e1 unique_commit e1 tree -p e2
on_committer_date "00:13" save_tag E unique_commit E tree -p e1 -p f1

on_committer_date "00:00" hide_error save_tag U unique_commit U tree
on_committer_date "00:01" save_tag u0 unique_commit u0 tree -p U
on_committer_date "00:01" save_tag u1 unique_commit u1 tree -p u0
on_committer_date "00:02" save_tag u2 unique_commit u2 tree -p u0
on_committer_date "00:03" save_tag u3 unique_commit u3 tree -p u0
on_committer_date "00:04" save_tag u4 unique_commit u4 tree -p u0
on_committer_date "00:05" save_tag u5 unique_commit u5 tree -p u0
on_committer_date "00:06" save_tag V unique_commit V tree -p u1 -p u2 -p u3 -p u4 -p u5

test_sequence()
{
	_bisect_option=$1

	test_bisection_diff 0 $_bisect_option l0 ^root
	test_bisection_diff 0 $_bisect_option l1 ^root
	test_bisection_diff 0 $_bisect_option l2 ^root
	test_bisection_diff 0 $_bisect_option a0 ^root
	test_bisection_diff 0 $_bisect_option a1 ^root
	test_bisection_diff 0 $_bisect_option a2 ^root
	test_bisection_diff 0 $_bisect_option a3 ^root
	test_bisection_diff 0 $_bisect_option b1 ^root
	test_bisection_diff 0 $_bisect_option b2 ^root
	test_bisection_diff 0 $_bisect_option b3 ^root
	test_bisection_diff 0 $_bisect_option c1 ^root
	test_bisection_diff 0 $_bisect_option c2 ^root
	test_bisection_diff 0 $_bisect_option c3 ^root
	test_bisection_diff 0 $_bisect_option E ^F
	test_bisection_diff 0 $_bisect_option e1 ^F
	test_bisection_diff 0 $_bisect_option e2 ^F
	test_bisection_diff 0 $_bisect_option e3 ^F
	test_bisection_diff 0 $_bisect_option e4 ^F
	test_bisection_diff 0 $_bisect_option e5 ^F
	test_bisection_diff 0 $_bisect_option e6 ^F
	test_bisection_diff 0 $_bisect_option e7 ^F
	test_bisection_diff 0 $_bisect_option f1 ^F
	test_bisection_diff 0 $_bisect_option f2 ^F
	test_bisection_diff 0 $_bisect_option f3 ^F
	test_bisection_diff 0 $_bisect_option f4 ^F
	test_bisection_diff 0 $_bisect_option E ^F

	test_bisection_diff 1 $_bisect_option V ^U
	test_bisection_diff 0 $_bisect_option V ^U ^u1 ^u2 ^u3
	test_bisection_diff 0 $_bisect_option u1 ^U
	test_bisection_diff 0 $_bisect_option u2 ^U
	test_bisection_diff 0 $_bisect_option u3 ^U
	test_bisection_diff 0 $_bisect_option u4 ^U
	test_bisection_diff 0 $_bisect_option u5 ^U

#
# the following illustrates Linus' binary bug blatt idea.
#
# assume the bug is actually at l3, but you don't know that - all you know is that l3 is broken
# and it wasn't broken before
#
# keep bisecting the list, advancing the "bad" head and accumulating "good" heads until
# the bisection point is the head - this is the bad point.
#

test_output_expect_success "$_bisect_option l5 ^root" 'git rev-list $_bisect_option l5 ^root' <<EOF
c3
EOF

test_output_expect_success "$_bisect_option l5 ^root ^c3" 'git rev-list $_bisect_option l5 ^root ^c3' <<EOF
b4
EOF

test_output_expect_success "$_bisect_option l5 ^root ^c3 ^b4" 'git rev-list $_bisect_option l5 ^c3 ^b4' <<EOF
l3
EOF

test_output_expect_success "$_bisect_option l3 ^root ^c3 ^b4" 'git rev-list $_bisect_option l3 ^root ^c3 ^b4' <<EOF
a4
EOF

test_output_expect_success "$_bisect_option l5 ^b3 ^a3 ^b4 ^a4" 'git rev-list $_bisect_option l3 ^b3 ^a3 ^a4' <<EOF
l3
EOF

#
# if l3 is bad, then l4 is bad too - so advance the bad pointer by making b4 the known bad head
#

test_output_expect_success "$_bisect_option l4 ^a2 ^a3 ^b ^a4" 'git rev-list $_bisect_option l4 ^a2 ^a3 ^a4' <<EOF
l3
EOF

test_output_expect_success "$_bisect_option l3 ^a2 ^a3 ^b ^a4" 'git rev-list $_bisect_option l3 ^a2 ^a3 ^a4' <<EOF
l3
EOF

# found!

#
# as another example, let's consider a4 to be the bad head, in which case
#

test_output_expect_success "$_bisect_option a4 ^a2 ^a3 ^b4" 'git rev-list $_bisect_option a4 ^a2 ^a3 ^b4' <<EOF
c2
EOF

test_output_expect_success "$_bisect_option a4 ^a2 ^a3 ^b4 ^c2" 'git rev-list $_bisect_option a4 ^a2 ^a3 ^b4 ^c2' <<EOF
c3
EOF

test_output_expect_success "$_bisect_option a4 ^a2 ^a3 ^b4 ^c2 ^c3" 'git rev-list $_bisect_option a4 ^a2 ^a3 ^b4 ^c2 ^c3' <<EOF
a4
EOF

# found!

#
# or consider c3 to be the bad head
#

test_output_expect_success "$_bisect_option a4 ^a2 ^a3 ^b4" 'git rev-list $_bisect_option a4 ^a2 ^a3 ^b4' <<EOF
c2
EOF

test_output_expect_success "$_bisect_option c3 ^a2 ^a3 ^b4 ^c2" 'git rev-list $_bisect_option c3 ^a2 ^a3 ^b4 ^c2' <<EOF
c3
EOF

# found!

}

test_sequence "--bisect"

#
#

test_expect_success 'set up fake --bisect refs' '
	git update-ref refs/bisect/bad c3 &&
	good=$(git rev-parse b1) &&
	git update-ref refs/bisect/good-$good $good &&
	good=$(git rev-parse c1) &&
	git update-ref refs/bisect/good-$good $good
'

test_expect_success 'rev-list --bisect can default to good/bad refs' '
	# the only thing between c3 and c1 is c2
	git rev-parse c2 >expect &&
	git rev-list --bisect >actual &&
	test_cmp expect actual
'

test_expect_success 'rev-parse --bisect can default to good/bad refs' '
	git rev-parse c3 ^b1 ^c1 >expect &&
	git rev-parse --bisect >actual &&

	# output order depends on the refnames, which in turn depends on
	# the exact sha1s. We just want to make sure we have the same set
	# of lines in any order.
	sort <expect >expect.sorted &&
	sort <actual >actual.sorted &&
	test_cmp expect.sorted actual.sorted
'

test_output_expect_success '--bisect --first-parent' 'git rev-list --bisect --first-parent E ^F' <<EOF
e4
EOF

test_output_expect_success '--first-parent' 'git rev-list --first-parent E ^F' <<EOF
E
e1
e2
e3
e4
e5
e6
e7
e8
EOF

test_output_expect_success '--bisect-vars --first-parent' 'git rev-list --bisect-vars --first-parent E ^F' <<EOF
bisect_rev='e5'
bisect_nr=4
bisect_good=4
bisect_bad=3
bisect_all=9
bisect_steps=2
EOF

test_expect_success '--bisect-all --first-parent' '
	cat >expect.unsorted <<-EOF &&
	$(git rev-parse E) (tag: E, dist=0)
	$(git rev-parse e1) (tag: e1, dist=1)
	$(git rev-parse e2) (tag: e2, dist=2)
	$(git rev-parse e3) (tag: e3, dist=3)
	$(git rev-parse e4) (tag: e4, dist=4)
	$(git rev-parse e5) (tag: e5, dist=4)
	$(git rev-parse e6) (tag: e6, dist=3)
	$(git rev-parse e7) (tag: e7, dist=2)
	$(git rev-parse e8) (tag: e8, dist=1)
	EOF

	# expect results to be ordered by distance (descending),
	# commit hash (ascending)
	sort -k4,4r -k1,1 expect.unsorted >expect &&
	git rev-list --bisect-all --first-parent E ^F >actual &&
	test_cmp expect actual
'

test_done
