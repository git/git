#!/bin/sh

test_description='tests for git-history squash subcommand'

. ./test-lib.sh

stage_file () {
	printf "%s\n" "$1" >file &&
	git add file
}

commit_with_message () {
	printf "%b" "$1" >msg &&
	git commit --allow-empty -qF msg
}

check_commit_count () {
	git rev-list --count "$1" >actual &&
	echo "$2" >expect &&
	test_cmp expect actual
}

check_log_subjects () {
	git log --format="%s" "$1" >actual &&
	cat >expect &&
	test_cmp expect actual
}

check_log_messages () {
	git log --format="%B" "$1" >actual &&
	cat >expect &&
	test_cmp expect actual
}

test_expect_success 'setup linear history touching two files' '
	test_commit base file a &&
	git tag start &&
	test_commit --no-tag one other x &&
	test_commit --no-tag two file c &&
	test_commit three file d
'

test_expect_success 'errors on missing range argument' '
	test_must_fail git history squash 2>err &&
	test_grep "expects a revision range" err
'

test_expect_success 'errors on an empty range' '
	test_must_fail git history squash HEAD..HEAD 2>err &&
	test_grep "the revision range is empty" err
'

test_expect_success 'errors on a single revision that is not a range' '
	test_must_fail git history squash HEAD 2>err &&
	test_grep "not a .*range" err &&
	test_must_fail git history squash HEAD~1 2>err &&
	test_grep "not a .*range" err
'

test_expect_success 'errors on a range holding a single commit' '
	git reset --hard three &&
	head_before=$(git rev-parse HEAD) &&

	test_must_fail git history squash "HEAD^!" 2>err &&
	test_grep "single commit; nothing to squash" err &&
	test_cmp_rev "$head_before" HEAD
'

test_expect_success 'accepts multiple revision arguments with an exclusion' '
	git reset --hard three &&
	git branch -f keep HEAD~2 &&
	tip_tree=$(git rev-parse HEAD^{tree}) &&

	git history squash start..HEAD ^keep &&

	git reflog -1 --format=%gs >actual &&
	echo "squash: updating start..HEAD ^keep" >expect &&
	test_cmp expect actual &&

	check_log_subjects start..HEAD <<-\EOF &&
	two
	one
	EOF
	test_cmp_rev keep HEAD~1 &&
	test "$tip_tree" = "$(git rev-parse HEAD^{tree})" &&

	git branch -D keep
'

test_expect_success 'squashes a branch the current branch is not on' '
	git reset --hard three &&
	main=$(git symbolic-ref --short HEAD) &&
	head_before=$(git rev-parse HEAD) &&
	git checkout -b off-history start &&
	test_commit --no-tag off-one off a &&
	test_commit --no-tag off-two off b &&
	git checkout "$main" &&

	git history squash start..off-history &&

	check_commit_count start..off-history 1 &&
	test_cmp_rev "$head_before" HEAD &&

	git branch -D off-history
'

test_expect_success 'squashes a range into a single commit without changing the tree' '
	git reset --hard three &&
	head_before=$(git rev-parse HEAD) &&
	tip_tree=$(git rev-parse HEAD^{tree}) &&

	git history squash --dry-run start.. >out &&
	predicted=$(awk "/^update refs\/heads\// {print \$3}" out) &&
	test_cmp_rev "$head_before" HEAD &&

	git history squash start.. &&

	test "$predicted" = "$(git rev-parse HEAD)" &&
	check_commit_count start..HEAD 1 &&
	test_cmp_rev start HEAD^ &&
	test "$tip_tree" = "$(git rev-parse HEAD^{tree})" &&
	check_log_subjects -1 <<-\EOF &&
	one
	EOF
	git reflog >reflog &&
	test_grep "squash: updating" reflog
'

test_expect_success 'sanitizes rev-list walk options, before and after --' '
	git reset --hard three &&
	tip_tree=$(git rev-parse HEAD^{tree}) &&

	git history squash --date-order start.. 2>err &&
	test_grep "ignoring rev-list options" err &&
	test_cmp_rev start HEAD^ &&
	test "$tip_tree" = "$(git rev-parse HEAD^{tree})" &&

	git reset --hard three &&
	git history squash -- --reverse start.. 2>err &&
	test_grep "ignoring rev-list options" err &&
	test_cmp_rev start HEAD^ &&
	test "$tip_tree" = "$(git rev-parse HEAD^{tree})"
