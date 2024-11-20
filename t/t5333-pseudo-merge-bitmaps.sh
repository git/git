#!/bin/sh

test_description='pseudo-merge bitmaps'

GIT_TEST_MULTI_PACK_INDEX_WRITE_BITMAP=0

. ./test-lib.sh

test_pseudo_merges () {
	test-tool bitmap dump-pseudo-merges
}

test_pseudo_merge_commits () {
	test-tool bitmap dump-pseudo-merge-commits "$1"
}

test_pseudo_merges_satisfied () {
	test_trace2_data bitmap pseudo_merges_satisfied "$1"
}

test_pseudo_merges_cascades () {
	test_trace2_data bitmap pseudo_merges_cascades "$1"
}

test_pseudo_merges_reused () {
	test_trace2_data pack-bitmap-write building_bitmaps_pseudo_merge_reused "$1"
}

tag_everything () {
	git rev-list --all --no-object-names >in &&
	perl -lne '
		print "create refs/tags/" . $. . " " . $1 if /([0-9a-f]+)/
	' <in | git update-ref --stdin
}

test_expect_success 'setup' '
	test_commit_bulk 512 &&
	tag_everything
'

test_expect_success 'bitmap traversal without pseudo-merges' '
	git repack -adb &&

	git rev-list --count --all --objects >expect &&

	: >trace2.txt &&
	GIT_TRACE2_EVENT=$PWD/trace2.txt \
		git rev-list --count --all --objects --use-bitmap-index >actual &&

	test_pseudo_merges_satisfied 0 <trace2.txt &&
	test_pseudo_merges_cascades 0 <trace2.txt &&
	test_pseudo_merges >merges &&
	test_must_be_empty merges &&
	test_cmp expect actual
'

test_expect_success 'pseudo-merges accurately represent their objects' '
	test_config bitmapPseudoMerge.test.pattern "refs/tags/" &&
	test_config bitmapPseudoMerge.test.maxMerges 8 &&
	test_config bitmapPseudoMerge.test.stableThreshold never &&

	git repack -adb &&

	test_pseudo_merges >merges &&
	test_line_count = 8 merges &&

	for i in $(test_seq 0 $(($(wc -l <merges)-1)))
	do
		test-tool bitmap dump-pseudo-merge-commits $i >commits &&

		git rev-list --objects --no-object-names --stdin <commits >expect.raw &&
		test-tool bitmap dump-pseudo-merge-objects $i >actual.raw &&

		sort -u <expect.raw >expect &&
		sort -u <actual.raw >actual &&

		test_cmp expect actual || return 1
	done
'

test_expect_success 'bitmap traversal with pseudo-merges' '
	: >trace2.txt &&
	GIT_TRACE2_EVENT=$PWD/trace2.txt \
		git rev-list --count --all --objects --use-bitmap-index >actual &&
	git rev-list --count --all --objects >expect &&

	test_pseudo_merges_satisfied 8 <trace2.txt &&
	test_pseudo_merges_cascades 1 <trace2.txt &&
	test_cmp expect actual
'

test_expect_success 'stale bitmap traversal with pseudo-merges' '
	test_commit other &&

	: >trace2.txt &&
	GIT_TRACE2_EVENT=$PWD/trace2.txt \
		git rev-list --count --all --objects --use-bitmap-index >actual &&
	git rev-list --count --all --objects >expect &&

	test_pseudo_merges_satisfied 8 <trace2.txt &&
	test_pseudo_merges_cascades 1 <trace2.txt &&
	test_cmp expect actual
'

test_expect_success 'bitmapPseudoMerge.sampleRate adjusts commit selection rate' '
	test_config bitmapPseudoMerge.test.pattern "refs/tags/" &&
	test_config bitmapPseudoMerge.test.maxMerges 1 &&
	test_config bitmapPseudoMerge.test.stableThreshold never &&

	commits_nr=$(git rev-list --all --count) &&

	for rate in 1.0 0.5 0.25
	do
		git -c bitmapPseudoMerge.test.sampleRate=$rate repack -adb &&

		test_pseudo_merges >merges &&
		test_line_count = 1 merges &&
		test_pseudo_merge_commits 0 >commits &&

		test-tool bitmap list-commits >bitmaps &&
		bitmaps_nr="$(wc -l <bitmaps)" &&

		perl -MPOSIX -e "print ceil(\$ARGV[0]*(\$ARGV[1]-\$ARGV[2]))" \
			"$rate" "$commits_nr" "$bitmaps_nr" >expect &&

		test $(cat expect) -eq $(wc -l <commits) || return 1
	done
'

