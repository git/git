#!/bin/sh

test_description='git branch display tests'

. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-terminal.sh

test_expect_success 'make commits' '
	echo content >file &&
	git add file &&
	git commit -m one &&
	git branch -M main &&
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
* main
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
	test_cmp expect actual &&

	git branch --remotes >actual &&
	test_cmp expect actual
'

test_expect_success 'git branch --no-remotes is rejected' '
	test_must_fail git branch --no-remotes 2>err &&
	grep "unknown option .no-remotes." err
'

cat >expect <<'EOF'
  branch-one
  branch-two
* main
  remotes/origin/HEAD -> origin/branch-one
  remotes/origin/branch-one
  remotes/origin/branch-two
EOF
test_expect_success 'git branch -a shows local and remote branches' '
	git branch -a >actual &&
	test_cmp expect actual &&

	git branch --all >actual &&
	test_cmp expect actual
'

test_expect_success 'git branch --no-all is rejected' '
	test_must_fail git branch --no-all 2>err &&
	grep "unknown option .no-all." err
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
test_expect_success 'git branch --ignore-case --list -v pattern shows branch summaries' '
	git branch --list --ignore-case -v BRANCH* >tmp &&
	awk "{print \$NF}" <tmp >actual &&
	test_cmp expect actual
'

test_expect_success 'git branch -v pattern does not show branch summaries' '
	test_must_fail git branch -v branch*
'

test_expect_success 'git branch `--show-current` shows current branch' '
	cat >expect <<-\EOF &&
	branch-two
	EOF
	git checkout branch-two &&
	git branch --show-current >actual &&
	test_cmp expect actual
'

test_expect_success 'git branch `--show-current` is silent when detached HEAD' '
	git checkout HEAD^0 &&
	git branch --show-current >actual &&
	test_must_be_empty actual
'

test_expect_success 'git branch `--show-current` works properly when tag exists' '
	cat >expect <<-\EOF &&
	branch-and-tag-name
	EOF
	test_when_finished "
		git checkout branch-one
		git branch -D branch-and-tag-name
	" &&
	git checkout -b branch-and-tag-name &&
	test_when_finished "git tag -d branch-and-tag-name" &&
	git tag branch-and-tag-name &&
	git branch --show-current >actual &&
	test_cmp expect actual
'

test_expect_success 'git branch `--show-current` works properly with worktrees' '
	cat >expect <<-\EOF &&
	branch-one
	branch-two
	EOF
	git checkout branch-one &&
	test_when_finished "
		git worktree remove worktree_dir
	" &&
	git worktree add worktree_dir branch-two &&
	{
		git branch --show-current &&
		git -C worktree_dir branch --show-current
	} >actual &&
	test_cmp expect actual
'

test_expect_success 'git branch shows detached HEAD properly' '
	cat >expect <<EOF &&
* (HEAD detached at $(git rev-parse --short HEAD^0))
  branch-one
  branch-two
  main
EOF
	git checkout HEAD^0 &&
	git branch >actual &&
	test_cmp expect actual
'

test_expect_success 'git branch shows detached HEAD properly after checkout --detach' '
	git checkout main &&
	cat >expect <<EOF &&
* (HEAD detached at $(git rev-parse --short HEAD^0))
  branch-one
  branch-two
  main
EOF
	git checkout --detach &&
	git branch >actual &&
	test_cmp expect actual
'

test_expect_success 'git branch shows detached HEAD properly after moving' '
	cat >expect <<EOF &&
* (HEAD detached from $(git rev-parse --short HEAD))
  branch-one
  branch-two
  main
EOF
	git reset --hard HEAD^1 &&
	git branch >actual &&
	test_cmp expect actual
'

test_expect_success 'git branch shows detached HEAD properly from tag' '
	cat >expect <<EOF &&
* (HEAD detached at fromtag)
  branch-one
  branch-two
  main
EOF
	git tag fromtag main &&
	git checkout fromtag &&
	git branch >actual &&
	test_cmp expect actual
'

test_expect_success 'git branch shows detached HEAD properly after moving from tag' '
	cat >expect <<EOF &&
* (HEAD detached from fromtag)
  branch-one
  branch-two
  main
EOF
	git reset --hard HEAD^1 &&
	git branch >actual &&
	test_cmp expect actual
'

test_expect_success 'git branch `--sort=[-]objectsize` option' '
	cat >expect <<-\EOF &&
	* (HEAD detached from fromtag)
	  branch-two
	  branch-one
	  main
	EOF
	git branch --sort=objectsize >actual &&
	test_cmp expect actual &&

	cat >expect <<-\EOF &&
	* (HEAD detached from fromtag)
	  branch-one
	  main
	  branch-two
	EOF
	git branch --sort=-objectsize >actual &&
	test_cmp expect actual
'

test_expect_success 'git branch `--sort=[-]type` option' '
	cat >expect <<-\EOF &&
	* (HEAD detached from fromtag)
	  branch-one
	  branch-two
	  main
	EOF
	git branch --sort=type >actual &&
	test_cmp expect actual &&

	cat >expect <<-\EOF &&
	* (HEAD detached from fromtag)
	  branch-one
	  branch-two
	  main
	EOF
	git branch --sort=-type >actual &&
	test_cmp expect actual
'

test_expect_success 'git branch `--sort=[-]version:refname` option' '
	cat >expect <<-\EOF &&
	* (HEAD detached from fromtag)
	  branch-one
	  branch-two
	  main
	EOF
	git branch --sort=version:refname >actual &&
	test_cmp expect actual &&

	cat >expect <<-\EOF &&
	* (HEAD detached from fromtag)
	  main
	  branch-two
	  branch-one
	EOF
	git branch --sort=-version:refname >actual &&
	test_cmp expect actual
'

