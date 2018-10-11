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
   \    \       /
    \    F - G                (second)
     \
      Conflicting-G
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
	b=$(git rev-parse --short HEAD) &&
	git checkout master &&
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
	git checkout master &&
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
	git rebase -i -r A master &&
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
	label E

	reset onto
	pick $c C
	label branch-point
	pick $f F
	pick $g G
	label H

	reset branch-point # C
	pick $d D
	merge -C $e E # E
	merge -C $h H # H

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
	test_must_fail git rebase -ir HEAD &&
	git rebase --abort
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
	grep "^merge -C .* G$" .git/rebase-merge/done &&
	grep "^merge -C .* G$" .git/rebase-merge/git-rebase-todo &&
	test_path_is_file .git/rebase-merge/patch &&

	: fail because of merge conflict &&
	rm G.t .git/rebase-merge/patch &&
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

test_expect_success 'fast-forward merge -c still rewords' '
	git checkout -b fast-forward-merge-c H &&
	(
		set_fake_editor &&
		FAKE_COMMIT_MESSAGE=edited \
			GIT_SEQUENCE_EDITOR="echo merge -c H G >" \
			git rebase -ir @^
	) &&
	echo edited >expected &&
	git log --pretty=format:%B -1 >actual &&
	test_cmp expected actual
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

test_expect_success 'root commits' '
	git checkout --orphan unrelated &&
	(GIT_AUTHOR_NAME="Parsnip" GIT_AUTHOR_EMAIL="root@example.com" \
	 test_commit second-root) &&
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
	git checkout -b asherah master &&
	test_commit shamkat &&
	git merge --allow-unrelated-histories khnum &&
	test_tick &&
	git rebase -f -r HEAD^ &&
	! test_cmp_rev HEAD^2 khnum &&
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
	git checkout -b third B &&
	test_commit I &&
	third=$(git rev-parse HEAD) &&
	git checkout -b labels master &&
	git merge --no-commit third &&
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
	* | one
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
	content="$(git diff HEAD^! | tail -n 1)"
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

test_done
