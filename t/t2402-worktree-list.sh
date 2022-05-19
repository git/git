#!/bin/sh

test_description='test but worktree list'

BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

test_expect_success 'setup' '
	test_cummit init
'

test_expect_success 'rev-parse --but-common-dir on main worktree' '
	but rev-parse --but-common-dir >actual &&
	echo .but >expected &&
	test_cmp expected actual &&
	mkdir sub &&
	but -C sub rev-parse --but-common-dir >actual2 &&
	echo ../.but >expected2 &&
	test_cmp expected2 actual2
'

test_expect_success 'rev-parse --but-path objects linked worktree' '
	echo "$(but rev-parse --show-toplevel)/.but/objects" >expect &&
	test_when_finished "rm -rf linked-tree actual expect && but worktree prune" &&
	but worktree add --detach linked-tree main &&
	but -C linked-tree rev-parse --but-path objects >actual &&
	test_cmp expect actual
'

test_expect_success '"list" all worktrees from main' '
	echo "$(but rev-parse --show-toplevel) $(but rev-parse --short HEAD) [$(but symbolic-ref --short HEAD)]" >expect &&
	test_when_finished "rm -rf here out actual expect && but worktree prune" &&
	but worktree add --detach here main &&
	echo "$(but -C here rev-parse --show-toplevel) $(but rev-parse --short HEAD) (detached HEAD)" >>expect &&
	but worktree list >out &&
	sed "s/  */ /g" <out >actual &&
	test_cmp expect actual
'

test_expect_success '"list" all worktrees from linked' '
	echo "$(but rev-parse --show-toplevel) $(but rev-parse --short HEAD) [$(but symbolic-ref --short HEAD)]" >expect &&
	test_when_finished "rm -rf here out actual expect && but worktree prune" &&
	but worktree add --detach here main &&
	echo "$(but -C here rev-parse --show-toplevel) $(but rev-parse --short HEAD) (detached HEAD)" >>expect &&
	but -C here worktree list >out &&
	sed "s/  */ /g" <out >actual &&
	test_cmp expect actual
'

test_expect_success '"list" all worktrees --porcelain' '
	echo "worktree $(but rev-parse --show-toplevel)" >expect &&
	echo "HEAD $(but rev-parse HEAD)" >>expect &&
	echo "branch $(but symbolic-ref HEAD)" >>expect &&
	echo >>expect &&
	test_when_finished "rm -rf here actual expect && but worktree prune" &&
	but worktree add --detach here main &&
	echo "worktree $(but -C here rev-parse --show-toplevel)" >>expect &&
	echo "HEAD $(but rev-parse HEAD)" >>expect &&
	echo "detached" >>expect &&
	echo >>expect &&
	but worktree list --porcelain >actual &&
	test_cmp expect actual
'

test_expect_success '"list" all worktrees --porcelain -z' '
	test_when_finished "rm -rf here _actual actual expect &&
				but worktree prune" &&
	printf "worktree %sQHEAD %sQbranch %sQQ" \
		"$(but rev-parse --show-toplevel)" \
		$(but rev-parse HEAD --symbolic-full-name HEAD) >expect &&
	but worktree add --detach here main &&
	printf "worktree %sQHEAD %sQdetachedQQ" \
		"$(but -C here rev-parse --show-toplevel)" \
		"$(but rev-parse HEAD)" >>expect &&
	but worktree list --porcelain -z >_actual &&
	nul_to_q <_actual >actual &&
	test_cmp expect actual
'

test_expect_success '"list" -z fails without --porcelain' '
	test_must_fail but worktree list -z
'

test_expect_success '"list" all worktrees with locked annotation' '
	test_when_finished "rm -rf locked unlocked out && but worktree prune" &&
	but worktree add --detach locked main &&
	but worktree add --detach unlocked main &&
	but worktree lock locked &&
	test_when_finished "but worktree unlock locked" &&
	but worktree list >out &&
	grep "/locked  *[0-9a-f].* locked$" out &&
	! grep "/unlocked  *[0-9a-f].* locked$" out
'