'

test_expect_success 'squashes an interior range and replays descendants verbatim' '
	git reset --hard three &&
	final_tree=$(git rev-parse HEAD^{tree}) &&

	git history squash start..@~1 &&

	check_log_subjects start..HEAD <<-\EOF &&
	three
	one
	EOF

	test_cmp_rev start HEAD~2 &&
	test "$final_tree" = "$(git rev-parse HEAD^{tree})"
'

test_expect_success 'squashes when the base is the root commit' '
	git reset --hard three &&
	root=$(git rev-list --max-parents=0 HEAD) &&
	tip_tree=$(git rev-parse HEAD^{tree}) &&

	git history squash "$root.." &&

	check_commit_count "$root..HEAD" 1 &&
	test_cmp_rev "$root" HEAD^ &&
	test "$tip_tree" = "$(git rev-parse HEAD^{tree})"
'


test_expect_success 'folds fixups whose target is in the range' '
	git reset --hard start &&
	test_commit --no-tag target file b &&
	git commit --allow-empty -m "fixup! target" &&
	git commit --allow-empty -m "fixup! target" &&
	test_commit --no-tag later file c &&

	git history squash start.. &&

	check_commit_count start..HEAD 1 &&
	check_log_subjects -1 <<-\EOF
	target
	EOF
'

test_expect_success 'refuses a below-range fixup! after an in-range commit' '
	git reset --hard start &&
	test_commit --no-tag inside file b &&
	test_commit --no-tag "fixup! outside" file c &&
	head_before=$(git rev-parse HEAD) &&

	test_must_fail git history squash start.. 2>err &&
	test_grep "target is not in the range" err &&
	test_cmp_rev "$head_before" HEAD
'

test_expect_success 'combines a run of fixups for one commit below the range' '
	git reset --hard start &&
	stage_file b && git commit -m "fixup! base" &&
	stage_file c && git commit -m "fixup! base" &&

	git history squash start.. &&

	check_commit_count start..HEAD 1 &&
	check_log_subjects -1 <<-\EOF
	fixup! base
	EOF
'

test_expect_success 'combining below-range fixups keeps the last amend! message' '
	git reset --hard start &&
	stage_file b && git commit -m "fixup! base" &&
	stage_file c &&
	commit_with_message "amend! base\n\namended body\n" &&

	git history squash start.. &&

	check_commit_count start..HEAD 1 &&
	check_log_messages -1 <<-\EOF
	amend! base

	amended body

	EOF
'

test_expect_success 'refuses fixups for two different commits below the range' '
	git reset --hard start &&
	stage_file b && git commit -m "fixup! aaa" &&
	stage_file c && git commit -m "fixup! bbb" &&
	head_before=$(git rev-parse HEAD) &&

	test_must_fail git history squash start.. 2>err &&
	test_grep "target is not in the range" err &&
	test_cmp_rev "$head_before" HEAD
'

test_expect_success 'the last amend! for the oldest commit replaces its message' '
	git reset --hard start &&
	test_commit --no-tag marker-oldest file b &&
	git commit --allow-empty -m "squash! marker-oldest" &&
	commit_with_message "amend! marker-oldest\n\nearlier message\n" &&
	commit_with_message \
		"amend! marker-oldest\n\namended subject\n\namended body\n" &&
	test_commit --no-tag marker-later file c &&
	commit_with_message "amend! marker-later\n\nwrong message\n" &&

	git history squash start.. &&

	check_commit_count start..HEAD 1 &&
	check_log_messages -1 <<-\EOF
	amended subject

	amended body

	EOF
'

