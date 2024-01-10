#!/bin/sh
#
# Copyright (c) 2012 Zbigniew Jędrzejewski-Szmek
#

test_description='test --stat output of various commands'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-terminal.sh

# 120-character name
name=aaaaaaaaaa
name=$name$name$name$name$name$name$name$name$name$name$name$name
test_expect_success 'preparation' '
	>"$name" &&
	git add "$name" &&
	git commit -m message &&
	echo a >"$name" &&
	git commit -m message "$name"
'

cat >expect72 <<-'EOF'
 ...aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa | 1 +
EOF
test_expect_success "format-patch: small change with long name gives more space to the name" '
	git format-patch -1 --stdout >output &&
	grep " | " output >actual &&
	test_cmp expect72 actual
'

while read cmd args
do
	cat >expect80 <<-'EOF'
	 ...aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa | 1 +
	EOF
	test_expect_success "$cmd: small change with long name gives more space to the name" '
		git $cmd $args >output &&
		grep " | " output >actual &&
		test_cmp expect80 actual
	'
done <<\EOF
diff HEAD^ HEAD --stat
show --stat
log -1 --stat
EOF

cat >expect.60 <<-'EOF'
 ...aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa | 1 +
EOF
cat >expect.6030 <<-'EOF'
 ...aaaaaaaaaaaaaaaaaaaaaaaaaaa | 1 +
EOF
while read verb expect cmd args
do
	# No width limit applied when statNameWidth is ignored
	case "$expect" in expect72|expect.6030)
		test_expect_success "$cmd $verb diff.statNameWidth with long name" '
			git -c diff.statNameWidth=30 $cmd $args >output &&
			grep " | " output >actual &&
			test_cmp $expect actual
		';;
	esac
	# Maximum width limit still applied when statNameWidth is ignored
	case "$expect" in expect.60|expect.6030)
		test_expect_success "$cmd --stat=width $verb diff.statNameWidth with long name" '
			git -c diff.statNameWidth=30 $cmd $args --stat=60 >output &&
			grep " | " output >actual &&
			test_cmp $expect actual
		';;
	esac
done <<\EOF
ignores expect72 format-patch -1 --stdout
ignores expect.60 format-patch -1 --stdout
respects expect.6030 diff HEAD^ HEAD --stat
respects expect.6030 show --stat
respects expect.6030 log -1 --stat
EOF

cat >expect.40 <<-'EOF'
 ...aaaaaaaaaaaaaaaaaaaaaaaaaaaaa | 1 +
EOF
cat >expect2.40 <<-'EOF'
 ...aaaaaaaaaaaaaaaaaaaaaaaaaaaaa | 1 +
 ...aaaaaaaaaaaaaaaaaaaaaaaaaaaaa | 1 +
EOF
cat >expect2.6030 <<-'EOF'
 ...aaaaaaaaaaaaaaaaaaaaaaaaaaa | 1 +
 ...aaaaaaaaaaaaaaaaaaaaaaaaaaa | 1 +
EOF
while read expect cmd args
do
	test_expect_success "$cmd --stat=width: a long name is given more room when the bar is short" '
		git $cmd $args --stat=40 >output &&
		grep " | " output >actual &&
		test_cmp $expect.40 actual
	'

	test_expect_success "$cmd --stat-width=width with long name" '
		git $cmd $args --stat-width=40 >output &&
		grep " | " output >actual &&
		test_cmp $expect.40 actual
	'

	test_expect_success "$cmd --stat=width,name-width with long name" '
		git $cmd $args --stat=60,30 >output &&
		grep " | " output >actual &&
		test_cmp $expect.6030 actual
	'

	test_expect_success "$cmd --stat-name-width=width with long name" '
		git $cmd $args --stat-name-width=30 >output &&
		grep " | " output >actual &&
		test_cmp $expect.6030 actual
	'
done <<\EOF
expect2 format-patch --cover-letter -1 --stdout
expect diff HEAD^ HEAD --stat
expect show --stat
expect log -1 --stat
EOF

test_expect_success 'preparation for big-change tests' '
	>abcd &&
	git add abcd &&
	git commit -m message &&
	i=0 &&
	while test $i -lt 1000
	do
		echo $i && i=$(($i + 1)) || return 1
	done >abcd &&
	git commit -m message abcd
'

cat >expect72 <<'EOF'
 abcd | 1000 ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
 abcd | 1000 ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