test_expect_success 'bitmapPseudoMerge.threshold excludes newer commits' '
	git init pseudo-merge-threshold &&
	(
		cd pseudo-merge-threshold &&

		new="1672549200" && # 2023-01-01
		old="1641013200" && # 2022-01-01

		GIT_COMMITTER_DATE="$new +0000" &&
		export GIT_COMMITTER_DATE &&
		test_commit_bulk --message="new" --notick 128 &&

		GIT_COMMITTER_DATE="$old +0000" &&
		export GIT_COMMITTER_DATE &&
		test_commit_bulk --message="old" --notick 128 &&

		tag_everything &&

		git \
			-c bitmapPseudoMerge.test.pattern="refs/tags/" \
			-c bitmapPseudoMerge.test.maxMerges=1 \
			-c bitmapPseudoMerge.test.threshold=$(($new - 1)) \
			-c bitmapPseudoMerge.test.stableThreshold=never \
			repack -adb &&

		test_pseudo_merges >merges &&
		test_line_count = 1 merges &&

		test_pseudo_merge_commits 0 >oids &&
		git cat-file --batch <oids >commits &&

		test $(wc -l <oids) = $(grep -c "^committer.*$old +0000$" commits)
	)
'

test_expect_success 'bitmapPseudoMerge.stableThreshold creates stable groups' '
	(
		cd pseudo-merge-threshold &&

		new="1672549200" && # 2023-01-01
		mid="1654059600" && # 2022-06-01
		old="1641013200" && # 2022-01-01

		GIT_COMMITTER_DATE="$mid +0000" &&
		export GIT_COMMITTER_DATE &&
		test_commit_bulk --message="mid" --notick 128 &&

		git for-each-ref --format="delete %(refname)" refs/tags >in &&
		git update-ref --stdin <in &&

		tag_everything &&

		git \
			-c bitmapPseudoMerge.test.pattern="refs/tags/" \
			-c bitmapPseudoMerge.test.maxMerges=1 \
			-c bitmapPseudoMerge.test.threshold=$(($new - 1)) \
			-c bitmapPseudoMerge.test.stableThreshold=$(($mid - 1)) \
			-c bitmapPseudoMerge.test.stableSize=10 \
			repack -adb &&

		test_pseudo_merges >merges &&
		merges_nr="$(wc -l <merges)" &&

		for i in $(test_seq $(($merges_nr - 1)))
		do
			test_pseudo_merge_commits 0 >oids &&
			git cat-file --batch <oids >commits &&

			expect="$(grep -c "^committer.*$old +0000$" commits)" &&
			actual="$(wc -l <oids)" &&

			test $expect = $actual || return 1
		done &&

		test_pseudo_merge_commits $(($merges_nr - 1)) >oids &&
		git cat-file --batch <oids >commits &&
		test $(wc -l <oids) = $(grep -c "^committer.*$mid +0000$" commits)
	)
'

test_expect_success 'out of order thresholds are rejected' '
	test_must_fail git \
		-c bitmapPseudoMerge.test.pattern="refs/*" \
		-c bitmapPseudoMerge.test.threshold=1.month.ago \
		-c bitmapPseudoMerge.test.stableThreshold=1.week.ago \
		repack -adb 2>err &&

	cat >expect <<-EOF &&
	fatal: pseudo-merge group ${SQ}test${SQ} has unstable threshold before stable one
	EOF

	test_cmp expect err
'

test_expect_success 'pseudo-merge pattern with capture groups' '
	git init pseudo-merge-captures &&
	(
		cd pseudo-merge-captures &&

		test_commit_bulk 128 &&
		tag_everything &&

		for r in $(test_seq 8)
		do
			test_commit_bulk 16 &&

			git rev-list HEAD~16.. >in &&

			perl -lne "print \"create refs/remotes/$r/tags/\$. \$_\"" <in |
			git update-ref --stdin || return 1
		done &&

		git \
			-c bitmapPseudoMerge.tags.pattern="refs/remotes/([0-9]+)/tags/" \
			-c bitmapPseudoMerge.tags.maxMerges=1 \
			repack -adb &&

		git for-each-ref --format="%(objectname) %(refname)" >refs &&

		test_pseudo_merges >merges &&
		for m in $(test_seq 0 $(($(wc -l <merges) - 1)))
		do
			test_pseudo_merge_commits $m >oids &&
			grep -f oids refs |
			perl -lne "print \$1 if /refs\/remotes\/([0-9]+)/" |
			sort -u || return 1
		done >remotes &&

		test $(wc -l <remotes) -eq $(sort -u <remotes | wc -l)
	)
'

test_expect_success 'pseudo-merge overlap setup' '
	git init pseudo-merge-overlap &&
	(
		cd pseudo-merge-overlap &&

		test_commit_bulk 256 &&
		tag_everything &&

		git \
			-c bitmapPseudoMerge.all.pattern="refs/" \
			-c bitmapPseudoMerge.all.maxMerges=1 \
			-c bitmapPseudoMerge.all.stableThreshold=never \
			-c bitmapPseudoMerge.tags.pattern="refs/tags/" \
			-c bitmapPseudoMerge.tags.maxMerges=1 \
			-c bitmapPseudoMerge.tags.stableThreshold=never \
			repack -adb
	)
'

test_expect_success 'pseudo-merge overlap generates overlapping groups' '
	(
		cd pseudo-merge-overlap &&

		test_pseudo_merges >merges &&
		test_line_count = 2 merges &&

		test_pseudo_merge_commits 0 >commits-0.raw &&
		test_pseudo_merge_commits 1 >commits-1.raw &&

		sort commits-0.raw >commits-0 &&
		sort commits-1.raw >commits-1 &&

		comm -12 commits-0 commits-1 >overlap &&

		test_line_count -gt 0 overlap
	)
