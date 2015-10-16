#!/bin/sh

test_description='git branch display tests'
. ./test-lib.sh

test_expect_success 'make commits' '
	echo content >file &&
	git add file &&
	git commit -m one &&
	echo content >>file &&
	git commit -a -m two
'

test_expect_success 'make branches' '
	git branch branch-one &&
	git branch branch-two HEAD^
'

test_expect_success 'make remote branches' '
	git update-ref refs/remotes/origin/branch-one branch-one &&
	git update-ref refs/remotes/origin/branch-two branch-two &&
	git symbolic-ref refs/remotes/origin/HEAD refs/remotes/origin/branch-one
'

cat >expect <<'EOF'
  branch-one
  branch-two
* master
EOF
test_expect_success 'git branch shows local branches' '
	git branch >actual &&
	test_cmp expect actual
'

test_expect_success 'git branch --list shows local branches' '
	git branch --list >actual &&
	test_cmp expect actual
'

cat >expect <<'EOF'
  branch-one
  branch-two
EOF
test_expect_success 'git branch --list pattern shows matching local branches' '
	git branch --list branch* >actual &&
	test_cmp expect actual
'

cat >expect <<'EOF'
  origin/HEAD -> origin/branch-one
  origin/branch-one
  origin/branch-two
EOF
test_expect_success 'git branch -r shows remote branches' '
	git branch -r >actual &&
	test_cmp expect actual
'

cat >expect <<'EOF'
  branch-one
  branch-two
* master
  remotes/origin/HEAD -> origin/branch-one
  remotes/origin/branch-one
  remotes/origin/branch-two
EOF
test_expect_success 'git branch -a shows local and remote branches' '
	git branch -a >actual &&
	test_cmp expect actual
'

cat >expect <<'EOF'
two
one
two
EOF
test_expect_success 'git branch -v shows branch summaries' '
	git branch -v >tmp &&
	awk "{print \$NF}" <tmp >actual &&
	test_cmp expect actual
'

cat >expect <<'EOF'
two
one
EOF
test_expect_success 'git branch --list -v pattern shows branch summaries' '
	git branch --list -v branch* >tmp &&
	awk "{print \$NF}" <tmp >actual &&
	test_cmp expect actual
'

test_expect_success 'git branch -v pattern does not show branch summaries' '
	test_must_fail git branch -v branch*
'

test_expect_success 'git branch shows detached HEAD properly' '
	cat >expect <<EOF &&
* (HEAD detached at $(git rev-parse --short HEAD^0))
  branch-one
  branch-two
  master
EOF
	git checkout HEAD^0 &&
	git branch >actual &&
	test_i18ncmp expect actual
'

test_expect_success 'git branch shows detached HEAD properly after checkout --detach' '
	git checkout master &&
	cat >expect <<EOF &&
* (HEAD detached at $(git rev-parse --short HEAD^0))
  branch-one
  branch-two
  master
EOF
	git checkout --detach &&
	git branch >actual &&
	test_i18ncmp expect actual
'

test_expect_success 'git branch shows detached HEAD properly after moving' '
	cat >expect <<EOF &&
* (HEAD detached from $(git rev-parse --short HEAD))
  branch-one
  branch-two
  master
EOF
	git reset --hard HEAD^1 &&
	git branch >actual &&
	test_i18ncmp expect actual
'

test_expect_success 'git branch shows detached HEAD properly from tag' '
	cat >expect <<EOF &&
* (HEAD detached at fromtag)
  branch-one
  branch-two
  master
EOF
	git tag fromtag master &&
	git checkout fromtag &&
	git branch >actual &&
	test_i18ncmp expect actual
'

test_expect_success 'git branch shows detached HEAD properly after moving from tag' '
	cat >expect <<EOF &&
* (HEAD detached from fromtag)
  branch-one
  branch-two
  master
EOF
	git reset --hard HEAD^1 &&
	git branch >actual &&
	test_i18ncmp expect actual
'

test_done