test_expect_success 'git branch --points-at option' '
	cat >expect <<-\EOF &&
	  branch-one
	  main
	EOF
	git branch --points-at=branch-one >actual &&
	test_cmp expect actual
'

test_expect_success 'ambiguous branch/tag not marked' '
	git tag ambiguous &&
	git branch ambiguous &&
	echo "  ambiguous" >expect &&
	git branch --list ambiguous >actual &&
	test_cmp expect actual
'

test_expect_success 'local-branch symrefs shortened properly' '
	git symbolic-ref refs/heads/ref-to-branch refs/heads/branch-one &&
	git symbolic-ref refs/heads/ref-to-remote refs/remotes/origin/branch-one &&
	cat >expect <<-\EOF &&
	  ref-to-branch -> branch-one
	  ref-to-remote -> origin/branch-one
	EOF
	git branch >actual.raw &&
	grep ref-to <actual.raw >actual &&
	test_cmp expect actual
'

test_expect_success 'sort branches, ignore case' '
	(
		git init -b main sort-icase &&
		cd sort-icase &&
		test_commit initial &&
		git branch branch-one &&
		git branch BRANCH-two &&
		git branch --list | awk "{print \$NF}" >actual &&
		cat >expected <<-\EOF &&
		BRANCH-two
		branch-one
		main
		EOF
		test_cmp expected actual &&
		git branch --list -i | awk "{print \$NF}" >actual &&
		cat >expected <<-\EOF &&
		branch-one
		BRANCH-two
		main
		EOF
		test_cmp expected actual
	)
'

test_expect_success 'git branch --format option' '
	cat >expect <<-\EOF &&
	Refname is (HEAD detached from fromtag)
	Refname is refs/heads/ambiguous
	Refname is refs/heads/branch-one
	Refname is refs/heads/branch-two
	Refname is refs/heads/main
	Refname is refs/heads/ref-to-branch
	Refname is refs/heads/ref-to-remote
	EOF
	git branch --format="Refname is %(refname)" >actual &&
	test_cmp expect actual
'

test_expect_success 'git branch --format with ahead-behind' '
	cat >expect <<-\EOF &&
	(HEAD detached from fromtag) 0 0
	refs/heads/ambiguous 0 0
	refs/heads/branch-one 1 0
	refs/heads/branch-two 0 0
	refs/heads/main 1 0
	refs/heads/ref-to-branch 1 0
	refs/heads/ref-to-remote 1 0
	EOF
	git branch --format="%(refname) %(ahead-behind:HEAD)" >actual &&
	test_cmp expect actual
'

test_expect_success 'git branch with --format=%(rest) must fail' '
	test_must_fail git branch --format="%(rest)" >actual
'

test_expect_success 'git branch --format --omit-empty' '
	cat >expect <<-\EOF &&
	Refname is (HEAD detached from fromtag)
	Refname is refs/heads/ambiguous
	Refname is refs/heads/branch-one
	Refname is refs/heads/branch-two

	Refname is refs/heads/ref-to-branch
	Refname is refs/heads/ref-to-remote
	EOF
	git branch --format="%(if:notequals=refs/heads/main)%(refname)%(then)Refname is %(refname)%(end)" >actual &&
	test_cmp expect actual &&
	cat >expect <<-\EOF &&
	Refname is (HEAD detached from fromtag)
	Refname is refs/heads/ambiguous
	Refname is refs/heads/branch-one
	Refname is refs/heads/branch-two
	Refname is refs/heads/ref-to-branch
	Refname is refs/heads/ref-to-remote
	EOF
	git branch --omit-empty --format="%(if:notequals=refs/heads/main)%(refname)%(then)Refname is %(refname)%(end)" >actual &&
	test_cmp expect actual
'

test_expect_success 'worktree colors correct' '
	cat >expect <<-EOF &&
	* <GREEN>(HEAD detached from fromtag)<RESET>
	  ambiguous<RESET>
	  branch-one<RESET>
	+ <CYAN>branch-two<RESET>
	  main<RESET>
	  ref-to-branch<RESET> -> branch-one
	  ref-to-remote<RESET> -> origin/branch-one
	EOF
	git worktree add worktree_dir branch-two &&
	git branch --color >actual.raw &&
	rm -r worktree_dir &&
	git worktree prune &&
	test_decode_color <actual.raw >actual &&
	test_cmp expect actual
'

test_expect_success "set up color tests" '
	echo "<RED>main<RESET>" >expect.color &&
	echo "main" >expect.bare &&
	color_args="--format=%(color:red)%(refname:short) --list main"
'

test_expect_success '%(color) omitted without tty' '
	TERM=vt100 git branch $color_args >actual.raw &&
	test_decode_color <actual.raw >actual &&
	test_cmp expect.bare actual
'

test_expect_success TTY '%(color) present with tty' '
	test_terminal git branch $color_args >actual.raw &&
	test_decode_color <actual.raw >actual &&
	test_cmp expect.color actual
'

test_expect_success '--color overrides auto-color' '
	git branch --color $color_args >actual.raw &&
	test_decode_color <actual.raw >actual &&
	test_cmp expect.color actual
'

test_expect_success 'verbose output lists worktree path' '
	one=$(git rev-parse --short HEAD) &&
	two=$(git rev-parse --short main) &&
	cat >expect <<-EOF &&
	* (HEAD detached from fromtag) $one one
	  ambiguous                    $one one
	  branch-one                   $two two
	+ branch-two                   $one ($(pwd)/worktree_dir) one
	  main                         $two two
	  ref-to-branch                $two two
	  ref-to-remote                $two two
	EOF
	git worktree add worktree_dir branch-two &&
	git branch -vv >actual &&
	rm -r worktree_dir &&
	git worktree prune &&
	test_cmp expect actual
'

test_done
