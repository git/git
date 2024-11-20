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
 A - C - D - E - H            (main)
   \    \       /
    \    F - G                (second)
     \
      Conflicting-G
'
GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-rebase.sh
. "$TEST_DIRECTORY"/lib-log-graph.sh

test_cmp_graph () {
	cat >expect &&
	lib_test_cmp_graph --boundary --format=%s "$@"
}

test_expect_success 'setup' '
	write_script replace-editor.sh <<-\EOF &&
	mv "$1" "$(git rev-parse --git-path ORIGINAL-TODO)"
	cp script-from-scratch "$1"
	EOF

	test_commit A &&
	git checkout -b first &&
	test_commit B &&
	b=$(git rev-parse --short HEAD) &&
	git checkout main &&
	test_commit C &&
	c=$(git rev-parse --short HEAD) &&
	test_commit D &&
	d=$(git rev-parse --short HEAD) &&
	git merge --no-commit B &&
	test_tick &&
	git commit -m E &&
	git tag -m E E &&
	e=$(git rev-parse --short HEAD) &&
	git checkout -b second C &&
	test_commit F &&
	f=$(git rev-parse --short HEAD) &&
	test_commit G &&
	g=$(git rev-parse --short HEAD) &&
	git checkout main &&
	git merge --no-commit G &&
	test_tick &&
	git commit -m H &&
	h=$(git rev-parse --short HEAD) &&
	git tag -m H H &&
	git checkout A &&
	test_commit conflicting-G G.t
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
	git rebase -i -r A main &&
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
	cat >expect <<-EOF &&
	label onto

	reset onto
	pick $b B
	label first

	reset onto
	pick $c C
	label branch-point
	pick $f F
	pick $g G
	label second

	reset branch-point # C
	pick $d D
	merge -C $e first # E
	merge -C $h second # H

	EOF

	grep -v "^#" <.git/ORIGINAL-TODO >output &&
	test_cmp expect output
'

test_expect_success '`reset` refuses to overwrite untracked files' '
	git checkout B &&
	test_commit dont-overwrite-untracked &&
	cat >script-from-scratch <<-EOF &&
	exec >dont-overwrite-untracked.t
	pick $(git rev-parse B) B
	reset refs/tags/dont-overwrite-untracked
	pick $(git rev-parse C) C
	exec cat .git/rebase-merge/done >actual
	EOF
	test_config sequence.editor \""$PWD"/replace-editor.sh\" &&
	test_must_fail git rebase -ir A &&
	test_cmp_rev HEAD B &&
	head -n3 script-from-scratch >expect &&
	test_cmp expect .git/rebase-merge/done &&
	rm dont-overwrite-untracked.t &&
	git rebase --continue &&
	tail -n3 script-from-scratch >>expect &&
	test_cmp expect actual
'

test_expect_success '`reset` rejects trees' '
	test_when_finished "test_might_fail git rebase --abort" &&
	test_must_fail env GIT_SEQUENCE_EDITOR="echo reset A^{tree} >" \
		git rebase -i B C >out 2>err &&
	grep "object .* is a tree" err &&
	test_must_be_empty out
'

test_expect_success '`reset` only looks for labels under refs/rewritten/' '
	test_when_finished "test_might_fail git rebase --abort" &&
	git branch refs/rewritten/my-label A &&
	test_must_fail env GIT_SEQUENCE_EDITOR="echo reset my-label >" \
		git rebase -i B C >out 2>err &&
	grep "could not resolve ${SQ}my-label${SQ}" err &&
	test_must_be_empty out
'

test_expect_success 'failed `merge -C` writes patch (may be rescheduled, too)' '
	test_when_finished "test_might_fail git rebase --abort" &&
	git checkout -b conflicting-merge A &&

	: fail because of conflicting untracked file &&
	>G.t &&
	echo "merge -C H G" >script-from-scratch &&
	test_config sequence.editor \""$PWD"/replace-editor.sh\" &&
	test_tick &&
	test_must_fail git rebase -ir HEAD &&
	test_cmp_rev REBASE_HEAD H^0 &&
	grep "^merge -C .* G$" .git/rebase-merge/done &&
	grep "^merge -C .* G$" .git/rebase-merge/git-rebase-todo &&
	test_path_is_missing .git/rebase-merge/patch &&
	echo changed >file1 &&
	git add file1 &&
	test_must_fail git rebase --continue 2>err &&
	grep "error: you have staged changes in your working tree" err &&

	: fail because of merge conflict &&
	git reset --hard conflicting-G &&
	test_must_fail git rebase --continue &&
	! grep "^merge -C .* G$" .git/rebase-merge/git-rebase-todo &&
	test_path_is_file .git/rebase-merge/patch