test_expect_success 'preserves authorship of the oldest commit' '
	git reset --hard start &&
	GIT_AUTHOR_NAME=Squasher GIT_AUTHOR_EMAIL=squash@example.com \
		test_commit --no-tag oldest file b &&
	test_commit newest file c &&

	git history squash start.. &&

	git log -1 --format="%an <%ae>" >actual &&
	echo "Squasher <squash@example.com>" >expect &&
	test_cmp expect actual
'

test_expect_success '--reedit-message offers every folded-in message' '
	git reset --hard start &&
	stage_file b &&
	git commit -m "re-one subject" -m "re-one body line" &&
	test_commit --no-tag re-two file c &&
	test_commit re-three file d &&

	write_script editor <<-\EOF &&
	cat "$1" >edited &&
	echo combined >"$1"
	EOF
	test_set_editor "$(pwd)/editor" &&
	git history squash --reedit-message start.. &&

	cat >expect <<-EOF &&
	# This is a combination of 3 commits.
	# This is the 1st commit message:

	re-one subject

	re-one body line

	# This is the commit message #2:

	re-two

	# This is the commit message #3:

	re-three

	# Please enter the commit message for the squash changes. Lines starting
	# with ${SQ}#${SQ} will be ignored, and an empty message aborts the commit.
	# Changes to be committed:
	#	modified:   file
	#
	EOF
	test_cmp expect edited &&
	check_log_subjects -1 <<-\EOF
	combined
	EOF
'

test_expect_success '--reedit-message handles fixup!, squash! and amend! like rebase' '
	git reset --hard start &&
	test_commit --no-tag mark-base file b &&
	stage_file c &&
	commit_with_message "fixup! mark-base\n\nfixup body\n" &&
	stage_file d &&
	commit_with_message "squash! mark-base\n\nsquash remark\n" &&
	stage_file e &&
	commit_with_message "amend! mark-base\n\namended message\n" &&

	write_script editor <<-\EOF &&
	cat "$1" >edited
	EOF
	test_set_editor "$(pwd)/editor" &&
	git history squash --reedit-message start.. &&

	cat >expect <<-EOF &&
	# This is a combination of 4 commits.
	# This is the 1st commit message:

	mark-base

	# The commit message #2 will be skipped:

	# fixup! mark-base
	#
	# fixup body

	# This is the commit message #3:

	# squash! mark-base

	squash remark

	# This is the commit message #4:

	# amend! mark-base

	amended message

	# Please enter the commit message for the squash changes. Lines starting
	# with ${SQ}#${SQ} will be ignored, and an empty message aborts the commit.
	# Changes to be committed:
	#	modified:   file
	#
	EOF
	test_cmp expect edited &&
	check_log_messages -1 <<-\EOF
	mark-base

	squash remark

	amended message

	EOF
'

test_expect_success '--reedit-message groups fixups under their targets' '
	git reset --hard start &&
	test_commit --no-tag alpha file a1 &&
	test_commit --no-tag beta file b1 &&
	stage_file a2 &&
	commit_with_message "fixup! alpha\n" &&
	stage_file b2 &&
	commit_with_message "fixup! beta\n" &&

	write_script editor <<-\EOF &&
	cat "$1" >edited
	EOF
	test_set_editor "$(pwd)/editor" &&
	git history squash --reedit-message start.. &&

	cat >expect <<-EOF &&
	# This is a combination of 4 commits.
	# This is the 1st commit message:

	alpha

	# The commit message #2 will be skipped:

	# fixup! alpha

	# This is the commit message #3:

	beta

	# The commit message #4 will be skipped:

	# fixup! beta

	# Please enter the commit message for the squash changes. Lines starting
	# with ${SQ}#${SQ} will be ignored, and an empty message aborts the commit.
	# Changes to be committed:
	#	modified:   file
	#
	EOF
	test_cmp expect edited
'

