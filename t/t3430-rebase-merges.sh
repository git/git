#!/bin/sh
#
# Copyright (c) 2018 Johannes E. Schindelin
#

test_description='but rebase -i --rebase-merges

This test runs but rebase "interactively", retaining the branch structure by
recreating merge cummits.

Initial setup:

    -- B --                   (first)
   /       \
 A - C - D - E - H            (main)
   \    \       /
    \    F - G                (second)
     \
      Conflicting-G
'
BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-rebase.sh
. "$TEST_DIRECTORY"/lib-log-graph.sh

test_cmp_graph () {
	cat >expect &&
	lib_test_cmp_graph --boundary --format=%s "$@"
}

test_expect_success 'setup' '
	write_script replace-editor.sh <<-\EOF &&
	mv "$1" "$(but rev-parse --but-path ORIGINAL-TODO)"
	cp script-from-scratch "$1"
	EOF

	test_cummit A &&
	but checkout -b first &&
	test_cummit B &&
	b=$(but rev-parse --short HEAD) &&
	but checkout main &&
	test_cummit C &&
	c=$(but rev-parse --short HEAD) &&
	test_cummit D &&
	d=$(but rev-parse --short HEAD) &&
	but merge --no-cummit B &&
	test_tick &&
	but cummit -m E &&
	but tag -m E E &&
	e=$(but rev-parse --short HEAD) &&
	but checkout -b second C &&
	test_cummit F &&
	f=$(but rev-parse --short HEAD) &&
	test_cummit G &&
	g=$(but rev-parse --short HEAD) &&
	but checkout main &&
	but merge --no-cummit G &&
	test_tick &&
	but cummit -m H &&
	h=$(but rev-parse --short HEAD) &&
	but tag -m H H &&
	but checkout A &&
	test_cummit conflicting-G G.t
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
	but rebase -i -r A main &&
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

	grep -v "^#" <.but/ORIGINAL-TODO >output &&
	test_cmp expect output
'

test_expect_success '`reset` refuses to overwrite untracked files' '
	but checkout -b refuse-to-reset &&
	test_cummit dont-overwrite-untracked &&
	but checkout @{-1} &&
	: >dont-overwrite-untracked.t &&
	echo "reset refs/tags/dont-overwrite-untracked" >script-from-scratch &&
	test_config sequence.editor \""$PWD"/replace-editor.sh\" &&
	test_must_fail but rebase -ir HEAD &&
	but rebase --abort
'

test_expect_success 'failed `merge -C` writes patch (may be rescheduled, too)' '
	test_when_finished "test_might_fail but rebase --abort" &&
	but checkout -b conflicting-merge A &&

	: fail because of conflicting untracked file &&
	>G.t &&
	echo "merge -C H G" >script-from-scratch &&
	test_config sequence.editor \""$PWD"/replace-editor.sh\" &&
	test_tick &&
	test_must_fail but rebase -ir HEAD &&
	grep "^merge -C .* G$" .but/rebase-merge/done &&
	grep "^merge -C .* G$" .but/rebase-merge/but-rebase-todo &&
	test_path_is_file .but/rebase-merge/patch &&

	: fail because of merge conflict &&
	rm G.t .but/rebase-merge/patch &&
	but reset --hard conflicting-G &&
	test_must_fail but rebase --continue &&
	! grep "^merge -C .* G$" .but/rebase-merge/but-rebase-todo &&
	test_path_is_file .but/rebase-merge/patch
'

test_expect_success 'failed `merge <branch>` does not crash' '
	test_when_finished "test_might_fail but rebase --abort" &&
	but checkout conflicting-G &&

	echo "merge G" >script-from-scratch &&
	test_config sequence.editor \""$PWD"/replace-editor.sh\" &&
	test_tick &&
	test_must_fail but rebase -ir HEAD &&
	! grep "^merge G$" .but/rebase-merge/but-rebase-todo &&
	grep "^Merge branch ${SQ}G${SQ}$" .but/rebase-merge/message
'

test_expect_success 'merge -c cummits before rewording and reloads todo-list' '
	cat >script-from-scratch <<-\EOF &&
	merge -c E B
	merge -c H G
	EOF

	but checkout -b merge-c H &&
	(
		set_reword_editor &&
		BUT_SEQUENCE_EDITOR="\"$PWD/replace-editor.sh\"" \
			but rebase -i -r D
	) &&
	check_reworded_cummits E H
'

test_expect_success 'merge -c rewords when a strategy is given' '
	but checkout -b merge-c-with-strategy H &&
	write_script but-merge-override <<-\EOF &&
	echo overridden$1 >G.t
	but add G.t
	EOF

	PATH="$PWD:$PATH" \
	BUT_SEQUENCE_EDITOR="echo merge -c H G >" \
	BUT_EDITOR="echo edited >>" \
		but rebase --no-ff -ir -s override -Xxopt E &&
	test_write_lines overridden--xopt >expect &&
	test_cmp expect G.t &&
	test_write_lines H "" edited "" >expect &&
	but log --format=%B -1 >actual &&
	test_cmp expect actual