'

test_expect_success 'failed `merge <branch>` does not crash' '
	test_when_finished "test_might_fail git rebase --abort" &&
	git checkout conflicting-G &&

	echo "merge G" >script-from-scratch &&
	test_config sequence.editor \""$PWD"/replace-editor.sh\" &&
	test_tick &&
	test_must_fail git rebase -ir HEAD &&
	! grep "^merge G$" .git/rebase-merge/git-rebase-todo &&
	grep "^Merge branch ${SQ}G${SQ}$" .git/rebase-merge/message
'

test_expect_success 'merge -c commits before rewording and reloads todo-list' '
	cat >script-from-scratch <<-\EOF &&
	merge -c E B
	merge -c H G
	EOF

	git checkout -b merge-c H &&
	(
		set_reword_editor &&
		GIT_SEQUENCE_EDITOR="\"$PWD/replace-editor.sh\"" \
			git rebase -i -r D
	) &&
	check_reworded_commits E H
'

test_expect_success 'merge -c rewords when a strategy is given' '
	git checkout -b merge-c-with-strategy H &&
	write_script git-merge-override <<-\EOF &&
	echo overridden$1 >G.t
	git add G.t
	EOF

	PATH="$PWD:$PATH" \
	GIT_SEQUENCE_EDITOR="echo merge -c H G >" \
	GIT_EDITOR="echo edited >>" \
		git rebase --no-ff -ir -s override -Xxopt E &&
	test_write_lines overridden--xopt >expect &&
	test_cmp expect G.t &&
	test_write_lines H "" edited "" >expect &&
	git log --format=%B -1 >actual &&
	test_cmp expect actual

'
test_expect_success 'with a branch tip that was cherry-picked already' '
	git checkout -b already-upstream main &&
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

test_expect_success '--no-rebase-merges countermands --rebase-merges' '
	git checkout -b no-rebase-merges E &&
	git rebase --rebase-merges --no-rebase-merges C &&
	test_cmp_graph C.. <<-\EOF
	* B
	* D
	o C
	EOF
'

test_expect_success 'do not rebase cousins unless asked for' '
	git checkout -b cousins main &&
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

test_expect_success 'rebase.rebaseMerges=rebase-cousins is equivalent to --rebase-merges=rebase-cousins' '
	test_config rebase.rebaseMerges rebase-cousins &&
	git checkout -b config-rebase-cousins main &&
	git rebase HEAD^ &&
	test_cmp_graph HEAD^.. <<-\EOF
	*   Merge the topic branch '\''onebranch'\''
	|\
	| * D
	| * G
	|/
	o H
	EOF
'

test_expect_success '--no-rebase-merges overrides rebase.rebaseMerges=no-rebase-cousins' '
	test_config rebase.rebaseMerges no-rebase-cousins &&
	git checkout -b override-config-no-rebase-cousins E &&
	git rebase --no-rebase-merges C &&
	test_cmp_graph C.. <<-\EOF
	* B
	* D
	o C
	EOF
'

test_expect_success '--rebase-merges overrides rebase.rebaseMerges=rebase-cousins' '
	test_config rebase.rebaseMerges rebase-cousins &&
	git checkout -b override-config-rebase-cousins E &&
	before="$(git rev-parse --verify HEAD)" &&
	test_tick &&
	git rebase --rebase-merges C &&
	test_cmp_rev HEAD $before
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

test_expect_success '--abort cleans up refs/rewritten' '
	git checkout -b abort-cleans-refs-rewritten H &&
	GIT_SEQUENCE_EDITOR="echo break >>" git rebase -ir @^ &&
	git rev-parse --verify refs/rewritten/onto &&
	git rebase --abort &&
	test_must_fail git rev-parse --verify refs/rewritten/onto
'