test_expect_success '"list" all worktrees --porcelain with locked' '
	test_when_finished "rm -rf locked1 locked2 unlocked out actual expect && but worktree prune" &&
	echo "locked" >expect &&
	echo "locked with reason" >>expect &&
	but worktree add --detach locked1 &&
	but worktree add --detach locked2 &&
	# unlocked worktree should not be annotated with "locked"
	but worktree add --detach unlocked &&
	but worktree lock locked1 &&
	test_when_finished "but worktree unlock locked1" &&
	but worktree lock locked2 --reason "with reason" &&
	test_when_finished "but worktree unlock locked2" &&
	but worktree list --porcelain >out &&
	grep "^locked" out >actual &&
	test_cmp expect actual
'

test_expect_success '"list" all worktrees --porcelain with locked reason newline escaped' '
	test_when_finished "rm -rf locked_lf locked_crlf out actual expect && but worktree prune" &&
	printf "locked \"locked\\\\r\\\\nreason\"\n" >expect &&
	printf "locked \"locked\\\\nreason\"\n" >>expect &&
	but worktree add --detach locked_lf &&
	but worktree add --detach locked_crlf &&
	but worktree lock locked_lf --reason "$(printf "locked\nreason")" &&
	test_when_finished "but worktree unlock locked_lf" &&
	but worktree lock locked_crlf --reason "$(printf "locked\r\nreason")" &&
	test_when_finished "but worktree unlock locked_crlf" &&
	but worktree list --porcelain >out &&
	grep "^locked" out >actual &&
	test_cmp expect actual
'

test_expect_success '"list" all worktrees with prunable annotation' '
	test_when_finished "rm -rf prunable unprunable out && but worktree prune" &&
	but worktree add --detach prunable &&
	but worktree add --detach unprunable &&
	rm -rf prunable &&
	but worktree list >out &&
	grep "/prunable  *[0-9a-f].* prunable$" out &&
	! grep "/unprunable  *[0-9a-f].* prunable$"
'

test_expect_success '"list" all worktrees --porcelain with prunable' '
	test_when_finished "rm -rf prunable out && but worktree prune" &&
	but worktree add --detach prunable &&
	rm -rf prunable &&
	but worktree list --porcelain >out &&
	sed -n "/^worktree .*\/prunable$/,/^$/p" <out >only_prunable &&
	test_i18ngrep "^prunable butdir file points to non-existent location$" only_prunable
'

test_expect_success '"list" all worktrees with prunable consistent with "prune"' '
	test_when_finished "rm -rf prunable unprunable out && but worktree prune" &&
	but worktree add --detach prunable &&
	but worktree add --detach unprunable &&
	rm -rf prunable &&
	but worktree list >out &&
	grep "/prunable  *[0-9a-f].* prunable$" out &&
	! grep "/unprunable  *[0-9a-f].* unprunable$" out &&
	but worktree prune --verbose 2>out &&
	test_i18ngrep "^Removing worktrees/prunable" out &&
	test_i18ngrep ! "^Removing worktrees/unprunable" out
'

test_expect_success '"list" --verbose and --porcelain mutually exclusive' '
	test_must_fail but worktree list --verbose --porcelain
'

test_expect_success '"list" all worktrees --verbose with locked' '
	test_when_finished "rm -rf locked1 locked2 out actual expect && but worktree prune" &&
	but worktree add locked1 --detach &&
	but worktree add locked2 --detach &&
	but worktree lock locked1 &&
	test_when_finished "but worktree unlock locked1" &&
	but worktree lock locked2 --reason "with reason" &&
	test_when_finished "but worktree unlock locked2" &&
	echo "$(but -C locked2 rev-parse --show-toplevel) $(but rev-parse --short HEAD) (detached HEAD)" >expect &&
	printf "\tlocked: with reason\n" >>expect &&
	but worktree list --verbose >out &&
	grep "/locked1  *[0-9a-f].* locked$" out &&
	sed -n "s/  */ /g;/\/locked2  *[0-9a-f].*$/,/locked: .*$/p" <out >actual &&
	test_cmp actual expect
'

