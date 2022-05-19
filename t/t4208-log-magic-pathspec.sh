#!/bin/sh

test_description='magic pathspec tests using but-log'

BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

test_expect_success 'setup' '
	test_cummit initial &&
	test_tick &&
	but cummit --allow-empty -m empty &&
	mkdir sub
'

test_expect_success '"but log :/" should not be ambiguous' '
	but log :/
'

test_expect_success '"but log :/a" should be ambiguous (applied both rev and worktree)' '
	: >a &&
	test_must_fail but log :/a 2>error &&
	test_i18ngrep ambiguous error
'

test_expect_success '"but log :/a -- " should not be ambiguous' '
	but log :/a --
'

test_expect_success '"but log :/detached -- " should find a cummit only in HEAD' '
	test_when_finished "but checkout main" &&
	but checkout --detach &&
	test_cummit --no-tag detached &&
	test_cummit --no-tag something-else &&
	but log :/detached --
'

test_expect_success '"but log :/detached -- " should not find an orphaned cummit' '
	test_must_fail but log :/detached --
'

test_expect_success '"but log :/detached -- " should find HEAD only of own worktree' '
	but worktree add other-tree HEAD &&
	but -C other-tree checkout --detach &&
	test_tick &&
	but -C other-tree cummit --allow-empty -m other-detached &&
	but -C other-tree log :/other-detached -- &&
	test_must_fail but log :/other-detached --
'

test_expect_success '"but log -- :/a" should not be ambiguous' '
	but log -- :/a
'

test_expect_success '"but log :/any/path/" should not segfault' '
	test_must_fail but log :/any/path/
'

# This differs from the ":/a" check above in that :/in looks like a pathspec,
# but doesn't match an actual file.
test_expect_success '"but log :/in" should not be ambiguous' '
	but log :/in
'

test_expect_success '"but log :" should be ambiguous' '
	test_must_fail but log : 2>error &&
	test_i18ngrep ambiguous error
'

test_expect_success 'but log -- :' '
	but log -- :
'

test_expect_success 'but log HEAD -- :/' '
	initial=$(but rev-parse --short HEAD^) &&
	cat >expected <<-EOF &&
	$initial initial
	EOF
	(cd sub && but log --oneline HEAD -- :/ >../actual) &&
	test_cmp expected actual
'

test_expect_success '"but log :^sub" is not ambiguous' '
	but log :^sub
'

test_expect_success '"but log :^does-not-exist" does not match anything' '
	test_must_fail but log :^does-not-exist
'

test_expect_success  '"but log :!" behaves the same as :^' '
	but log :!sub &&
	test_must_fail but log :!does-not-exist
'

test_expect_success '"but log :(exclude)sub" is not ambiguous' '
	but log ":(exclude)sub"
'

test_expect_success '"but log :(exclude)sub --" must resolve as an object' '
	test_must_fail but log ":(exclude)sub" --
'

test_expect_success '"but log :(unknown-magic) complains of bogus magic' '
	test_must_fail but log ":(unknown-magic)" 2>error &&
	test_i18ngrep pathspec.magic error
'

test_expect_success 'command line pathspec parsing for "but log"' '
	but reset --hard &&
	>a &&
	but add a &&
	but cummit -m "add an empty a" --allow-empty &&
	echo 1 >a &&
	but cummit -a -m "update a to 1" &&
	but checkout HEAD^ &&
	echo 2 >a &&
	but cummit -a -m "update a to 2" &&
	test_must_fail but merge main &&
	but add a &&
	but log --merge -- a
'

test_expect_success 'tree_entry_interesting does not match past submodule boundaries' '
	test_when_finished "rm -rf repo submodule" &&
	but init submodule &&
	test_cummit -C submodule initial &&
	but init repo &&
	>"repo/[bracket]" &&
	but -C repo add "[bracket]" &&
	test_tick &&
	but -C repo cummit -m bracket &&
	but -C repo rev-list HEAD -- "[bracket]" >expect &&

	but -C repo submodule add ../submodule &&
	test_tick &&
	but -C repo cummit -m submodule &&

	but -C repo rev-list HEAD -- "[bracket]" >actual &&
	test_cmp expect actual
'

test_done
