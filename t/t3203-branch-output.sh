#!/bin/sh

test_description='but branch display tests'
. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-terminal.sh

test_expect_success 'make cummits' '
	echo content >file &&
	but add file &&
	but cummit -m one &&
	but branch -M main &&
	echo content >>file &&
	but cummit -a -m two
'

test_expect_success 'make branches' '
	but branch branch-one &&
	but branch branch-two HEAD^
'

test_expect_success 'make remote branches' '
	but update-ref refs/remotes/origin/branch-one branch-one &&
	but update-ref refs/remotes/origin/branch-two branch-two &&
	but symbolic-ref refs/remotes/origin/HEAD refs/remotes/origin/branch-one
'

cat >expect <<'EOF'
  branch-one
  branch-two
* main
EOF
test_expect_success 'but branch shows local branches' '
	but branch >actual &&
	test_cmp expect actual
'

test_expect_success 'but branch --list shows local branches' '
	but branch --list >actual &&
	test_cmp expect actual
'

cat >expect <<'EOF'
  branch-one
  branch-two
EOF
test_expect_success 'but branch --list pattern shows matching local branches' '
	but branch --list branch* >actual &&
	test_cmp expect actual
'

cat >expect <<'EOF'
  origin/HEAD -> origin/branch-one
  origin/branch-one
  origin/branch-two
EOF
test_expect_success 'but branch -r shows remote branches' '
	but branch -r >actual &&
	test_cmp expect actual
'

cat >expect <<'EOF'
  branch-one
  branch-two
* main
  remotes/origin/HEAD -> origin/branch-one
  remotes/origin/branch-one
  remotes/origin/branch-two
EOF
test_expect_success 'but branch -a shows local and remote branches' '
	but branch -a >actual &&
	test_cmp expect actual
'

cat >expect <<'EOF'
two
one
two
EOF
test_expect_success 'but branch -v shows branch summaries' '
	but branch -v >tmp &&
	awk "{print \$NF}" <tmp >actual &&
	test_cmp expect actual
'

cat >expect <<'EOF'
two
one
EOF
test_expect_success 'but branch --list -v pattern shows branch summaries' '
	but branch --list -v branch* >tmp &&
	awk "{print \$NF}" <tmp >actual &&
	test_cmp expect actual
'
test_expect_success 'but branch --ignore-case --list -v pattern shows branch summaries' '
	but branch --list --ignore-case -v BRANCH* >tmp &&
	awk "{print \$NF}" <tmp >actual &&
	test_cmp expect actual
'

test_expect_success 'but branch -v pattern does not show branch summaries' '
	test_must_fail but branch -v branch*
'

test_expect_success 'but branch `--show-current` shows current branch' '
	cat >expect <<-\EOF &&
	branch-two
	EOF
	but checkout branch-two &&
	but branch --show-current >actual &&
	test_cmp expect actual
'

test_expect_success 'but branch `--show-current` is silent when detached HEAD' '
	but checkout HEAD^0 &&
	but branch --show-current >actual &&
	test_must_be_empty actual
'

test_expect_success 'but branch `--show-current` works properly when tag exists' '
	cat >expect <<-\EOF &&
	branch-and-tag-name
	EOF
	test_when_finished "
		but checkout branch-one
		but branch -D branch-and-tag-name
	" &&
	but checkout -b branch-and-tag-name &&
	test_when_finished "but tag -d branch-and-tag-name" &&
	but tag branch-and-tag-name &&
	but branch --show-current >actual &&
	test_cmp expect actual
'

test_expect_success 'but branch `--show-current` works properly with worktrees' '
	cat >expect <<-\EOF &&
	branch-one
	branch-two
	EOF
	but checkout branch-one &&
	test_when_finished "
		but worktree remove worktree_dir
	" &&
	but worktree add worktree_dir branch-two &&
	{
		but branch --show-current &&
		but -C worktree_dir branch --show-current
	} >actual &&
	test_cmp expect actual
'

test_expect_success 'but branch shows detached HEAD properly' '
	cat >expect <<EOF &&
* (HEAD detached at $(but rev-parse --short HEAD^0))
  branch-one
  branch-two
  main
EOF
	but checkout HEAD^0 &&
	but branch >actual &&
	test_cmp expect actual
'

test_expect_success 'but branch shows detached HEAD properly after checkout --detach' '
	but checkout main &&
	cat >expect <<EOF &&
* (HEAD detached at $(but rev-parse --short HEAD^0))
  branch-one
  branch-two
  main
EOF
	but checkout --detach &&
	but branch >actual &&
	test_cmp expect actual
'

test_expect_success 'but branch shows detached HEAD properly after moving' '
	cat >expect <<EOF &&
* (HEAD detached from $(but rev-parse --short HEAD))
  branch-one
  branch-two
  main
EOF
	but reset --hard HEAD^1 &&
	but branch >actual &&
	test_cmp expect actual
'

test_expect_success 'but branch shows detached HEAD properly from tag' '
	cat >expect <<EOF &&
* (HEAD detached at fromtag)
  branch-one
  branch-two
  main
EOF
	but tag fromtag main &&
	but checkout fromtag &&
	but branch >actual &&
	test_cmp expect actual
'

test_expect_success 'but branch shows detached HEAD properly after moving from tag' '
	cat >expect <<EOF &&
* (HEAD detached from fromtag)
  branch-one
  branch-two
  main