test_expect_success '"list" all worktrees --verbose with prunable' '
	test_when_finished "rm -rf prunable out actual expect && but worktree prune" &&
	but worktree add prunable --detach &&
	echo "$(but -C prunable rev-parse --show-toplevel) $(but rev-parse --short HEAD) (detached HEAD)" >expect &&
	printf "\tprunable: butdir file points to non-existent location\n" >>expect &&
	rm -rf prunable &&
	but worktree list --verbose >out &&
	sed -n "s/  */ /g;/\/prunable  *[0-9a-f].*$/,/prunable: .*$/p" <out >actual &&
	test_cmp actual expect
'

test_expect_success 'bare repo setup' '
	but init --bare bare1 &&
	echo "data" >file1 &&
	but add file1 &&
	but cummit -m"File1: add data" &&
	but push bare1 main &&
	but reset --hard HEAD^
'

test_expect_success '"list" all worktrees from bare main' '
	test_when_finished "rm -rf there out actual expect && but -C bare1 worktree prune" &&
	but -C bare1 worktree add --detach ../there main &&
	echo "$(pwd)/bare1 (bare)" >expect &&
	echo "$(but -C there rev-parse --show-toplevel) $(but -C there rev-parse --short HEAD) (detached HEAD)" >>expect &&
	but -C bare1 worktree list >out &&
	sed "s/  */ /g" <out >actual &&
	test_cmp expect actual
'

test_expect_success '"list" all worktrees --porcelain from bare main' '
	test_when_finished "rm -rf there actual expect && but -C bare1 worktree prune" &&
	but -C bare1 worktree add --detach ../there main &&
	echo "worktree $(pwd)/bare1" >expect &&
	echo "bare" >>expect &&
	echo >>expect &&
	echo "worktree $(but -C there rev-parse --show-toplevel)" >>expect &&
	echo "HEAD $(but -C there rev-parse HEAD)" >>expect &&
	echo "detached" >>expect &&
	echo >>expect &&
	but -C bare1 worktree list --porcelain >actual &&
	test_cmp expect actual
'

test_expect_success '"list" all worktrees from linked with a bare main' '
	test_when_finished "rm -rf there out actual expect && but -C bare1 worktree prune" &&
	but -C bare1 worktree add --detach ../there main &&
	echo "$(pwd)/bare1 (bare)" >expect &&
	echo "$(but -C there rev-parse --show-toplevel) $(but -C there rev-parse --short HEAD) (detached HEAD)" >>expect &&
	but -C there worktree list >out &&
	sed "s/  */ /g" <out >actual &&
	test_cmp expect actual
'

test_expect_success 'bare repo cleanup' '
	rm -rf bare1
'

test_expect_success 'broken main worktree still at the top' '
	but init broken-main &&
	(
		cd broken-main &&
		test_cummit new &&
		but worktree add linked &&
		cat >expected <<-EOF &&
		worktree $(pwd)
		HEAD $ZERO_OID

		EOF
		cd linked &&
		echo "worktree $(pwd)" >expected &&
		(cd ../ && test-tool ref-store main create-symref HEAD .broken ) &&
		but worktree list --porcelain >out &&
		head -n 3 out >actual &&
		test_cmp ../expected actual &&
		but worktree list >out &&
		head -n 1 out >actual.2 &&
		grep -F "(error)" actual.2
	)
'

test_expect_success 'linked worktrees are sorted' '
	mkdir sorted &&
	but init sorted/main &&
	(
		cd sorted/main &&
		test_tick &&
		test_cummit new &&
		but worktree add ../first &&
		but worktree add ../second &&
		but worktree list --porcelain >out &&
		grep ^worktree out >actual
	) &&
	cat >expected <<-EOF &&
	worktree $(pwd)/sorted/main
	worktree $(pwd)/sorted/first
	worktree $(pwd)/sorted/second
	EOF
	test_cmp expected sorted/main/actual
'

test_expect_success 'worktree path when called in .but directory' '
	but worktree list >list1 &&
	but -C .but worktree list >list2 &&
	test_cmp list1 list2
'

test_done
