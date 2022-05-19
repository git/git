#!/bin/sh

test_description='test but rev-list --cherry-pick -- file'

BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

# A---B---D---F
#  \
#   \
#    C---E
#
# B changes a file foo.c, adding a line of text.  C changes foo.c as
# well as bar.c, but the change in foo.c was identical to change B.
# D and C change bar in the same way, E and F differently.

test_expect_success setup '
	echo Hallo > foo &&
	but add foo &&
	test_tick &&
	but cummit -m "A" &&
	but tag A &&
	but checkout -b branch &&
	echo Bello > foo &&
	echo Cello > bar &&
	but add foo bar &&
	test_tick &&
	but cummit -m "C" &&
	but tag C &&
	echo Dello > bar &&
	but add bar &&
	test_tick &&
	but cummit -m "E" &&
	but tag E &&
	but checkout main &&
	but checkout branch foo &&
	test_tick &&
	but cummit -m "B" &&
	but tag B &&
	echo Cello > bar &&
	but add bar &&
	test_tick &&
	but cummit -m "D" &&
	but tag D &&
	echo Nello > bar &&
	but add bar &&
	test_tick &&
	but cummit -m "F" &&
	but tag F
'

cat >expect <<EOF
<tags/B
>tags/C
EOF

test_expect_success '--left-right' '
	but rev-list --left-right B...C > actual &&
	but name-rev --annotate-stdin --name-only --refs="*tags/*" \
		< actual > actual.named &&
	test_cmp expect actual.named
'

test_expect_success '--count' '
	but rev-list --count B...C > actual &&
	test "$(cat actual)" = 2
'

test_expect_success '--cherry-pick foo comes up empty' '
	test -z "$(but rev-list --left-right --cherry-pick B...C -- foo)"
'

cat >expect <<EOF
>tags/C
EOF

test_expect_success '--cherry-pick bar does not come up empty' '
	but rev-list --left-right --cherry-pick B...C -- bar > actual &&
	but name-rev --annotate-stdin --name-only --refs="*tags/*" \
		< actual > actual.named &&
	test_cmp expect actual.named
'

test_expect_success 'bar does not come up empty' '
	but rev-list --left-right B...C -- bar > actual &&
	but name-rev --annotate-stdin --name-only --refs="*tags/*" \
		< actual > actual.named &&
	test_cmp expect actual.named
'

cat >expect <<EOF
<tags/F
>tags/E
EOF

test_expect_success '--cherry-pick bar does not come up empty (II)' '
	but rev-list --left-right --cherry-pick F...E -- bar > actual &&
	but name-rev --annotate-stdin --name-only --refs="*tags/*" \
		< actual > actual.named &&
	test_cmp expect actual.named
'

test_expect_success 'name-rev multiple --refs combine inclusive' '
	but rev-list --left-right --cherry-pick F...E -- bar >actual &&
	but name-rev --annotate-stdin --name-only --refs="*tags/F" --refs="*tags/E" \
		<actual >actual.named &&
	test_cmp expect actual.named
'

cat >expect <<EOF
<tags/F
EOF

test_expect_success 'name-rev --refs excludes non-matched patterns' '
	but rev-list --left-right --right-only --cherry-pick F...E -- bar >>expect &&
	but rev-list --left-right --cherry-pick F...E -- bar >actual &&
	but name-rev --annotate-stdin --name-only --refs="*tags/F" \
		<actual >actual.named &&
	test_cmp expect actual.named
'

cat >expect <<EOF
<tags/F
EOF

test_expect_success 'name-rev --exclude excludes matched patterns' '
	but rev-list --left-right --right-only --cherry-pick F...E -- bar >>expect &&
	but rev-list --left-right --cherry-pick F...E -- bar >actual &&
	but name-rev --annotate-stdin --name-only --refs="*tags/*" --exclude="*E" \
		<actual >actual.named &&
	test_cmp expect actual.named
'

test_expect_success 'name-rev --no-refs clears the refs list' '
	but rev-list --left-right --cherry-pick F...E -- bar >expect &&
	but name-rev --annotate-stdin --name-only --refs="*tags/F" --refs="*tags/E" --no-refs --refs="*tags/G" \
		<expect >actual &&
	test_cmp expect actual