'
test_expect_success 'with a branch tip that was cherry-picked already' '
	but checkout -b already-upstream main &&
	base="$(but rev-parse --verify HEAD)" &&

	test_cummit A1 &&
	test_cummit A2 &&
	but reset --hard $base &&
	test_cummit B1 &&
	test_tick &&
	but merge -m "Merge branch A" A2 &&

	but checkout -b upstream-with-a2 $base &&
	test_tick &&
	but cherry-pick A2 &&

	but checkout already-upstream &&
	test_tick &&
	but rebase -i -r upstream-with-a2 &&
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
	but checkout -b cousins main &&
	before="$(but rev-parse --verify HEAD)" &&
	test_tick &&
	but rebase -r HEAD^ &&
	test_cmp_rev HEAD $before &&
	test_tick &&
	but rebase --rebase-merges=rebase-cousins HEAD^ &&
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
	but worktree add wt &&
	cat >wt/script-from-scratch <<-\EOF &&
	label xyz
	exec BUT_DIR=../.but but rev-parse --verify refs/rewritten/xyz >a || :
	exec but rev-parse --verify refs/rewritten/xyz >b
	EOF

	test_config -C wt sequence.editor \""$PWD"/replace-editor.sh\" &&
	but -C wt rebase -i HEAD &&
	test_must_be_empty wt/a &&
	test_cmp_rev HEAD "$(cat wt/b)"
'

test_expect_success '--abort cleans up refs/rewritten' '
	but checkout -b abort-cleans-refs-rewritten H &&
	BUT_SEQUENCE_EDITOR="echo break >>" but rebase -ir @^ &&
	but rev-parse --verify refs/rewritten/onto &&
	but rebase --abort &&
	test_must_fail but rev-parse --verify refs/rewritten/onto
'

test_expect_success '--quit cleans up refs/rewritten' '
	but checkout -b quit-cleans-refs-rewritten H &&
	BUT_SEQUENCE_EDITOR="echo break >>" but rebase -ir @^ &&
	but rev-parse --verify refs/rewritten/onto &&
	but rebase --quit &&
	test_must_fail but rev-parse --verify refs/rewritten/onto
'

test_expect_success 'post-rewrite hook and fixups work for merges' '
	but checkout -b post-rewrite H &&
	test_cummit same1 &&
	but reset --hard HEAD^ &&
	test_cummit same2 &&
	but merge -m "to fix up" same1 &&
	echo same old same old >same2.t &&
	test_tick &&
	but cummit --fixup HEAD same2.t &&
	fixup="$(but rev-parse HEAD)" &&

	test_hook post-rewrite <<-\EOF &&
	cat >actual
	EOF

	test_tick &&
	but rebase -i --autosquash -r HEAD^^^ &&
	printf "%s %s\n%s %s\n%s %s\n%s %s\n" >expect $(but rev-parse \
		$fixup^^2 HEAD^2 \
		$fixup^^ HEAD^ \
		$fixup^ HEAD \
		$fixup HEAD) &&
	test_cmp expect actual
'

test_expect_success 'refuse to merge ancestors of HEAD' '
	echo "merge HEAD^" >script-from-scratch &&
	test_config -C wt sequence.editor \""$PWD"/replace-editor.sh\" &&
	before="$(but rev-parse HEAD)" &&
	but rebase -i HEAD &&
	test_cmp_rev HEAD $before
'

test_expect_success 'root cummits' '
	but checkout --orphan unrelated &&
	(BUT_AUTHOR_NAME="Parsnip" BUT_AUTHOR_EMAIL="root@example.com" \
	 test_cummit second-root) &&
	test_cummit third-root &&
	cat >script-from-scratch <<-\EOF &&
	pick third-root
	label first-branch
	reset [new root]
	pick second-root
	merge first-branch # Merge the 3rd root
	EOF
	test_config sequence.editor \""$PWD"/replace-editor.sh\" &&
	test_tick &&
	but rebase -i --force-rebase --root -r &&
	test "Parsnip" = "$(but show -s --format=%an HEAD^)" &&
	test $(but rev-parse second-root^0) != $(but rev-parse HEAD^) &&
	test $(but rev-parse second-root:second-root.t) = \
		$(but rev-parse HEAD^:second-root.t) &&
	test_cmp_graph HEAD <<-\EOF &&
	*   Merge the 3rd root
	|\
	| * third-root
	* second-root
	EOF

	: fast forward if possible &&
	before="$(but rev-parse --verify HEAD)" &&
	test_might_fail but config --unset sequence.editor &&
	test_tick &&
	but rebase -i --root -r &&
	test_cmp_rev HEAD $before
'

