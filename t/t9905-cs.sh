#!/bin/sh

test_description='git cs context histories'

. ./test-lib.sh

if ! command -v jq >/dev/null
then
	skip_all='skipping git cs tests, jq not available'
	test_done
fi

# Write a payload with the given active list over a fixed chunk set.
payload () {
	printf '{"schema":"cs/1","active":%s,"chunks":[{"id":"m1"},{"id":"m2"},{"id":"a1"},{"id":"b1"}]}' "$1" >"$2"
}

ctx () {
	payload "$2" p.json &&
	git cs commit "$1" p.json
}

active () {
	git cs show "$1" | jq -c .active
}

test_expect_success 'commit stores context without touching code refs' '
	test_commit code &&
	base=$(ctx main "[\"m1\",\"m2\"]") &&
	test "$(git rev-parse HEAD)" = "$(git rev-parse code)" &&
	test "$(active $base)" = "[\"m1\",\"m2\"]" &&
	git cs log main >log &&
	test_line_count = 1 log
'

test_expect_success 'commit refuses a stale expected tip' '
	payload "[\"m1\"]" p.json &&
	tip=$(git cs commit main p.json --expect $base) &&
	test_must_fail git cs commit main p.json --expect $base &&
	test "$(git rev-parse refs/cs/heads/main)" = $tip
'

test_expect_success 'commit rejects malformed payloads' '
	payload "[\"m1\",\"m1\"]" dup.json &&
	payload "[\"zz\"]" missing.json &&
	echo "{\"schema\":\"cs/1\",\"active\":[\"m1\"],\"chunks\":[{\"id\":\"m1\",\"body\":\"x\"}]}" >changed.json &&
	test_must_fail git cs commit main dup.json &&
	test_must_fail git cs commit main missing.json &&
	test_must_fail git cs commit main changed.json
'

test_expect_success 'show round-trips through commit' '
	git cs show $base >out.json &&
	again=$(git cs commit main out.json) &&
	test "$(git rev-parse $base^{tree})" = "$(git rev-parse $again^{tree})"
'

test_expect_success 'an empty active view is valid' '
	payload "[]" empty.json &&
	git cs branch empty $base &&
	git cs commit empty empty.json &&
	test "$(active refs/cs/heads/empty)" = "[]"
'

test_expect_success 'diff reports dropped, added and reordered chunks' '
	b=$(ctx diffs "[\"m2\",\"m1\",\"a1\"]") &&
	git cs diff $base $b >actual &&
	printf "+ a1\n~ reordered\n" >expect &&
	test_cmp expect actual
'

test_expect_success 'branch, reset and reflog keep earlier tips' '
	git cs branch retry $base &&
	test_must_fail git cs branch retry $base &&
	tip=$(ctx retry "[\"m1\"]") &&
	git cs reset retry $base &&
	test "$(git rev-parse refs/cs/recovery/retry)" = $tip &&
	git cs reflog retry >log &&
	test_line_count = 3 log
'

test_expect_success 'branch --through creates a derived prefix view' '
	git cs branch cut $base --through m1 &&
	test "$(active refs/cs/heads/cut)" = "[\"m1\"]" &&
	git log -1 --format=%s refs/cs/heads/cut >msg &&
	grep "derived view" msg &&
	test_must_fail git cs branch cut2 $base --through nope
'

test_expect_success 'merge takes the ordered union of both sides' '
	git cs branch a $base &&
	git cs branch b $base &&
	ctx a "[\"m1\",\"m2\",\"a1\"]" &&
	ctx b "[\"m1\",\"m2\",\"b1\"]" &&
	git cs merge-preview refs/cs/heads/a refs/cs/heads/b >preview &&
	test "$(jq -c .active preview)" = "[\"m1\",\"m2\",\"a1\",\"b1\"]" &&
	m=$(git cs merge a refs/cs/heads/b) &&
	test "$(active $m)" = "[\"m1\",\"m2\",\"a1\",\"b1\"]" &&
	git rev-list --parents -1 $m >parents &&
	test $(wc -w <parents) = 3
'

test_expect_success 'merge reports order conflicts and keeps the branch' '
	git cs branch x $base &&
	git cs branch y $base &&
	ctx x "[\"m2\",\"m1\"]" &&
	ctx y "[\"m1\",\"m2\",\"b1\"]" &&
	old=$(git rev-parse refs/cs/heads/y) &&
	test_must_fail git cs merge y refs/cs/heads/x &&
	test "$(git rev-parse refs/cs/heads/y)" = $old
'

test_expect_success 'merge fast-forwards unless --no-ff' '
	git cs branch ff $base &&
	git cs branch noff $base &&
	git cs merge ff refs/cs/heads/x &&
	test "$(git rev-parse refs/cs/heads/ff)" = "$(git rev-parse refs/cs/heads/x)" &&
	git cs merge noff refs/cs/heads/x --no-ff &&
	test "$(active refs/cs/heads/noff)" = "[\"m2\",\"m1\"]" &&
	git rev-parse refs/cs/heads/noff^2
'