'

cat >expect <<EOF
+tags/F
=tags/D
+tags/E
=tags/C
EOF

test_expect_success '--cherry-mark' '
	but rev-list --cherry-mark F...E -- bar > actual &&
	but name-rev --annotate-stdin --name-only --refs="*tags/*" \
		< actual > actual.named &&
	test_cmp expect actual.named
'

cat >expect <<EOF
<tags/F
=tags/D
>tags/E
=tags/C
EOF

test_expect_success '--cherry-mark --left-right' '
	but rev-list --cherry-mark --left-right F...E -- bar > actual &&
	but name-rev --annotate-stdin --name-only --refs="*tags/*" \
		< actual > actual.named &&
	test_cmp expect actual.named
'

cat >expect <<EOF
tags/E
EOF

test_expect_success '--cherry-pick --right-only' '
	but rev-list --cherry-pick --right-only F...E -- bar > actual &&
	but name-rev --annotate-stdin --name-only --refs="*tags/*" \
		< actual > actual.named &&
	test_cmp expect actual.named
'

test_expect_success '--cherry-pick --left-only' '
	but rev-list --cherry-pick --left-only E...F -- bar > actual &&
	but name-rev --annotate-stdin --name-only --refs="*tags/*" \
		< actual > actual.named &&
	test_cmp expect actual.named
'

cat >expect <<EOF
+tags/E
=tags/C
EOF

test_expect_success '--cherry' '
	but rev-list --cherry F...E -- bar > actual &&
	but name-rev --annotate-stdin --name-only --refs="*tags/*" \
		< actual > actual.named &&
	test_cmp expect actual.named
'

cat >expect <<EOF
1	1
EOF

test_expect_success '--cherry --count' '
	but rev-list --cherry --count F...E -- bar > actual &&
	test_cmp expect actual
'

cat >expect <<EOF
2	2
EOF

test_expect_success '--cherry-mark --count' '
	but rev-list --cherry-mark --count F...E -- bar > actual &&
	test_cmp expect actual
'

cat >expect <<EOF
1	1	2
EOF

test_expect_success '--cherry-mark --left-right --count' '
	but rev-list --cherry-mark --left-right --count F...E -- bar > actual &&
	test_cmp expect actual
'

test_expect_success '--cherry-pick with independent, but identical branches' '
	but symbolic-ref HEAD refs/heads/independent &&
	rm .but/index &&
	echo Hallo > foo &&
	but add foo &&
	test_tick &&
	but cummit -m "independent" &&
	echo Bello > foo &&
	test_tick &&
	but cummit -m "independent, too" foo &&
	test -z "$(but rev-list --left-right --cherry-pick \
		HEAD...main -- foo)"
'

cat >expect <<EOF
1	2
EOF

test_expect_success '--count --left-right' '
	but rev-list --count --left-right C...D > actual &&
	test_cmp expect actual
'

test_expect_success '--cherry-pick with duplicates on each side' '
	but checkout -b dup-orig &&
	test_cummit dup-base &&
	but revert dup-base &&
	but cherry-pick dup-base &&
	but checkout -b dup-side HEAD~3 &&
	test_tick &&
	but cherry-pick -3 dup-orig &&
	but rev-list --cherry-pick dup-orig...dup-side >actual &&
	test_must_be_empty actual
'

# Corrupt the object store deliberately to make sure
# the object is not even checked for its existence.
remove_loose_object () {
	sha1="$(but rev-parse "$1")" &&
	remainder=${sha1#??} &&
	firsttwo=${sha1%$remainder} &&
	rm .but/objects/$firsttwo/$remainder
}

test_expect_success '--cherry-pick avoids looking at full diffs' '
	but checkout -b shy-diff &&
	test_cummit dont-look-at-me &&
	echo Hello >dont-look-at-me.t &&
	test_tick &&
	but cummit -m tip dont-look-at-me.t &&
	but checkout -b mainline HEAD^ &&
	test_cummit to-cherry-pick &&
	remove_loose_object shy-diff^:dont-look-at-me.t &&
	but rev-list --cherry-pick ...shy-diff
'

test_done