EOF
test_expect_success "format-patch --cover-letter ignores COLUMNS with big change" '
	COLUMNS=200 git format-patch -1 --stdout --cover-letter >output &&
	grep " | " output >actual &&
	test_cmp expect72 actual
'

cat >expect72 <<'EOF'
 abcd | 1000 ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
EOF
cat >expect72-graph <<'EOF'
|  abcd | 1000 ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
EOF
cat >expect200 <<'EOF'
 abcd | 1000 ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
EOF
cat >expect200-graph <<'EOF'
|  abcd | 1000 ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
EOF
while read verb expect cmd args
do
	test_expect_success "$cmd $verb COLUMNS with big change" '
		COLUMNS=200 git $cmd $args >output &&
		grep " | " output >actual &&
		test_cmp "$expect" actual
	'

	case "$cmd" in diff|show) continue;; esac

	test_expect_success "$cmd --graph $verb COLUMNS with big change" '
		COLUMNS=200 git $cmd $args --graph >output &&
		grep " | " output >actual &&
		test_cmp "$expect-graph" actual
	'
done <<\EOF
ignores expect72 format-patch -1 --stdout
respects expect200 diff HEAD^ HEAD --stat
respects expect200 show --stat
respects expect200 log -1 --stat
EOF

cat >expect40 <<'EOF'
 abcd | 1000 ++++++++++++++++++++++++++
EOF
cat >expect40-graph <<'EOF'
|  abcd | 1000 ++++++++++++++++++++++++
EOF
while read verb expect cmd args
do
	test_expect_success "$cmd $verb not enough COLUMNS with big change" '
		COLUMNS=40 git $cmd $args >output &&
		grep " | " output >actual &&
		test_cmp "$expect" actual
	'

	case "$cmd" in diff|show) continue;; esac

	test_expect_success "$cmd --graph $verb not enough COLUMNS with big change" '
		COLUMNS=40 git $cmd $args --graph >output &&
		grep " | " output >actual &&
		test_cmp "$expect-graph" actual
	'
done <<\EOF
ignores expect72 format-patch -1 --stdout
respects expect40 diff HEAD^ HEAD --stat
respects expect40 show --stat
respects expect40 log -1 --stat
EOF

cat >expect40 <<'EOF'
 abcd | 1000 ++++++++++++++++++++++++++
EOF
cat >expect40-graph <<'EOF'
|  abcd | 1000 ++++++++++++++++++++++++++
EOF
while read verb expect cmd args
do
	test_expect_success "$cmd $verb diff.statGraphWidth" '
		git -c diff.statGraphWidth=26 $cmd $args >output &&
		grep " | " output >actual &&
		test_cmp "$expect" actual
	'

	case "$cmd" in diff|show) continue;; esac

	test_expect_success "$cmd --graph $verb diff.statGraphWidth" '
		git -c diff.statGraphWidth=26 $cmd $args --graph >output &&
		grep " | " output >actual &&
		test_cmp "$expect-graph" actual
	'
done <<\EOF
ignores expect72 format-patch -1 --stdout
respects expect40 diff HEAD^ HEAD --stat
respects expect40 show --stat
respects expect40 log -1 --stat
EOF

cat >expect <<'EOF'
 abcd | 1000 ++++++++++++++++++++++++++
EOF
cat >expect-graph <<'EOF'
|  abcd | 1000 ++++++++++++++++++++++++++
EOF
while read cmd args
do
	test_expect_success "$cmd --stat=width with big change" '
		git $cmd $args --stat=40 >output &&
		grep " | " output >actual &&
		test_cmp expect actual
	'

	test_expect_success "$cmd --stat-width=width with big change" '
		git $cmd $args --stat-width=40 >output &&
		grep " | " output >actual &&
		test_cmp expect actual
	'

	test_expect_success "$cmd --stat-graph-width=width with big change" '
		git $cmd $args --stat-graph-width=26 >output &&
		grep " | " output >actual &&
		test_cmp expect actual
	'

	case "$cmd" in diff|show) continue;; esac

	test_expect_success "$cmd --stat-width=width --graph with big change" '
		git $cmd $args --stat-width=40 --graph >output &&
		grep " | " output >actual &&
		test_cmp expect-graph actual
	'

	test_expect_success "$cmd --stat-graph-width=width --graph with big change" '
		git $cmd $args --stat-graph-width=26 --graph >output &&
		grep " | " output >actual &&
		test_cmp expect-graph actual
	'
done <<\EOF
format-patch -1 --stdout
diff HEAD^ HEAD --stat
show --stat
log -1 --stat
EOF