'

test_expect_success 'pseudo-merge overlap traversal' '
	(
		cd pseudo-merge-overlap &&

		: >trace2.txt &&
		GIT_TRACE2_EVENT=$PWD/trace2.txt \
			git rev-list --count --all --objects --use-bitmap-index >actual &&
		git rev-list --count --all --objects >expect &&

		test_pseudo_merges_satisfied 2 <trace2.txt &&
		test_pseudo_merges_cascades 1 <trace2.txt &&
		test_cmp expect actual
	)
'

test_expect_success 'pseudo-merge overlap stale traversal' '
	(
		cd pseudo-merge-overlap &&

		test_commit other &&

		: >trace2.txt &&
		GIT_TRACE2_EVENT=$PWD/trace2.txt \
			git rev-list --count --all --objects --use-bitmap-index >actual &&
		git rev-list --count --all --objects >expect &&

		test_pseudo_merges_satisfied 2 <trace2.txt &&
		test_pseudo_merges_cascades 1 <trace2.txt &&
		test_cmp expect actual
	)
'

test_expect_success 'pseudo-merge reuse' '
	git init pseudo-merge-reuse &&
	(
		cd pseudo-merge-reuse &&

		stable="1641013200" && # 2022-01-01
		unstable="1672549200" && # 2023-01-01

		GIT_COMMITTER_DATE="$stable +0000" &&
		export GIT_COMMITTER_DATE &&
		test_commit_bulk --notick 128 &&
		GIT_COMMITTER_DATE="$unstable +0000" &&
		export GIT_COMMITTER_DATE &&
		test_commit_bulk --notick 128 &&

		tag_everything &&

		git \
			-c bitmapPseudoMerge.test.pattern="refs/tags/" \
			-c bitmapPseudoMerge.test.maxMerges=1 \
			-c bitmapPseudoMerge.test.threshold=now \
			-c bitmapPseudoMerge.test.stableThreshold=$(($unstable - 1)) \
			-c bitmapPseudoMerge.test.stableSize=512 \
			repack -adb &&

		test_pseudo_merges >merges &&
		test_line_count = 2 merges &&

		test_pseudo_merge_commits 0 >stable-oids.before &&
		test_pseudo_merge_commits 1 >unstable-oids.before &&

		: >trace2.txt &&
		GIT_TRACE2_EVENT=$PWD/trace2.txt git \
			-c bitmapPseudoMerge.test.pattern="refs/tags/" \
			-c bitmapPseudoMerge.test.maxMerges=2 \
			-c bitmapPseudoMerge.test.threshold=now \
			-c bitmapPseudoMerge.test.stableThreshold=$(($unstable - 1)) \
			-c bitmapPseudoMerge.test.stableSize=512 \
			repack -adb &&

		test_pseudo_merges_reused 1 <trace2.txt &&

		test_pseudo_merges >merges &&
		test_line_count = 3 merges &&

		test_pseudo_merge_commits 0 >stable-oids.after &&
		for i in 1 2
		do
			test_pseudo_merge_commits $i || return 1
		done >unstable-oids.after &&

		sort -u <stable-oids.before >expect &&
		sort -u <stable-oids.after >actual &&
		test_cmp expect actual &&

		sort -u <unstable-oids.before >expect &&
		sort -u <unstable-oids.after >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'empty pseudo-merge group' '
	git init pseudo-merge-empty-group &&
	(
		cd pseudo-merge-empty-group &&

		# Ensure that a pseudo-merge group with no unstable
		# commits does not generate an empty pseudo-merge
		# bitmap.
		git config bitmapPseudoMerge.empty.pattern refs/ &&

		test_commit base &&
		git repack -adb &&

		test-tool bitmap dump-pseudo-merges >merges &&
		test_line_count = 1 merges &&

		test 0 -eq "$(grep -c commits=0 <merges)"
	)
'

test_expect_success 'pseudo-merge closure' '
	git init pseudo-merge-closure &&
	(
		cd pseudo-merge-closure &&

		test_commit A &&
		git repack -d &&

		test_commit B &&

		# Note that the contents of A is packed, but B is not. A
		# (and the objects reachable from it) are thus visible
		# to the MIDX, but the same is not true for B and its
		# objects.
		#
		# Ensure that we do not attempt to create a pseudo-merge
		# for B, depsite it matching the below pseudo-merge
		# group pattern, as doing so would result in a failure
		# to write a non-closed bitmap.
		git config bitmapPseudoMerge.test.pattern refs/ &&
		git config bitmapPseudoMerge.test.threshold now &&

		git multi-pack-index write --bitmap &&

		test-tool bitmap dump-pseudo-merges >pseudo-merges &&
		test_line_count = 1 pseudo-merges &&

		git rev-parse A >expect &&

		test-tool bitmap list-commits >actual &&
		test_cmp expect actual &&
		test-tool bitmap dump-pseudo-merge-commits 0 >actual &&
		test_cmp expect actual
	)
'

test_done
