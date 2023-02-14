#!/bin/sh

test_description='test git worktree list'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

test_expect_success 'setup' '
	test_commit init
'

test_expect_success 'rev-parse --git-common-dir on main worktree' '
	git rev-parse --git-common-dir >actual &&
	echo .git >expected &&
	test_cmp expected actual &&
	mkdir sub &&
	git -C sub rev-parse --git-common-dir >actual2 &&
	echo ../.git >expected2 &&
	test_cmp expected2 actual2
'

test_expect_success 'rev-parse --git-path objects linked worktree' '
	echo "$(git rev-parse --show-toplevel)/.git/objects" >expect &&
	test_when_finished "rm -rf linked-tree actual expect && git worktree prune" &&
	git worktree add --detach linked-tree main &&
	git -C linked-tree rev-parse --git-path objects >actual &&
	test_cmp expect actual
'

test_expect_success '"list" all worktrees from main' '
	echo "$(git rev-parse --show-toplevel) $(git rev-parse --short HEAD) [$(git symbolic-ref --short HEAD)]" >expect &&
	test_when_finished "rm -rf here out actual expect && git worktree prune" &&
	git worktree add --detach here main &&
	echo "$(git -C here rev-parse --show-toplevel) $(git rev-parse --short HEAD) (detached HEAD)" >>expect &&
	git worktree list >out &&
	sed "s/  */ /g" <out >actual &&
	test_cmp expect actual
'

test_expect_success '"list" all worktrees from linked' '
	echo "$(git rev-parse --show-toplevel) $(git rev-parse --short HEAD) [$(git symbolic-ref --short HEAD)]" >expect &&
	test_when_finished "rm -rf here out actual expect && git worktree prune" &&
	git worktree add --detach here main &&
	echo "$(git -C here rev-parse --show-toplevel) $(git rev-parse --short HEAD) (detached HEAD)" >>expect &&
	git -C here worktree list >out &&
	sed "s/  */ /g" <out >actual &&
	test_cmp expect actual
'

test_expect_success '"list" all worktrees --porcelain' '
	echo "worktree $(git rev-parse --show-toplevel)" >expect &&
	echo "HEAD $(git rev-parse HEAD)" >>expect &&
	echo "branch $(git symbolic-ref HEAD)" >>expect &&
	echo >>expect &&
	test_when_finished "rm -rf here actual expect && git worktree prune" &&
	git worktree add --detach here main &&
	echo "worktree $(git -C here rev-parse --show-toplevel)" >>expect &&
	echo "HEAD $(git rev-parse HEAD)" >>expect &&
	echo "detached" >>expect &&
	echo >>expect &&
	git worktree list --porcelain >actual &&
	test_cmp expect actual
'

test_expect_success '"list" all worktrees --porcelain -z' '
	test_when_finished "rm -rf here _actual actual expect &&
				git worktree prune" &&
	printf "worktree %sQHEAD %sQbranch %sQQ" \
		"$(git rev-parse --show-toplevel)" \
		$(git rev-parse HEAD --symbolic-full-name HEAD) >expect &&
	git worktree add --detach here main &&
	printf "worktree %sQHEAD %sQdetachedQQ" \
		"$(git -C here rev-parse --show-toplevel)" \
		"$(git rev-parse HEAD)" >>expect &&
	git worktree list --porcelain -z >_actual &&
	nul_to_q <_actual >actual &&
	test_cmp expect actual
'

test_expect_success '"list" -z fails without --porcelain' '
	test_must_fail git worktree list -z
'

test_expect_success '"list" all worktrees with locked annotation' '
	test_when_finished "rm -rf locked unlocked out && git worktree prune" &&
	git worktree add --detach locked main &&
	git worktree add --detach unlocked main &&
	git worktree lock locked &&
	test_when_finished "git worktree unlock locked" &&
	git worktree list >out &&
	grep "/locked  *[0-9a-f].* locked$" out &&
	! grep "/unlocked  *[0-9a-f].* locked$" out
'