test_expect_success 'merge accepts a supplied result' '
	git cs branch c $base &&
	ctx c "[\"m1\",\"b1\"]" &&
	payload "[\"a1\"]" result.json &&
	git cs merge a refs/cs/heads/c --result result.json &&
	test "$(active refs/cs/heads/a)" = "[\"a1\"]"
'

test_expect_success 'rebase replays each change onto the new base' '
	git cs branch topic $base &&
	git cs branch up $base &&
	ctx topic "[\"m1\",\"m2\",\"a1\"]" &&
	ctx topic "[\"m2\",\"a1\"]" &&
	ctx up "[\"m1\",\"m2\",\"b1\"]" &&
	git cs rebase topic refs/cs/heads/up &&
	test "$(active refs/cs/heads/topic)" = "[\"m2\",\"a1\",\"b1\"]" &&
	git merge-base --is-ancestor refs/cs/heads/up refs/cs/heads/topic
'

test_expect_success 'rebase refuses removals that no longer apply' '
	ctx up "[\"m2\",\"b1\"]" &&
	git cs reset topic refs/cs/recovery/topic &&
	old=$(git rev-parse refs/cs/heads/topic) &&
	test_must_fail git cs rebase topic refs/cs/heads/up &&
	test "$(git rev-parse refs/cs/heads/topic)" = $old
'

test_expect_success 'rebase refuses merge checkpoints' '
	test_must_fail git cs rebase a $base
'

test_expect_success 'cherry-pick and revert replay one change' '
	git cs branch pick $base &&
	add=$(ctx b "[\"m1\",\"m2\",\"b1\",\"a1\"]") &&
	git cs cherry-pick pick $add &&
	test "$(active refs/cs/heads/pick)" = "[\"m1\",\"m2\",\"a1\"]" &&
	git cs revert pick refs/cs/heads/pick &&
	test "$(active refs/cs/heads/pick)" = "[\"m1\",\"m2\"]"
'

test_expect_success 'tags and labels name exact checkpoints' '
	git cs tag v1 $base &&
	test_must_fail git cs tag v1 $base &&
	git cs label add ticket $base refs/cs/heads/a &&
	git cs label resolve ticket >members &&
	test_line_count = 2 members &&
	git cs label remove ticket $base &&
	git cs label resolve ticket >members &&
	test_line_count = 1 members
'

test_expect_success 'summary sources are pinned' '
	printf "{\"schema\":\"cs/1\",\"active\":[\"s1\"],\"chunks\":[{\"id\":\"s1\",\"sources\":[\"%s\"]}]}" $base >summary.json &&
	git cs branch sum $base &&
	git cs commit sum summary.json &&
	git rev-parse refs/cs/pins/$base
'

test_expect_success 'commit-code links and pins the context' '
	git cs unpin $base &&
	echo change >file &&
	git add file &&
	git cs commit-code --context $base -- -m linked &&
	test "$(git cs links --code HEAD)" = $base &&
	test "$(git cs links --context $base)" = "$(git rev-parse HEAD)" &&
	git rev-parse refs/cs/pins/$base &&
	test_must_fail git cs commit-code --context HEAD -- -m nope
'

test_expect_success 'commit-code keeps exactly one link' '
	git cs commit-code --context refs/cs/heads/x -- --amend --no-edit &&
	test "$(git cs links --code HEAD)" = "$(git rev-parse refs/cs/heads/x)" &&
	git cs commit-code --context $base -- --amend --no-edit &&
	test "$(git cs links --code HEAD)" = $base
'

test_expect_success 'linked context survives branch deletion and gc' '
	git update-ref -d refs/cs/heads/main &&
	git gc --prune=now &&
	git cs show $base
'

test_expect_success 'fsck finds unpinned links and stray refs' '
	git cs fsck &&
	git cs unpin $base &&
	test_must_fail git cs fsck &&
	git cs pin $base &&
	git update-ref refs/cs/heads/bogus HEAD &&
	test_must_fail git cs fsck &&
	git update-ref -d refs/cs/heads/bogus
'

test_expect_success 'push and fetch carry context to a fresh receiver' '
	git init --bare hub.git &&
	git cs push hub.git HEAD:refs/heads/master &&
	git clone hub.git rx &&
	git -C rx cs fetch origin &&
	git -C rx cs show "$(git -C rx cs links --code HEAD)" &&
	git -C rx rev-parse refs/cs/remotes/origin/a refs/cs/tags/v1 &&
	git -C rx cs label resolve ticket
'

test_expect_success 'push is atomic across code and context' '
	git push -f hub.git "$(git commit-tree HEAD^{tree} -m other)":refs/heads/master &&
	before=$(git -C hub.git rev-parse refs/cs/heads/a) &&
	ctx a "[\"m1\"]" &&
	test_must_fail git cs push hub.git HEAD:refs/heads/master &&
	test "$(git -C hub.git rev-parse refs/cs/heads/a)" = $before
'

test_expect_success 'bundle carries context offline' '
	git cs bundle cs.bundle HEAD &&
	git init offline &&
	git -C offline fetch ../cs.bundle "refs/cs/*:refs/cs/*" HEAD:refs/heads/in &&
	git -C offline cs show "$(git -C offline cs links --code in)"
'

test_done