test_expect_success '--quit cleans up refs/rewritten' '
	git checkout -b quit-cleans-refs-rewritten H &&
	GIT_SEQUENCE_EDITOR="echo break >>" git rebase -ir @^ &&
	git rev-parse --verify refs/rewritten/onto &&
	git rebase --quit &&
	test_must_fail git rev-parse --verify refs/rewritten/onto
'

test_expect_success 'post-rewrite hook and fixups work for merges' '
	git checkout -b post-rewrite H &&
	test_commit same1 &&
	git reset --hard HEAD^ &&
	test_commit same2 &&
	git merge -m "to fix up" same1 &&
	echo same old same old >same2.t &&
	test_tick &&
	git commit --fixup HEAD same2.t &&
	fixup="$(git rev-parse HEAD)" &&

	test_hook post-rewrite <<-\EOF &&
	cat >actual
	EOF

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

test_expect_success 'root commits' '
	git checkout --orphan unrelated &&
	test_commit --author "Parsnip <root@example.com>" second-root &&
	test_commit third-root &&
	cat >script-from-scratch <<-\EOF &&
	pick third-root
	label first-branch
	reset [new root]
	pick second-root
	merge first-branch # Merge the 3rd root
	EOF
	test_config sequence.editor \""$PWD"/replace-editor.sh\" &&
	test_tick &&
	git rebase -i --force-rebase --root -r &&
	test "Parsnip" = "$(git show -s --format=%an HEAD^)" &&
	test $(git rev-parse second-root^0) != $(git rev-parse HEAD^) &&
	test $(git rev-parse second-root:second-root.t) = \
		$(git rev-parse HEAD^:second-root.t) &&
	test_cmp_graph HEAD <<-\EOF &&
	*   Merge the 3rd root
	|\
	| * third-root
	* second-root
	EOF

	: fast forward if possible &&
	before="$(git rev-parse --verify HEAD)" &&
	test_might_fail git config --unset sequence.editor &&
	test_tick &&
	git rebase -i --root -r &&
	test_cmp_rev HEAD $before
'

test_expect_success 'a "merge" into a root commit is a fast-forward' '
	head=$(git rev-parse HEAD) &&
	cat >script-from-scratch <<-EOF &&
	reset [new root]
	merge $head
	EOF
	test_config sequence.editor \""$PWD"/replace-editor.sh\" &&
	test_tick &&
	git rebase -i -r HEAD^ &&
	test_cmp_rev HEAD $head
'

test_expect_success 'A root commit can be a cousin, treat it that way' '
	git checkout --orphan khnum &&
	test_commit yama &&
	git checkout -b asherah main &&
	test_commit shamkat &&
	git merge --allow-unrelated-histories khnum &&
	test_tick &&
	git rebase -f -r HEAD^ &&
	test_cmp_rev ! HEAD^2 khnum &&
	test_cmp_graph HEAD^.. <<-\EOF &&
	*   Merge branch '\''khnum'\'' into asherah
	|\
	| * yama
	o shamkat
	EOF
	test_tick &&
	git rebase --rebase-merges=rebase-cousins HEAD^ &&
	test_cmp_graph HEAD^.. <<-\EOF
	*   Merge branch '\''khnum'\'' into asherah
	|\
	| * yama
	|/
	o shamkat
	EOF
'

test_expect_success 'labels that are object IDs are rewritten' '
	git checkout --detach B &&
	test_commit I &&
	third=$(git rev-parse HEAD) &&
	git checkout -b labels main &&
	git merge --no-commit $third &&
	test_tick &&
	git commit -m "Merge commit '\''$third'\'' into labels" &&
	echo noop >script-from-scratch &&
	test_config sequence.editor \""$PWD"/replace-editor.sh\" &&
	test_tick &&
	git rebase -i -r A &&
	grep "^label $third-" .git/ORIGINAL-TODO &&
	! grep "^label $third$" .git/ORIGINAL-TODO
'