test_expect_success '"list" all worktrees --porcelain with locked' '
	test_when_finished "rm -rf locked1 locked2 unlocked out actual expect && git worktree prune" &&
	echo "locked" >expect &&
	echo "locked with reason" >>expect &&
	git worktree add --detach locked1 &&
	git worktree add --detach locked2 &&
	# unlocked worktree should not be annotated with "locked"
	git worktree add --detach unlocked &&
	git worktree lock locked1 &&
	test_when_finished "git worktree unlock locked1" &&
	git worktree lock locked2 --reason "with reason" &&
	test_when_finished "git worktree unlock locked2" &&
	git worktree list --porcelain >out &&
	grep "^locked" out >actual &&
	test_cmp expect actual
'

test_expect_success '"list" all worktrees --porcelain with locked reason newline escaped' '
	test_when_finished "rm -rf locked_lf locked_crlf out actual expect && git worktree prune" &&
	printf "locked \"locked\\\\r\\\\nreason\"\n" >expect &&
	printf "locked \"locked\\\\nreason\"\n" >>expect &&
	git worktree add --detach locked_lf &&
	git worktree add --detach locked_crlf &&
	git worktree lock locked_lf --reason "$(printf "locked\nreason")" &&
	test_when_finished "git worktree unlock locked_lf" &&
	git worktree lock locked_crlf --reason "$(printf "locked\r\nreason")" &&
	test_when_finished "git worktree unlock locked_crlf" &&
	git worktree list --porcelain >out &&
	grep "^locked" out >actual &&
	test_cmp expect actual
'

test_expect_success '"list" all worktrees with prunable annotation' '
	test_when_finished "rm -rf prunable unprunable out && git worktree prune" &&
	git worktree add --detach prunable &&
	git worktree add --detach unprunable &&
	rm -rf prunable &&
	git worktree list >out &&
	grep "/prunable  *[0-9a-f].* prunable$" out &&
	! grep "/unprunable  *[0-9a-f].* prunable$"
'

test_expect_success '"list" all worktrees --porcelain with prunable' '
	test_when_finished "rm -rf prunable out && git worktree prune" &&
	git worktree add --detach prunable &&
	rm -rf prunable &&
	git worktree list --porcelain >out &&
	sed -n "/^worktree .*\/prunable$/,/^$/p" <out >only_prunable &&
	test_i18ngrep "^prunable gitdir file points to non-existent location$" only_prunable
'

test_expect_success '"list" all worktrees with prunable consistent with "prune"' '
	test_when_finished "rm -rf prunable unprunable out && git worktree prune" &&
	git worktree add --detach prunable &&
	git worktree add --detach unprunable &&
	rm -rf prunable &&
	git worktree list >out &&
	grep "/prunable  *[0-9a-f].* prunable$" out &&
	! grep "/unprunable  *[0-9a-f].* unprunable$" out &&
	git worktree prune --verbose 2>out &&
	test_i18ngrep "^Removing worktrees/prunable" out &&
	test_i18ngrep ! "^Removing worktrees/unprunable" out
'

test_expect_success '"list" --verbose and --porcelain mutually exclusive' '
	test_must_fail git worktree list --verbose --porcelain
'

test_expect_success '"list" all worktrees --verbose with locked' '
	test_when_finished "rm -rf locked1 locked2 out actual expect && git worktree prune" &&
	git worktree add locked1 --detach &&
	git worktree add locked2 --detach &&
	git worktree lock locked1 &&
	test_when_finished "git worktree unlock locked1" &&
	git worktree lock locked2 --reason "with reason" &&
	test_when_finished "git worktree unlock locked2" &&
	echo "$(git -C locked2 rev-parse --show-toplevel) $(git rev-parse --short HEAD) (detached HEAD)" >expect &&
	printf "\tlocked: with reason\n" >>expect &&
	git worktree list --verbose >out &&
	grep "/locked1  *[0-9a-f].* locked$" out &&
	sed -n "s/  */ /g;/\/locked2  *[0-9a-f].*$/,/locked: .*$/p" <out >actual &&
	test_cmp actual expect
'

