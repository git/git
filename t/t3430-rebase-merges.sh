#!/bin/sh
#
# Copyright (c) 2018 Johannes E. Schindelin
#

test_description='git rebase -i --rebase-merges

This test runs git rebase "interactively", retaining the branch structure by
recreating merge commits.

Initial setup:

    -- B --                   (first)
   /       \
 A - C - D - E - H            (master)
       \       /
         F - G                (second)
'
. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-rebase.sh

test_cmp_graph () {
	cat >expect &&
	git log --graph --boundary --format=%s "$@" >output &&
	sed "s/ *$//" <output >output.trimmed &&
	test_cmp expect output.trimmed
}

test_expect_success 'setup' '
	write_script replace-editor.sh <<-\EOF &&
	mv "$1" "$(git rev-parse --git-path ORIGINAL-TODO)"
	cp script-from-scratch "$1"
	EOF

	test_commit A &&
	git checkout -b first &&
	test_commit B &&
	git checkout master &&
	test_commit C &&
	test_commit D &&
	git merge --no-commit B &&
	test_tick &&
	git commit -m E &&
	git tag -m E E &&
	git checkout -b second C &&
	test_commit F &&
	test_commit G &&
	git checkout master &&
	git merge --no-commit G &&
	test_tick &&
	git commit -m H &&
	git tag -m H H
'

test_expect_success 'create completely different structure' '
	cat >script-from-scratch <<-\EOF &&
	label onto

	# onebranch
	pick G
	pick D
	label onebranch

	# second
	reset onto
	pick B
	label second

	reset onto
	merge -C H second
	merge onebranch # Merge the topic branch '\''onebranch'\''
	EOF
	test_config sequence.editor \""$PWD"/replace-editor.sh\" &&
	test_tick &&
	git rebase -i -r A &&
	test_cmp_graph <<-\EOF
	*   Merge the topic branch '\''onebranch'\''
	|\
	| * D
	| * G
	* |   H
	|\ \
	| |/
	|/|
	| * B
	|/
	* A
	EOF
'

test_expect_success 'generate correct todo list' '
	cat >expect <<-\EOF &&
	label onto

	reset onto
	pick d9df450 B
	label E

	reset onto
	pick 5dee784 C
	label branch-point
	pick ca2c861 F
	pick 088b00a G
	label H

	reset branch-point # C
	pick 12bd07b D
	merge -C 2051b56 E # E
	merge -C 233d48a H # H

	EOF

	grep -v "^#" <.git/ORIGINAL-TODO >output &&
	test_cmp expect output
'

test_expect_success '`reset` refuses to overwrite untracked files' '
	git checkout -b refuse-to-reset &&
	test_commit dont-overwrite-untracked &&
	git checkout @{-1} &&
	: >dont-overwrite-untracked.t &&
	echo "reset refs/tags/dont-overwrite-untracked" >script-from-scratch &&
	test_config sequence.editor \""$PWD"/replace-editor.sh\" &&
	test_must_fail git rebase -r HEAD &&
	git rebase --abort
'

test_expect_success 'failed `merge` writes patch (may be rescheduled, too)' '
	test_when_finished "test_might_fail git rebase --abort" &&
	git checkout -b conflicting-merge A &&

	: fail because of conflicting untracked file &&
	>G.t &&
	echo "merge -C H G" >script-from-scratch &&
	test_config sequence.editor \""$PWD"/replace-editor.sh\" &&
	test_tick &&
	test_must_fail git rebase -ir HEAD &&
	grep "^merge -C .* G$" .git/rebase-merge/done &&
	grep "^merge -C .* G$" .git/rebase-merge/git-rebase-todo &&
	test_path_is_file .git/rebase-merge/patch &&

	: fail because of merge conflict &&
	rm G.t .git/rebase-merge/patch &&
	git reset --hard &&
	test_commit conflicting-G G.t not-G conflicting-G &&
	test_must_fail git rebase --continue &&
	! grep "^merge -C .* G$" .git/rebase-merge/git-rebase-todo &&
	test_path_is_file .git/rebase-merge/patch
'

test_expect_success 'with a branch tip that was cherry-picked already' '
	git checkout -b already-upstream master &&
	base="$(git rev-parse --verify HEAD)" &&

	test_commit A1 &&
	test_commit A2 &&
	git reset --hard $base &&
	test_commit B1 &&
	test_tick &&
	git merge -m "Merge branch A" A2 &&

	git checkout -b upstream-with-a2 $base &&
	test_tick &&
	git cherry-pick A2 &&

	git checkout already-upstream &&
	test_tick &&
	git rebase -i -r upstream-with-a2 &&
	test_cmp_graph upstream-with-a2.. <<-\EOF
	*   Merge branch A
	|\
	| * A1
	* | B1
	|/
	o A2
	EOF
'

test_expect_success 'do not rebase cousins unless asked for' '
	git checkout -b cousins master &&
	before="$(git rev-parse --verify HEAD)" &&
	test_tick &&
	git rebase -r HEAD^ &&
	test_cmp_rev HEAD $before &&
	test_tick &&
	git rebase --rebase-merges=rebase-cousins HEAD^ &&
	test_cmp_graph HEAD^.. <<-\EOF
	*   Merge the topic branch '\''onebranch'\''
	|\
	| * D
	| * G
	|/
	o H
	EOF
'

test_expect_success 'refs/rewritten/* is worktree-local' '
	git worktree add wt &&
	cat >wt/script-from-scratch <<-\EOF &&
	label xyz
	exec GIT_DIR=../.git git rev-parse --verify refs/rewritten/xyz >a || :
	exec git rev-parse --verify refs/rewritten/xyz >b
	EOF

	test_config -C wt sequence.editor \""$PWD"/replace-editor.sh\" &&
	git -C wt rebase -i HEAD &&
	test_must_be_empty wt/a &&
	test_cmp_rev HEAD "$(cat wt/b)"
'

test_expect_success 'post-rewrite hook and fixups work for merges' '
	git checkout -b post-rewrite &&
	test_commit same1 &&
	git reset --hard HEAD^ &&
	test_commit same2 &&
	git merge -m "to fix up" same1 &&
	echo same old same old >same2.t &&
	test_tick &&
	git commit --fixup HEAD same2.t &&
	fixup="$(git rev-parse HEAD)" &&

	mkdir -p .git/hooks &&
	test_when_finished "rm .git/hooks/post-rewrite" &&
	echo "cat >actual" | write_script .git/hooks/post-rewrite &&

	test_tick &&
	git rebase -i --autosquash -r HEAD^^^ &&
	printf "%s %s\n%s %s\n%s %s\n%s %s\n" >expect $(git rev-parse \
		$fixup^^2 HEAD^2 \
		$fixup^^ HEAD^ \
		$fixup^ HEAD \
		$fixup HEAD) &&
	test_cmp expect actual
'

test_expect_success 'refuse to merge ancestors of HEAD' '
	echo "merge HEAD^" >script-from-scratch &&
	test_config -C wt sequence.editor \""$PWD"/replace-editor.sh\" &&
	before="$(git rev-parse HEAD)" &&
	git rebase -i HEAD &&
	test_cmp_rev HEAD $before
'

test_done