test_expect_success 'octopus merges' '
	git checkout -b three &&
	test_commit before-octopus &&
	test_commit three &&
	git checkout -b two HEAD^ &&
	test_commit two &&
	git checkout -b one HEAD^ &&
	test_commit one &&
	test_tick &&
	(GIT_AUTHOR_NAME="Hank" GIT_AUTHOR_EMAIL="hank@sea.world" \
	 git merge -m "Tüntenfüsch" two three) &&

	: fast forward if possible &&
	before="$(git rev-parse --verify HEAD)" &&
	test_tick &&
	git rebase -i -r HEAD^^ &&
	test_cmp_rev HEAD $before &&

	test_tick &&
	git rebase -i --force-rebase -r HEAD^^ &&
	test "Hank" = "$(git show -s --format=%an HEAD)" &&
	test "$before" != $(git rev-parse HEAD) &&
	test_cmp_graph HEAD^^.. <<-\EOF
	*-.   Tüntenfüsch
	|\ \
	| | * three
	| * | two
	| |/
	* / one
	|/
	o before-octopus
	EOF
'

test_expect_success 'with --autosquash and --exec' '
	git checkout -b with-exec H &&
	echo Booh >B.t &&
	test_tick &&
	git commit --fixup B B.t &&
	write_script show.sh <<-\EOF &&
	subject="$(git show -s --format=%s HEAD)"
	content="$(git diff HEAD^ HEAD | tail -n 1)"
	echo "$subject: $content"
	EOF
	test_tick &&
	git rebase -ir --autosquash --exec ./show.sh A >actual &&
	grep "B: +Booh" actual &&
	grep "E: +Booh" actual &&
	grep "G: +G" actual
'

test_expect_success '--continue after resolving conflicts after a merge' '
	git checkout -b already-has-g E &&
	git cherry-pick E..G &&
	test_commit H2 &&

	git checkout -b conflicts-in-merge H &&
	test_commit H2 H2.t conflicts H2-conflict &&
	test_must_fail git rebase -r already-has-g &&
	grep conflicts H2.t &&
	echo resolved >H2.t &&
	git add -u &&
	git rebase --continue &&
	test_must_fail git rev-parse --verify HEAD^2 &&
	test_path_is_missing .git/MERGE_HEAD
'

test_expect_success '--rebase-merges with strategies' '
	git checkout -b with-a-strategy F &&
	test_tick &&
	git merge -m "Merge conflicting-G" conflicting-G &&

	: first, test with a merge strategy option &&
	git rebase -ir -Xtheirs G &&
	echo conflicting-G >expect &&
	test_cmp expect G.t &&

	: now, try with a merge strategy other than recursive &&
	git reset --hard @{1} &&
	write_script git-merge-override <<-\EOF &&
	echo overridden$1 >>G.t
	git add G.t
	EOF
	PATH="$PWD:$PATH" git rebase -ir -s override -Xxopt G &&
	test_write_lines G overridden--xopt >expect &&
	test_cmp expect G.t
'

test_expect_success '--rebase-merges with commit that can generate bad characters for filename' '
	git checkout -b colon-in-label E &&
	git merge -m "colon: this should work" G &&
	git rebase --rebase-merges --force-rebase E
'

test_expect_success '--rebase-merges with message matched with onto label' '
	git checkout -b onto-label E &&
	git merge -m onto G &&
	git rebase --rebase-merges --force-rebase E &&
	test_cmp_graph <<-\EOF
	*   onto
	|\
	| * G
	| * F
	* |   E
	|\ \
	| * | B
	* | | D
	| |/
	|/|
	* | C
	|/
	* A
	EOF
'

test_expect_success 'progress shows the correct total' '
	git checkout -b progress H &&
	git rebase --rebase-merges --force-rebase --verbose A 2> err &&
	# Expecting "Rebasing (N/14)" here, no bogus total number
	grep "^Rebasing.*/14.$" err >progress &&
	test_line_count = 14 progress
'

test_expect_success 'truncate label names' '
	commit=$(git commit-tree -p HEAD^ -p HEAD -m "0123456789 我 123" HEAD^{tree}) &&
	git merge --ff-only $commit &&

	done="$(git rev-parse --git-path rebase-merge/done)" &&
	git -c rebase.maxLabelLength=14 rebase --rebase-merges -x "cp \"$done\" out" --root &&
	grep "label 0123456789-我$" out &&
	git -c rebase.maxLabelLength=13 rebase --rebase-merges -x "cp \"$done\" out" --root &&
	grep "label 0123456789-$" out
'

test_done