test_expect_success 'preparation for long-name tests' '
	cp abcd aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa &&
	git add aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa &&
	git commit -m message
'

cat >expect <<'EOF'
 ...aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa | 1000 ++++++++++++
EOF
cat >expect-graph <<'EOF'
|  ...aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa | 1000 ++++++++++++
EOF
while read cmd args
do
	test_expect_success "$cmd --stat=width with big change is more balanced" '
		git $cmd $args --stat-width=60 >output &&
		grep " | " output >actual &&
		test_cmp expect actual
	'

	case "$cmd" in diff|show) continue;; esac

	test_expect_success "$cmd --stat=width --graph with big change is balanced" '
		git $cmd $args --stat-width=60 --graph >output &&
		grep " | " output >actual &&
		test_cmp expect-graph actual
	'
done <<\EOF
format-patch -1 --stdout
diff HEAD^ HEAD --stat
show --stat
log -1 --stat
EOF

cat >expect72 <<'EOF'
 ...aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa | 1000 +++++++++++++++++
EOF
cat >expect72-graph <<'EOF'
|  ...aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa | 1000 +++++++++++++++++
EOF
cat >expect200 <<'EOF'
 aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa | 1000 +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
EOF
cat >expect200-graph <<'EOF'
|  aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa | 1000 +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
EOF
while read verb expect cmd args
do
	test_expect_success "$cmd $verb COLUMNS with long name" '
		COLUMNS=200 git $cmd $args >output &&
		grep " | " output >actual &&
		test_cmp "$expect" actual
	'

	case "$cmd" in diff|show) continue;; esac

	test_expect_success "$cmd --graph $verb COLUMNS with long name" '
		COLUMNS=200 git $cmd $args --graph >output &&
		grep " | " output >actual &&
		test_cmp "$expect-graph" actual
	'
done <<\EOF
ignores expect72 format-patch -1 --stdout
respects expect200 diff HEAD^ HEAD --stat
respects expect200 show --stat
respects expect200 log -1 --stat
EOF

cat >expect1 <<'EOF'
 ...aaaaaaa | 1000 ++++++
EOF
cat >expect1-graph <<'EOF'
|  ...aaaaaaa | 1000 ++++++
EOF
while read verb expect cmd args
do
	test_expect_success COLUMNS_CAN_BE_1 \
		"$cmd $verb prefix greater than COLUMNS with big change" '
		COLUMNS=1 git $cmd $args >output &&
		grep " | " output >actual &&
		test_cmp "$expect" actual
	'

	case "$cmd" in diff|show) continue;; esac

	test_expect_success COLUMNS_CAN_BE_1 \
		"$cmd --graph $verb prefix greater than COLUMNS with big change" '
		COLUMNS=1 git $cmd $args --graph >output &&
		grep " | " output >actual &&
		test_cmp "$expect-graph" actual
	'
done <<\EOF
ignores expect72 format-patch -1 --stdout
respects expect1 diff HEAD^ HEAD --stat
respects expect1 show --stat
respects expect1 log -1 --stat
EOF

cat >expect <<'EOF'
 abcd | 1000 ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
EOF
test_expect_success 'merge --stat respects diff.statGraphWidth with big change' '
	git checkout -b branch1 HEAD^^ &&
	git -c diff.statGraphWidth=26 merge --stat --no-ff main^ >output &&
	grep " | " output >actual &&
	test_cmp expect40 actual
'
test_expect_success 'merge --stat respects COLUMNS with big change' '
	git checkout -b branch2 HEAD^^ &&
	COLUMNS=100 git merge --stat --no-ff main^ >output &&
	grep " | " output >actual &&
	test_cmp expect actual
'

cat >expect <<'EOF'
 aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa | 1000 +++++++++++++++++++++++++++++++++++++++
EOF
cat >expect.30 <<'EOF'
 ...aaaaaaaaaaaaaaaaaaaaaaaaaaa | 1000 ++++++++++++++++++++++++++++++++++++++++
EOF
test_expect_success 'merge --stat respects diff.statNameWidth with long name' '
	git switch branch1 &&
	git -c diff.statNameWidth=30 merge --stat --no-ff main >output &&
	grep " | " output >actual &&
	test_cmp expect.30 actual
'
test_expect_success 'merge --stat respects COLUMNS with long name' '
	git switch branch2 &&
	COLUMNS=100 git merge --stat --no-ff main >output &&
	grep " | " output >actual &&
	test_cmp expect actual
'

test_done
