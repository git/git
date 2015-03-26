#!/bin/sh
#
# Copyright (c) 2012 Zbigniew Jędrzejewski-Szmek
#

test_description='test --stat output of various commands'

. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-terminal.sh

# 120 character name
name=aaaaaaaaaa
name=$name$name$name$name$name$name$name$name$name$name$name$name
test_expect_success 'preparation' '
	>"$name" &&
	git add "$name" &&
	git commit -m message &&
	echo a >"$name" &&
	git commit -m message "$name"
'

while read cmd args
do
	cat >expect <<-'EOF'
	 ...aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa | 1 +
	EOF
	test_expect_success "$cmd: small change with long name gives more space to the name" '
		git $cmd $args >output &&
		grep " | " output >actual &&
		test_cmp expect actual
	'

	cat >expect <<-'EOF'
	 ...aaaaaaaaaaaaaaaaaaaaaaaaaaaaa | 1 +
	EOF
	test_expect_success "$cmd --stat=width: a long name is given more room when the bar is short" '
		git $cmd $args --stat=40 >output &&
		grep " | " output >actual &&
		test_cmp expect actual
	'

	test_expect_success "$cmd --stat-width=width with long name" '
		git $cmd $args --stat-width=40 >output &&
		grep " | " output >actual &&
		test_cmp expect actual
	'

	cat >expect <<-'EOF'
	 ...aaaaaaaaaaaaaaaaaaaaaaaaaaa | 1 +
	EOF
	test_expect_success "$cmd --stat=...,name-width with long name" '
		git $cmd $args --stat=60,30 >output &&
		grep " | " output >actual &&
		test_cmp expect actual
	'

	test_expect_success "$cmd --stat-name-width with long name" '
		git $cmd $args --stat-name-width=30 >output &&
		grep " | " output >actual &&
		test_cmp expect actual
	'
done <<\EOF
format-patch -1 --stdout
diff HEAD^ HEAD --stat
show --stat
log -1 --stat
EOF


test_expect_success 'preparation for big change tests' '
	>abcd &&
	git add abcd &&
	git commit -m message &&
	i=0 &&
	while test $i -lt 1000
	do
		echo $i && i=$(($i + 1))
	done >abcd &&
	git commit -m message abcd
'

cat >expect80 <<'EOF'
 abcd | 1000 ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
EOF
cat >expect80-graph <<'EOF'
|  abcd | 1000 ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
EOF
cat >expect200 <<'EOF'
 abcd | 1000 ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
EOF
cat >expect200-graph <<'EOF'
|  abcd | 1000 ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
EOF
while read verb expect cmd args
do
	test_expect_success "$cmd $verb COLUMNS (big change)" '
		COLUMNS=200 git $cmd $args >output &&
		grep " | " output >actual &&
		test_cmp "$expect" actual
	'

	case "$cmd" in diff|show) continue;; esac

	test_expect_success "$cmd --graph $verb COLUMNS (big change)" '
		COLUMNS=200 git $cmd $args --graph >output &&
		grep " | " output >actual &&
		test_cmp "$expect-graph" actual
	'
done <<\EOF
ignores expect80 format-patch -1 --stdout
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
	test_expect_success "$cmd $verb not enough COLUMNS (big change)" '
		COLUMNS=40 git $cmd $args >output &&
		grep " | " output >actual &&
		test_cmp "$expect" actual
	'

	case "$cmd" in diff|show) continue;; esac

	test_expect_success "$cmd --graph $verb not enough COLUMNS (big change)" '
		COLUMNS=40 git $cmd $args --graph >output &&
		grep " | " output >actual &&
		test_cmp "$expect-graph" actual
	'
done <<\EOF
ignores expect80 format-patch -1 --stdout
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
	test_expect_success "$cmd $verb statGraphWidth config" '
		git -c diff.statGraphWidth=26 $cmd $args >output &&
		grep " | " output >actual &&
		test_cmp "$expect" actual
	'

	case "$cmd" in diff|show) continue;; esac

	test_expect_success "$cmd --graph $verb statGraphWidth config" '
		git -c diff.statGraphWidth=26 $cmd $args --graph >output &&
		grep " | " output >actual &&
		test_cmp "$expect-graph" actual
	'
done <<\EOF
ignores expect80 format-patch -1 --stdout
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

	test_expect_success "$cmd --stat-graph-width with big change" '
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

	test_expect_success "$cmd --stat-graph-width --graph with big change" '
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

test_expect_success 'preparation for long filename tests' '
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

cat >expect80 <<'EOF'
 ...aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa | 1000 ++++++++++++++++++++
EOF
cat >expect80-graph <<'EOF'
|  ...aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa | 1000 ++++++++++++++++++++
EOF
cat >expect200 <<'EOF'
 aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa | 1000 +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
EOF
cat >expect200-graph <<'EOF'
|  aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa | 1000 +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
EOF
while read verb expect cmd args
do
	test_expect_success "$cmd $verb COLUMNS (long filename)" '
		COLUMNS=200 git $cmd $args >output &&
		grep " | " output >actual &&
		test_cmp "$expect" actual
	'

	case "$cmd" in diff|show) continue;; esac

	test_expect_success "$cmd --graph $verb COLUMNS (long filename)" '
		COLUMNS=200 git $cmd $args --graph >output &&
		grep " | " output >actual &&
		test_cmp "$expect-graph" actual
	'
done <<\EOF
ignores expect80 format-patch -1 --stdout
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
		"$cmd $verb prefix greater than COLUMNS (big change)" '
		COLUMNS=1 git $cmd $args >output &&
		grep " | " output >actual &&
		test_cmp "$expect" actual
	'

	case "$cmd" in diff|show) continue;; esac

	test_expect_success COLUMNS_CAN_BE_1 \
		"$cmd --graph $verb prefix greater than COLUMNS (big change)" '
		COLUMNS=1 git $cmd $args --graph >output &&
		grep " | " output >actual &&
		test_cmp "$expect-graph" actual
	'
done <<\EOF
ignores expect80 format-patch -1 --stdout
respects expect1 diff HEAD^ HEAD --stat
respects expect1 show --stat
respects expect1 log -1 --stat
EOF

cat >expect <<'EOF'
 abcd | 1000 ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
EOF
test_expect_success 'merge --stat respects COLUMNS (big change)' '
	git checkout -b branch HEAD^^ &&
	COLUMNS=100 git merge --stat --no-ff master^ >output &&
	grep " | " output >actual &&
	test_cmp expect actual
'

cat >expect <<'EOF'
 aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa | 1000 +++++++++++++++++++++++++++++++++++++++
EOF
test_expect_success 'merge --stat respects COLUMNS (long filename)' '
	COLUMNS=100 git merge --stat --no-ff master >output &&
	grep " | " output >actual &&
	test_cmp expect actual
'

test_done