test_expect_success 'a "merge" into a root cummit is a fast-forward' '
	head=$(but rev-parse HEAD) &&
	cat >script-from-scratch <<-EOF &&
	reset [new root]
	merge $head
	EOF
	test_config sequence.editor \""$PWD"/replace-editor.sh\" &&
	test_tick &&
	but rebase -i -r HEAD^ &&
	test_cmp_rev HEAD $head
'

test_expect_success 'A root cummit can be a cousin, treat it that way' '
	but checkout --orphan khnum &&
	test_cummit yama &&
	but checkout -b asherah main &&
	test_cummit shamkat &&
	but merge --allow-unrelated-histories khnum &&
	test_tick &&
	but rebase -f -r HEAD^ &&
	test_cmp_rev ! HEAD^2 khnum &&
	test_cmp_graph HEAD^.. <<-\EOF &&
	*   Merge branch '\''khnum'\'' into asherah
	|\
	| * yama
	o shamkat
	EOF
	test_tick &&
	but rebase --rebase-merges=rebase-cousins HEAD^ &&
	test_cmp_graph HEAD^.. <<-\EOF
	*   Merge branch '\''khnum'\'' into asherah
	|\
	| * yama
	|/
	o shamkat
	EOF
'

test_expect_success 'labels that are object IDs are rewritten' '
	but checkout -b third B &&
	test_cummit I &&
	third=$(but rev-parse HEAD) &&
	but checkout -b labels main &&
	but merge --no-cummit third &&
	test_tick &&
	but cummit -m "Merge cummit '\''$third'\'' into labels" &&
	echo noop >script-from-scratch &&
	test_config sequence.editor \""$PWD"/replace-editor.sh\" &&
	test_tick &&
	but rebase -i -r A &&
	grep "^label $third-" .but/ORIGINAL-TODO &&
	! grep "^label $third$" .but/ORIGINAL-TODO
'

test_expect_success 'octopus merges' '
	but checkout -b three &&
	test_cummit before-octopus &&
	test_cummit three &&
	but checkout -b two HEAD^ &&
	test_cummit two &&
	but checkout -b one HEAD^ &&
	test_cummit one &&
	test_tick &&
	(BUT_AUTHOR_NAME="Hank" BUT_AUTHOR_EMAIL="hank@sea.world" \
	 but merge -m "Tüntenfüsch" two three) &&

	: fast forward if possible &&
	before="$(but rev-parse --verify HEAD)" &&
	test_tick &&
	but rebase -i -r HEAD^^ &&
	test_cmp_rev HEAD $before &&

	test_tick &&
	but rebase -i --force-rebase -r HEAD^^ &&
	test "Hank" = "$(but show -s --format=%an HEAD)" &&
	test "$before" != $(but rev-parse HEAD) &&
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
	but checkout -b with-exec H &&
	echo Booh >B.t &&
	test_tick &&
	but cummit --fixup B B.t &&
	write_script show.sh <<-\EOF &&
	subject="$(but show -s --format=%s HEAD)"
	content="$(but diff HEAD^ HEAD | tail -n 1)"
	echo "$subject: $content"
	EOF
	test_tick &&
	but rebase -ir --autosquash --exec ./show.sh A >actual &&
	grep "B: +Booh" actual &&
	grep "E: +Booh" actual &&
	grep "G: +G" actual
'

test_expect_success '--continue after resolving conflicts after a merge' '
	but checkout -b already-has-g E &&
	but cherry-pick E..G &&
	test_commit H2 &&

	but checkout -b conflicts-in-merge H &&
	test_commit H2 H2.t conflicts H2-conflict &&
	test_must_fail but rebase -r already-has-g &&
	grep conflicts H2.t &&
	echo resolved >H2.t &&
	but add -u &&
	but rebase --continue &&
	test_must_fail but rev-parse --verify HEAD^2 &&
	test_path_is_missing .but/MERGE_HEAD
'

test_expect_success '--rebase-merges with strategies' '
	but checkout -b with-a-strategy F &&
	test_tick &&
	but merge -m "Merge conflicting-G" conflicting-G &&

	: first, test with a merge strategy option &&
	but rebase -ir -Xtheirs G &&
	echo conflicting-G >expect &&
	test_cmp expect G.t &&

	: now, try with a merge strategy other than recursive &&
	but reset --hard @{1} &&
	write_script but-merge-override <<-\EOF &&
	echo overridden$1 >>G.t
	but add G.t
	EOF
	PATH="$PWD:$PATH" but rebase -ir -s override -Xxopt G &&
	test_write_lines G overridden--xopt >expect &&
	test_cmp expect G.t
'

test_expect_success '--rebase-merges with cummit that can generate bad characters for filename' '
	but checkout -b colon-in-label E &&
	but merge -m "colon: this should work" G &&
	but rebase --rebase-merges --force-rebase E
'

test_expect_success '--rebase-merges with message matched with onto label' '
	but checkout -b onto-label E &&
	but merge -m onto G &&
	but rebase --rebase-merges --force-rebase E &&
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

test_done