test_expect_success '--reedit-message lets amend! replace its target message' '
	git reset --hard start &&
	test_commit --no-tag mark-base file b &&
	stage_file c &&
	commit_with_message "amend! mark-base\n\namended message\n" &&
	stage_file d &&
	commit_with_message "squash! mark-base\n\nsquash remark\n" &&

	write_script editor <<-\EOF &&
	cat "$1" >edited
	EOF
	test_set_editor "$(pwd)/editor" &&
	git history squash --reedit-message start.. &&

	cat >expect <<-EOF &&
	# This is a combination of 3 commits.
	# The 1st commit message will be skipped:

	# mark-base

	# This is the commit message #2:

	# amend! mark-base

	amended message

	# This is the commit message #3:

	# squash! mark-base

	squash remark

	# Please enter the commit message for the squash changes. Lines starting
	# with ${SQ}#${SQ} will be ignored, and an empty message aborts the commit.
	# Changes to be committed:
	#	modified:   file
	#
	EOF
	test_cmp expect edited &&
	check_log_messages -1 <<-\EOF
	amended message

	squash remark

	EOF
'

test_expect_success '--reedit-message aborts on an empty message' '
	git reset --hard three &&
	head_before=$(git rev-parse HEAD) &&

	write_script editor <<-\EOF &&
	>"$1"
	EOF
	test_set_editor "$(pwd)/editor" &&
	test_must_fail git history squash --reedit-message start.. &&

	test_cmp_rev "$head_before" HEAD
'

test_expect_success '--update-refs=head only moves HEAD' '
	git reset --hard three &&
	git branch -f other HEAD &&
	other_before=$(git rev-parse other) &&

	git history squash --update-refs=head start.. &&

	check_commit_count start..HEAD 1 &&
	test_cmp_rev "$other_before" other
'

test_expect_success 'refuses to fold a range a ref points into' '
	git reset --hard three &&
	git branch -f mid HEAD~1 &&
	head_before=$(git rev-parse HEAD) &&

	test_must_fail git history squash start.. 2>err &&
	test_grep "error: .* points into the squashed range" err &&
	test_grep "hint: .*--update-refs=head" err &&
	test_cmp_rev "$head_before" HEAD &&

	git branch -D mid
'

test_expect_success 'advice.historyUpdateRefs silences the hint' '
	git reset --hard three &&
	git branch -f mid HEAD~1 &&
	head_before=$(git rev-parse HEAD) &&

	test_must_fail git -c advice.historyUpdateRefs=false \
		history squash start.. 2>err &&
	test_grep "points into the squashed range" err &&
	test_grep ! "hint:" err &&
	test_cmp_rev "$head_before" HEAD &&

	git branch -D mid
'

test_expect_success '--update-refs=head folds past a ref pointing into the range' '
	git reset --hard three &&
	git branch -f mid HEAD~1 &&
	mid_before=$(git rev-parse mid) &&

	git history squash --update-refs=head start.. &&

	check_commit_count start..HEAD 1 &&
	test_cmp_rev "$mid_before" mid &&

	git branch -D mid
'

test_expect_success 'refuses to fold a range a tag points into' '
	git reset --hard three &&
	git tag -f mark HEAD~1 &&
	head_before=$(git rev-parse HEAD) &&

	test_must_fail git history squash start.. 2>err &&
	test_grep "refs/tags/mark" err &&
	test_grep "points into the squashed range" err &&
	test_cmp_rev "$head_before" HEAD &&

	git tag -d mark
'

test_expect_success 'squashes a range whose internal merge has a single base' '
	git reset --hard start &&
	main=$(git symbolic-ref --short HEAD) &&
	test_commit --no-tag before-side file b &&
	git checkout -b inner-side &&
	test_commit --no-tag on-inner-side inner x &&
	git checkout "$main" &&
	test_commit --no-tag after-side file c &&
	git merge --no-ff -m merge inner-side &&
	git branch -D inner-side &&
	test_commit --no-tag after-merge file d &&
	tip_tree=$(git rev-parse HEAD^{tree}) &&

	git history squash start.. &&

	check_commit_count start..HEAD 1 &&
	check_log_subjects -1 <<-\EOF &&
	before-side
	EOF
	test "$tip_tree" = "$(git rev-parse HEAD^{tree})" &&
	test_path_is_file inner
'

