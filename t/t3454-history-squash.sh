#!/bin/sh

test_description='tests for git-history squash subcommand'

. ./test-lib.sh

test_expect_success 'setup linear history touching two files' '
	test_commit base file a &&
	git tag start &&
	test_commit one other x &&
	test_commit two file c &&
	test_commit three file d
'

test_expect_success 'errors on missing range argument' '
	test_must_fail git history squash 2>err &&
	test_grep "command expects a single revision range" err
'

test_expect_success 'errors on too many arguments' '
	test_must_fail git history squash start.. HEAD 2>err &&
	test_grep "command expects a single revision range" err
'

test_expect_success 'errors on an empty range' '
	test_must_fail git history squash HEAD..HEAD 2>err &&
	test_grep "the range .* is empty" err
'

test_expect_success 'errors when the range includes the root commit' '
	test_must_fail git history squash HEAD 2>err &&
	test_grep "cannot squash the root commit" err
'

test_expect_success 'squashes a range into a single commit without changing the tree' '
	git reset --hard three &&
	tip_tree=$(git rev-parse HEAD^{tree}) &&

	git history squash start.. &&

	git rev-list --count start..HEAD >count &&
	echo 1 >expect &&
	test_cmp expect count &&
	test_cmp_rev start HEAD^ &&
	test "$tip_tree" = "$(git rev-parse HEAD^{tree})" &&
	git log --format="%s" -1 >subject &&
	echo one >expect &&
	test_cmp expect subject &&
	git reflog >reflog &&
	test_grep "squash: updating" reflog
'

test_expect_success 'squashes an interior range and replays descendants verbatim' '
	git reset --hard three &&
	final_tree=$(git rev-parse HEAD^{tree}) &&

	git history squash start..@~1 &&

	git log --format="%s" start..HEAD >actual &&
	cat >expect <<-\EOF &&
	three
	one
	EOF
	test_cmp expect actual &&

	test_cmp_rev start HEAD~2 &&
	test "$final_tree" = "$(git rev-parse HEAD^{tree})"
'

test_expect_success 'squashes when the base is the root commit' '
	git reset --hard three &&
	root=$(git rev-list --max-parents=0 HEAD) &&
	tip_tree=$(git rev-parse HEAD^{tree}) &&

	git history squash "$root.." &&

	git rev-list --count "$root..HEAD" >count &&
	echo 1 >expect &&
	test_cmp expect count &&
	test_cmp_rev "$root" HEAD^ &&
	test "$tip_tree" = "$(git rev-parse HEAD^{tree})"
'

test_expect_success 'squashing a single-commit range replays the rest' '
	git reset --hard three &&
	tip_tree=$(git rev-parse HEAD^{tree}) &&

	git history squash start..@~2 &&

	git log --format="%s" start..HEAD >actual &&
	cat >expect <<-\EOF &&
	three
	two
	one
	EOF
	test_cmp expect actual &&
	test "$tip_tree" = "$(git rev-parse HEAD^{tree})"
'

test_expect_success 'reuses the message of a fixup! commit in the range' '
	git reset --hard start &&
	test_commit reg1 file b &&
	git commit --allow-empty -m "fixup! reg1" &&
	test_commit reg2 file c &&

	git history squash start.. &&

	git log --format="%s" -1 >actual &&
	echo reg1 >expect &&
	test_cmp expect actual
'

test_expect_success 'keeps the oldest message even if it is a fixup!' '
	git reset --hard start &&
	test_commit --no-tag "fixup! something" file b &&
	test_commit tail file c &&

	git history squash start.. &&

	git log --format="%s" -1 >actual &&
	echo "fixup! something" >expect &&
	test_cmp expect actual
'

test_expect_success 'preserves authorship of the oldest commit' '
	git reset --hard start &&
	GIT_AUTHOR_NAME=Squasher GIT_AUTHOR_EMAIL=squash@example.com \
		test_commit oldest file b &&
	test_commit newest file c &&

	git history squash start.. &&

	git log -1 --format="%an <%ae>" >actual &&
	echo "Squasher <squash@example.com>" >expect &&
	test_cmp expect actual
'

test_expect_success '--reedit-message offers every folded-in message' '
	git reset --hard start &&
	echo b >file &&
	git add file &&
	git commit -m "re-one subject" -m "re-one body line" &&
	test_commit re-two file c &&
	test_commit re-three file d &&

	write_script editor <<-\EOF &&
	cp "$1" buffer &&
	echo combined >"$1"
	EOF
	test_set_editor "$(pwd)/editor" &&
	git history squash --reedit-message start.. &&

	grep "re-one subject" buffer &&
	grep "re-one body line" buffer &&
	grep re-two buffer &&
	grep re-three buffer &&
	git log --format="%s" -1 >actual &&
	echo combined >expect &&
	test_cmp expect actual
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

test_expect_success '--dry-run predicts the rewrite without performing it' '
	git reset --hard three &&
	head_before=$(git rev-parse HEAD) &&

	git history squash --dry-run start.. >out &&
	grep "^update refs/heads/" out >update &&
	predicted=$(awk "{print \$3}" update) &&
	test_cmp_rev "$head_before" HEAD &&

	git history squash start.. &&
	test "$predicted" = "$(git rev-parse HEAD)"
'

test_expect_success '--update-refs=head only moves HEAD' '
	git reset --hard three &&
	git branch -f other HEAD &&
	other_before=$(git rev-parse other) &&

	git history squash --update-refs=head start.. &&

	git rev-list --count start..HEAD >count &&
	echo 1 >expect &&
	test_cmp expect count &&
	test_cmp_rev "$other_before" other
'

test_expect_success '--update-refs=branches moves a branch pointing into the range' '
	git reset --hard three &&
	git branch -f mid HEAD~2 &&
	mid_before=$(git rev-parse mid) &&

	git history squash start..@~1 &&

	test_cmp_rev "$mid_before" mid &&
	test_commit_message mid -m one
'

test_expect_success 'squashes a range whose internal merge has a single base' '
	git reset --hard start &&
	test_commit before-side file b &&
	git checkout -b inner-side &&
	test_commit on-inner-side inner x &&
	git checkout - &&
	test_commit after-side file c &&
	git merge --no-ff -m merge inner-side &&
	test_commit after-merge file d &&
	tip_tree=$(git rev-parse HEAD^{tree}) &&

	git history squash start.. &&

	git rev-list --count start..HEAD >count &&
	echo 1 >expect &&
	test_cmp expect count &&
	git log --format="%s" -1 >subject &&
	echo before-side >expect &&
	test_cmp expect subject &&
	test "$tip_tree" = "$(git rev-parse HEAD^{tree})" &&
	test_path_is_file inner
'

test_expect_success 'refuses to squash a range with more than one base' '
	git reset --hard start &&
	head_before=$(git rev-parse HEAD) &&
	git checkout -b forked-before &&
	test_commit forked-side fside x &&
	git checkout - &&
	test_commit forked-main file b &&
	git merge --no-ff -m merge forked-before &&
	merged=$(git rev-parse HEAD) &&

	test_must_fail git history squash forked-main.. 2>err &&
	test_grep "more than one base" err &&
	test_cmp_rev "$merged" HEAD
'

test_done
