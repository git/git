#!/bin/sh

test_description='magic pathspec tests using git-log'

. ./test-lib.sh

test_expect_success 'setup' '
	test_commit initial &&
	test_tick &&
	git commit --allow-empty -m empty &&
	mkdir sub
'

test_expect_success '"git log :/" should not be ambiguous' '
	git log :/
'

test_expect_success '"git log :/a" should be ambiguous (applied both rev and worktree)' '
	: >a &&
	test_must_fail git log :/a 2>error &&
	test_i18ngrep ambiguous error
'

test_expect_success '"git log :/a -- " should not be ambiguous' '
	git log :/a --
'

test_expect_success '"git log :/detached -- " should find a commit only in HEAD' '
	test_when_finished "git checkout master" &&
	git checkout --detach &&
	# Must manually call `test_tick` instead of using `test_commit`,
	# because the latter additionally creates a tag, which would make
	# the commit reachable not only via HEAD.
	test_tick &&
	git commit --allow-empty -m detached &&
	test_tick &&
	git commit --allow-empty -m something-else &&
	git log :/detached --
'

test_expect_success '"git log :/detached -- " should not find an orphaned commit' '
	test_must_fail git log :/detached --
'

test_expect_success '"git log :/detached -- " should find HEAD only of own worktree' '
	git worktree add other-tree HEAD &&
	git -C other-tree checkout --detach &&
	test_tick &&
	git -C other-tree commit --allow-empty -m other-detached &&
	git -C other-tree log :/other-detached -- &&
	test_must_fail git log :/other-detached --
'

test_expect_success '"git log -- :/a" should not be ambiguous' '
	git log -- :/a
'

# This differs from the ":/a" check above in that :/in looks like a pathspec,
# but doesn't match an actual file.
test_expect_success '"git log :/in" should not be ambiguous' '
	git log :/in
'

test_expect_success '"git log :" should be ambiguous' '
	test_must_fail git log : 2>error &&
	test_i18ngrep ambiguous error
'

test_expect_success 'git log -- :' '
	git log -- :
'

test_expect_success 'git log HEAD -- :/' '
	initial=$(git rev-parse --short HEAD^) &&
	cat >expected <<-EOF &&
	$initial initial
	EOF
	(cd sub && git log --oneline HEAD -- :/ >../actual) &&
	test_cmp expected actual
'

test_expect_success '"git log :^sub" is not ambiguous' '
	git log :^sub
'

test_expect_success '"git log :^does-not-exist" does not match anything' '
	test_must_fail git log :^does-not-exist
'

test_expect_success  '"git log :!" behaves the same as :^' '
	git log :!sub &&
	test_must_fail git log :!does-not-exist
'

test_expect_success '"git log :(exclude)sub" is not ambiguous' '
	git log ":(exclude)sub"
'

test_expect_success '"git log :(exclude)sub --" must resolve as an object' '
	test_must_fail git log ":(exclude)sub" --
'

test_expect_success '"git log :(unknown-magic) complains of bogus magic' '
	test_must_fail git log ":(unknown-magic)" 2>error &&
	test_i18ngrep pathspec.magic error
'

test_expect_success 'command line pathspec parsing for "git log"' '
	git reset --hard &&
	>a &&
	git add a &&
	git commit -m "add an empty a" --allow-empty &&
	echo 1 >a &&
	git commit -a -m "update a to 1" &&
	git checkout HEAD^ &&
	echo 2 >a &&
	git commit -a -m "update a to 2" &&
	test_must_fail git merge master &&
	git add a &&
	git log --merge -- a
'

test_expect_success 'tree_entry_interesting does not match past submodule boundaries' '
	test_when_finished "rm -rf repo submodule" &&
	git init submodule &&
	test_commit -C submodule initial &&
	git init repo &&
	>"repo/[bracket]" &&
	git -C repo add "[bracket]" &&
	test_tick &&
	git -C repo commit -m bracket &&
	git -C repo rev-list HEAD -- "[bracket]" >expect &&

	git -C repo submodule add ../submodule &&
	test_tick &&
	git -C repo commit -m submodule &&

	git -C repo rev-list HEAD -- "[bracket]" >actual &&
	test_cmp expect actual
'

test_done