test_expect_success 'folds a merge of a branch that forked at the base' '
	git reset --hard start &&
	main=$(git symbolic-ref --short HEAD) &&
	git checkout -b base-fork-side &&
	test_commit --no-tag base-fork-side side x &&
	git checkout "$main" &&
	test_commit --no-tag base-fork-main file b &&
	git merge --no-ff -m "merge base-fork-side" base-fork-side &&
	git branch -D base-fork-side &&
	test_commit --no-tag base-fork-tail file c &&
	tip_tree=$(git rev-parse HEAD^{tree}) &&

	git history squash start.. &&

	check_commit_count start..HEAD 1 &&
	test_cmp_rev start HEAD^ &&
	test "$tip_tree" = "$(git rev-parse HEAD^{tree})" &&
	test_path_is_file side
'

test_expect_success 'refuses a merge whose other parent is outside the range' '
	git reset --hard start &&
	main=$(git symbolic-ref --short HEAD) &&
	git checkout -b outside-parent &&
	test_commit --no-tag outside-parent outside x &&
	git checkout "$main" &&
	test_commit --no-tag outside-main file b &&
	base=$(git rev-parse HEAD) &&
	test_commit --no-tag outside-mid file c &&
	git merge --no-ff -m "merge outside-parent" outside-parent &&
	git branch -D outside-parent &&
	merged=$(git rev-parse HEAD) &&

	test_must_fail git history squash "$base.." 2>err &&
	test_grep "more than one base" err &&
	test_cmp_rev "$merged" HEAD
'

test_expect_success 'folds a range whose tip is a merge commit' '
	git reset --hard start &&
	main=$(git symbolic-ref --short HEAD) &&
	test_commit --no-tag tipmerge-base file b &&
	git checkout -b tipmerge-side &&
	test_commit --no-tag tipmerge-side side x &&
	git checkout "$main" &&
	test_commit --no-tag tipmerge-main file c &&
	git merge --no-ff -m "merge tipmerge-side" tipmerge-side &&
	git branch -D tipmerge-side &&
	tip_tree=$(git rev-parse HEAD^{tree}) &&

	git history squash start.. &&

	check_commit_count start..HEAD 1 &&
	test "$tip_tree" = "$(git rev-parse HEAD^{tree})" &&
	test_path_is_file side
'

test_expect_success 'folds a range whose base is a merge commit' '
	git reset --hard start &&
	main=$(git symbolic-ref --short HEAD) &&
	git checkout -b basemerge-side &&
	test_commit --no-tag basemerge-side side x &&
	git checkout "$main" &&
	test_commit --no-tag basemerge-main file b &&
	git merge --no-ff -m "merge basemerge-side" basemerge-side &&
	git branch -D basemerge-side &&
	base=$(git rev-parse HEAD) &&
	test_commit --no-tag basemerge-one file c &&
	test_commit --no-tag basemerge-two file d &&
	tip_tree=$(git rev-parse HEAD^{tree}) &&

	git history squash "$base.." &&

	check_commit_count "$base..HEAD" 1 &&
	test_cmp_rev "$base" HEAD^ &&
	test "$tip_tree" = "$(git rev-parse HEAD^{tree})"
'

test_expect_success 'folds a range with two interior merges' '
	git reset --hard start &&
	main=$(git symbolic-ref --short HEAD) &&
	test_commit --no-tag two-merge-a file a1 &&
	git checkout -b two-merge-s1 &&
	test_commit --no-tag two-merge-s1 s1 x &&
	git checkout "$main" &&
	git merge --no-ff -m "merge s1" two-merge-s1 &&
	test_commit --no-tag two-merge-b file b1 &&
	git checkout -b two-merge-s2 &&
	test_commit --no-tag two-merge-s2 s2 y &&
	git checkout "$main" &&
	git merge --no-ff -m "merge s2" two-merge-s2 &&
	git branch -D two-merge-s1 two-merge-s2 &&
	tip_tree=$(git rev-parse HEAD^{tree}) &&

	git history squash start.. &&

	check_commit_count start..HEAD 1 &&
	test "$tip_tree" = "$(git rev-parse HEAD^{tree})" &&
	test_path_is_file s1 &&
	test_path_is_file s2
'

