#!/bin/sh

test_description='test git rev-list --cherry-pick -- file'

. ./test-lib.sh

# A---B---D
#  \
#   \
#    C---E
#
# B changes a file foo.c, adding a line of text.  C changes foo.c as
# well as bar.c, but the change in foo.c was identical to change B.
# D and C change bar in the same way, E differently.

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
	echo Dello > bar &&
	git add bar &&
	test_tick &&
	git commit -m "E" &&
	git tag E &&
	git checkout master &&
	git checkout branch foo &&
	test_tick &&
	git commit -m "B" &&
	git tag B &&
	echo Cello > bar &&
	git add bar &&
	test_tick &&
	git commit -m "D" &&
	git tag D
'

cat >expect <<EOF
<tags/B
>tags/C
EOF

test_expect_success '--left-right' '
	git rev-list --left-right B...C > actual &&
	git name-rev --stdin --name-only --refs="*tags/*" \
		< actual > actual.named &&
	test_cmp actual.named expect
'

test_expect_success '--count' '
	git rev-list --count B...C > actual &&
	test "$(cat actual)" = 2
'

test_expect_success '--cherry-pick foo comes up empty' '
	test -z "$(git rev-list --left-right --cherry-pick B...C -- foo)"
'

cat >expect <<EOF
>tags/C
EOF

test_expect_success '--cherry-pick bar does not come up empty' '
	git rev-list --left-right --cherry-pick B...C -- bar > actual &&
	git name-rev --stdin --name-only --refs="*tags/*" \
		< actual > actual.named &&
	test_cmp actual.named expect
'

test_expect_success 'bar does not come up empty' '
	git rev-list --left-right B...C -- bar > actual &&
	git name-rev --stdin --name-only --refs="*tags/*" \
		< actual > actual.named &&
	test_cmp actual.named expect
'

cat >expect <<EOF
>tags/E
EOF

test_expect_success '--cherry-pick bar does not come up empty (II)' '
	git rev-list --left-right --cherry-pick D...E -- bar > actual &&
	git name-rev --stdin --name-only --refs="*tags/*" \
		< actual > actual.named &&
	test_cmp actual.named expect
'

test_expect_success '--cherry-pick with independent, but identical branches' '
	git symbolic-ref HEAD refs/heads/independent &&
	rm .git/index &&
	echo Hallo > foo &&
	git add foo &&
	test_tick &&
	git commit -m "independent" &&
	echo Bello > foo &&
	test_tick &&
	git commit -m "independent, too" foo &&
	test -z "$(git rev-list --left-right --cherry-pick \
		HEAD...master -- foo)"
'

cat >expect <<EOF
1	2
EOF

test_expect_success '--count --left-right' '
	git rev-list --count --left-right C...D > actual &&
	test_cmp expect actual
'

test_done