test_expect_success '"list" all worktrees --verbose with prunable' '
	test_when_finished "rm -rf prunable out actual expect && git worktree prune" &&
	git worktree add prunable --detach &&
	echo "$(git -C prunable rev-parse --show-toplevel) $(git rev-parse --short HEAD) (detached HEAD)" >expect &&
	printf "\tprunable: gitdir file points to non-existent location\n" >>expect &&
	rm -rf prunable &&
	git worktree list --verbose >out &&
	sed -n "s/  */ /g;/\/prunable  *[0-9a-f].*$/,/prunable: .*$/p" <out >actual &&
	test_cmp actual expect
'

test_expect_success 'bare repo setup' '
	git init --bare bare1 &&
	echo "data" >file1 &&
	git add file1 &&
	git commit -m"File1: add data" &&
	git push bare1 main &&
	git reset --hard HEAD^
'

test_expect_success '"list" all worktrees from bare main' '
	test_when_finished "rm -rf there out actual expect && git -C bare1 worktree prune" &&
	git -C bare1 worktree add --detach ../there main &&
	echo "$(pwd)/bare1 (bare)" >expect &&
	echo "$(git -C there rev-parse --show-toplevel) $(git -C there rev-parse --short HEAD) (detached HEAD)" >>expect &&
	git -C bare1 worktree list >out &&
	sed "s/  */ /g" <out >actual &&
	test_cmp expect actual
'

test_expect_success '"list" all worktrees --porcelain from bare main' '
	test_when_finished "rm -rf there actual expect && git -C bare1 worktree prune" &&
	git -C bare1 worktree add --detach ../there main &&
	echo "worktree $(pwd)/bare1" >expect &&
	echo "bare" >>expect &&
	echo >>expect &&
	echo "worktree $(git -C there rev-parse --show-toplevel)" >>expect &&
	echo "HEAD $(git -C there rev-parse HEAD)" >>expect &&
	echo "detached" >>expect &&
	echo >>expect &&
	git -C bare1 worktree list --porcelain >actual &&
	test_cmp expect actual
'

test_expect_success '"list" all worktrees from linked with a bare main' '
	test_when_finished "rm -rf there out actual expect && git -C bare1 worktree prune" &&
	git -C bare1 worktree add --detach ../there main &&
	echo "$(pwd)/bare1 (bare)" >expect &&
	echo "$(git -C there rev-parse --show-toplevel) $(git -C there rev-parse --short HEAD) (detached HEAD)" >>expect &&
	git -C there worktree list >out &&
	sed "s/  */ /g" <out >actual &&
	test_cmp expect actual
'

test_expect_success 'bare repo cleanup' '
	rm -rf bare1
'

test_expect_success 'broken main worktree still at the top' '
	git init broken-main &&
	(
		cd broken-main &&
		test_commit new &&
		git worktree add linked &&
		cat >expected <<-EOF &&
		worktree $(pwd)
		HEAD $ZERO_OID

		EOF
		cd linked &&
		echo "worktree $(pwd)" >expected &&
		(cd ../ && test-tool ref-store main create-symref HEAD .broken ) &&
		git worktree list --porcelain >out &&
		head -n 3 out >actual &&
		test_cmp ../expected actual &&
		git worktree list >out &&
		head -n 1 out >actual.2 &&
		grep -F "(error)" actual.2
	)
'

test_expect_success 'linked worktrees are sorted' '
	mkdir sorted &&
	git init sorted/main &&
	(
		cd sorted/main &&
		test_tick &&
		test_commit new &&
		git worktree add ../first &&
		git worktree add ../second &&
		git worktree list --porcelain >out &&
		grep ^worktree out >actual
	) &&
	cat >expected <<-EOF &&
	worktree $(pwd)/sorted/main
	worktree $(pwd)/sorted/first
	worktree $(pwd)/sorted/second
	EOF
	test_cmp expected sorted/main/actual
'

test_expect_success 'worktree path when called in .git directory' '
	git worktree list >list1 &&
	git -C .git worktree list >list2 &&
	test_cmp list1 list2
'

test_done