test_expect_success 'folds a range with a nested merge' '
	git reset --hard start &&
	main=$(git symbolic-ref --short HEAD) &&
	git checkout -b nested-outer &&
	test_commit --no-tag nested-outer outer x &&
	git checkout -b nested-inner &&
	test_commit --no-tag nested-inner inner y &&
	git checkout nested-outer &&
	git merge --no-ff -m "merge inner" nested-inner &&
	git checkout "$main" &&
	test_commit --no-tag nested-main file b1 &&
	git merge --no-ff -m "merge outer" nested-outer &&
	git branch -D nested-outer nested-inner &&
	tip_tree=$(git rev-parse HEAD^{tree}) &&

	git history squash start.. &&

	check_commit_count start..HEAD 1 &&
	test "$tip_tree" = "$(git rev-parse HEAD^{tree})" &&
	test_path_is_file outer &&
	test_path_is_file inner
'

test_expect_success 'folds a range with an octopus merge' '
	git reset --hard start &&
	main=$(git symbolic-ref --short HEAD) &&
	test_commit --no-tag octo-base file a1 &&
	git checkout -b octo-1 &&
	test_commit --no-tag octo-1 o1 x &&
	git checkout "$main" &&
	git checkout -b octo-2 &&
	test_commit --no-tag octo-2 o2 y &&
	git checkout "$main" &&
	git merge --no-ff -m octopus octo-1 octo-2 &&
	git branch -D octo-1 octo-2 &&
	tip_tree=$(git rev-parse HEAD^{tree}) &&

	git history squash start.. &&

	check_commit_count start..HEAD 1 &&
	test "$tip_tree" = "$(git rev-parse HEAD^{tree})" &&
	test_path_is_file o1 &&
	test_path_is_file o2
'

test_expect_success 'refuses an octopus merge with an arm forked before the base' '
	git reset --hard start &&
	main=$(git symbolic-ref --short HEAD) &&
	git checkout -b octo-pre &&
	test_commit octo-pre-side pside x &&
	git checkout "$main" &&
	test_commit octo-pre-main file b1 &&
	octo_base=$(git rev-parse HEAD) &&
	git checkout -b octo-within &&
	test_commit --no-tag octo-within wside y &&
	git checkout "$main" &&
	git merge --no-ff -m octopus octo-pre octo-within &&
	merged=$(git rev-parse HEAD) &&
	git branch -D octo-pre octo-within &&

	test_must_fail git history squash "$octo_base.." 2>err &&
	test_grep "more than one base" err &&
	test_cmp_rev "$merged" HEAD
'

test_expect_success 'refuses when a descendant above the range is a merge' '
	git reset --hard start &&
	main=$(git symbolic-ref --short HEAD) &&
	test_commit --no-tag desc-one file b &&
	test_commit --no-tag desc-two file c &&
	git tag desc-tip &&
	git checkout -b desc-above &&
	test_commit --no-tag desc-above above x &&
	git checkout "$main" &&
	test_commit --no-tag desc-main file d &&
	git merge --no-ff -m "merge desc-above" desc-above &&
	git branch -D desc-above &&
	head_before=$(git rev-parse HEAD) &&

	test_must_fail git history squash start..desc-tip 2>err &&
	test_grep "merge commits is not supported" err &&
	test_cmp_rev "$head_before" HEAD
'

test_expect_success 'refuses to fold a range a ref points into at a merge' '
	git reset --hard start &&
	main=$(git symbolic-ref --short HEAD) &&
	test_commit --no-tag refmerge-base file b &&
	git checkout -b refmerge-side &&
	test_commit --no-tag refmerge-side side x &&
	git checkout "$main" &&
	test_commit --no-tag refmerge-main file c &&
	git merge --no-ff -m "interior merge" refmerge-side &&
	git branch -D refmerge-side &&
	git branch at-merge HEAD &&
	test_commit --no-tag refmerge-tail file d &&
	head_before=$(git rev-parse HEAD) &&

	test_must_fail git history squash start.. 2>err &&
	test_grep "at-merge" err &&
	test_grep "points into the squashed range" err &&
	test_cmp_rev "$head_before" HEAD &&

	git branch -D at-merge
'

test_done
