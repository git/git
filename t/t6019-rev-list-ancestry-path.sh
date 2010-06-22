#!/bin/sh

test_description='--ancestry-path'

#          D---E-------F
#         /     \       \
#    B---C---G---H---I---J
#   /                     \
#  A-------K---------------L--M
#
#  D..M                 == E F G H I J K L M
#  --ancestry-path D..M == E F H I J L M
#
#  D..M -- M.t                 == M
#  --ancestry-path D..M -- M.t == M

. ./test-lib.sh

test_merge () {
	test_tick &&
	git merge -s ours -m "$2" "$1" &&
	git tag "$2"
}

test_expect_success setup '
	test_commit A &&
	test_commit B &&
	test_commit C &&
	test_commit D &&
	test_commit E &&
	test_commit F &&
	git reset --hard C &&
	test_commit G &&
	test_merge E H &&
	test_commit I &&
	test_merge F J &&
	git reset --hard A &&
	test_commit K &&
	test_merge J L &&
	test_commit M
'

test_expect_success 'rev-list D..M' '
	for c in E F G H I J K L M; do echo $c; done >expect &&
	git rev-list --format=%s D..M |
	sed -e "/^commit /d" |
	sort >actual &&
	test_cmp expect actual
'

test_expect_success 'rev-list --ancestry-path D..M' '
	for c in E F H I J L M; do echo $c; done >expect &&
	git rev-list --ancestry-path --format=%s D..M |
	sed -e "/^commit /d" |
	sort >actual &&
	test_cmp expect actual
'

test_expect_success 'rev-list D..M -- M.t' '
	echo M >expect &&
	git rev-list --format=%s D..M -- M.t |
	sed -e "/^commit /d" >actual &&
	test_cmp expect actual
'

test_expect_success 'rev-list --ancestry-patch D..M -- M.t' '
	echo M >expect &&
	git rev-list --ancestry-path --format=%s D..M -- M.t |
	sed -e "/^commit /d" >actual &&
	test_cmp expect actual
'

test_done