EOF
	but reset --hard HEAD^1 &&
	but branch >actual &&
	test_cmp expect actual
'

test_expect_success 'but branch `--sort=[-]objectsize` option' '
	cat >expect <<-\EOF &&
	* (HEAD detached from fromtag)
	  branch-two
	  branch-one
	  main
	EOF
	but branch --sort=objectsize >actual &&
	test_cmp expect actual &&

	cat >expect <<-\EOF &&
	* (HEAD detached from fromtag)
	  branch-one
	  main
	  branch-two
	EOF
	but branch --sort=-objectsize >actual &&
	test_cmp expect actual
'

test_expect_success 'but branch `--sort=[-]type` option' '
	cat >expect <<-\EOF &&
	* (HEAD detached from fromtag)
	  branch-one
	  branch-two
	  main
	EOF
	but branch --sort=type >actual &&
	test_cmp expect actual &&

	cat >expect <<-\EOF &&
	* (HEAD detached from fromtag)
	  branch-one
	  branch-two
	  main
	EOF
	but branch --sort=-type >actual &&
	test_cmp expect actual
'

test_expect_success 'but branch `--sort=[-]version:refname` option' '
	cat >expect <<-\EOF &&
	* (HEAD detached from fromtag)
	  branch-one
	  branch-two
	  main
	EOF
	but branch --sort=version:refname >actual &&
	test_cmp expect actual &&

	cat >expect <<-\EOF &&
	* (HEAD detached from fromtag)
	  main
	  branch-two
	  branch-one
	EOF
	but branch --sort=-version:refname >actual &&
	test_cmp expect actual
'

test_expect_success 'but branch --points-at option' '
	cat >expect <<-\EOF &&
	  branch-one
	  main
	EOF
	but branch --points-at=branch-one >actual &&
	test_cmp expect actual
'

test_expect_success 'ambiguous branch/tag not marked' '
	but tag ambiguous &&
	but branch ambiguous &&
	echo "  ambiguous" >expect &&
	but branch --list ambiguous >actual &&
	test_cmp expect actual
'

test_expect_success 'local-branch symrefs shortened properly' '
	but symbolic-ref refs/heads/ref-to-branch refs/heads/branch-one &&
	but symbolic-ref refs/heads/ref-to-remote refs/remotes/origin/branch-one &&
	cat >expect <<-\EOF &&
	  ref-to-branch -> branch-one
	  ref-to-remote -> origin/branch-one
	EOF
	but branch >actual.raw &&
	grep ref-to <actual.raw >actual &&
	test_cmp expect actual
'

test_expect_success 'sort branches, ignore case' '
	(
		but init -b main sort-icase &&
		cd sort-icase &&
		test_cummit initial &&
		but branch branch-one &&
		but branch BRANCH-two &&
		but branch --list | awk "{print \$NF}" >actual &&
		cat >expected <<-\EOF &&
		BRANCH-two
		branch-one
		main
		EOF
		test_cmp expected actual &&
		but branch --list -i | awk "{print \$NF}" >actual &&
		cat >expected <<-\EOF &&
		branch-one
		BRANCH-two
		main
		EOF
		test_cmp expected actual
	)
'

test_expect_success 'but branch --format option' '
	cat >expect <<-\EOF &&
	Refname is (HEAD detached from fromtag)
	Refname is refs/heads/ambiguous
	Refname is refs/heads/branch-one
	Refname is refs/heads/branch-two
	Refname is refs/heads/main
	Refname is refs/heads/ref-to-branch
	Refname is refs/heads/ref-to-remote
	EOF
	but branch --format="Refname is %(refname)" >actual &&
	test_cmp expect actual
'

test_expect_success 'but branch with --format=%(rest) must fail' '
	test_must_fail but branch --format="%(rest)" >actual
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
	but worktree add worktree_dir branch-two &&
	but branch --color >actual.raw &&
	rm -r worktree_dir &&
	but worktree prune &&
	test_decode_color <actual.raw >actual &&
	test_cmp expect actual
'

test_expect_success "set up color tests" '
	echo "<RED>main<RESET>" >expect.color &&
	echo "main" >expect.bare &&
	color_args="--format=%(color:red)%(refname:short) --list main"
'

test_expect_success '%(color) omitted without tty' '
	TERM=vt100 but branch $color_args >actual.raw &&
	test_decode_color <actual.raw >actual &&
	test_cmp expect.bare actual
'

test_expect_success TTY '%(color) present with tty' '
	test_terminal but branch $color_args >actual.raw &&
	test_decode_color <actual.raw >actual &&
	test_cmp expect.color actual
'

test_expect_success '--color overrides auto-color' '
	but branch --color $color_args >actual.raw &&
	test_decode_color <actual.raw >actual &&
	test_cmp expect.color actual
'

test_expect_success 'verbose output lists worktree path' '
	one=$(but rev-parse --short HEAD) &&
	two=$(but rev-parse --short main) &&
	cat >expect <<-EOF &&
	* (HEAD detached from fromtag) $one one
	  ambiguous                    $one one
	  branch-one                   $two two
	+ branch-two                   $one ($(pwd)/worktree_dir) one
	  main                         $two two
	  ref-to-branch                $two two
	  ref-to-remote                $two two
	EOF
	but worktree add worktree_dir branch-two &&
	but branch -vv >actual &&
	rm -r worktree_dir &&
	but worktree prune &&
	test_cmp expect actual
'

test_done
