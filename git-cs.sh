#!/bin/sh
#
# git cs: version captured context in a history separate from code.

SUBDIRECTORY_OK=Yes
USAGE='commit <branch> <payload> [--expect <checkpoint>]
   or: show <checkpoint>
   or: log <branch>
   or: diff <checkpoint> <checkpoint>
   or: branch <name> <checkpoint>
   or: reset <branch> <checkpoint>
   or: merge <branch> <checkpoint> [--result <payload>]
   or: commit-code --context <checkpoint> -- <git-commit-options>
   or: links (--code <commit> | --context <checkpoint> [<code-refs>...])
   or: push <remote>
   or: fetch <remote>'

. git-sh-setup

ref () {
	echo "refs/cs/heads/$1"
}

cmd_commit () {
	branch=$1 payload=$2 expect=$4
	test -n "$payload" || usage
	tip=$(git rev-parse -q --verify "$(ref "$branch")")
	jq -e '.schema == "cs/1" and (.active | length == (unique | length)) and
		([.chunks[].id] as $ids | all(.active[]; IN($ids[])))' "$payload" >/dev/null ||
	die "invalid cs/1 payload: $payload"
	entries=$(jq -c '.chunks[]' "$payload" | while read -r chunk
	do
		id=$(echo "$chunk" | jq -r .id)
		blob=$(echo "$chunk" | jq -cS . | git hash-object -w --stdin)
		old=${tip:+$(git rev-parse -q --verify $tip:chunks/$id.json)}
		test "${old:-$blob}" = $blob || die "identity conflict: $id"
		printf '100644 blob %s\t%s.json\n' "$blob" "$id"
	done) &&
	tree=$(printf %s "$entries" | git mktree) &&
	manifest=$(jq -cS '{schema, active}' "$payload" | git hash-object -w --stdin) &&
	tree=$(printf '100644 blob %s\tcontext.json\n040000 tree %s\tchunks\n' "$manifest" "$tree" | git mktree) &&
	commit=$(echo "cs checkpoint" | git commit-tree $tree ${tip:+-p $tip} ${merge:+-p $merge}) || exit
	for src in $(jq -r '.chunks[].sources[]?' "$payload")
	do
		git update-ref refs/cs/pins/$src $src || exit
	done
	git update-ref "$(ref "$branch")" $commit "${expect:-$tip}" || die "STALE_TIP"
	echo $commit
}

cmd_show () {
	rev=$(git rev-parse --verify "$1^{commit}") || exit
	git ls-tree --name-only $rev chunks/ |
	while read -r path
	do
		git cat-file blob $rev:$path
	done |
	jq -cs --argjson ctx "$(git cat-file blob $rev:context.json)" '$ctx + {chunks: .}'
}

cmd_log () {
	git log --format='%H %ad' "$(ref "$1")"
}

cmd_diff () {
	jq -rn --argjson a "$(cmd_show "$1")" --argjson b "$(cmd_show "$2")" '
		($a.active - $b.active | map("- " + .)[]),
		($b.active - $a.active | map("+ " + .)[]),
		if ($a.active - ($a.active - $b.active)) != ($b.active - ($b.active - $a.active))
		then "~ reordered" else empty end'
}

cmd_branch () {
	git update-ref "$(ref "$1")" "$2" ""
}

cmd_reset () {
	git update-ref refs/cs/recovery/"$1" "$(ref "$1")" &&
	git update-ref "$(ref "$1")" "$2"
}

# Ordered union: keep each side's order, break ties by chunk ID.
union='
[$a.active, $b.active | [.[:-1], .[1:]] | transpose[]] as $edges |
{out: [], left: ($a.active + $b.active | unique)} |
until(.left == [];
	.left as $left |
	(first($left[] | select(. as $n | all($edges[];
		.[1] != $n or (.[0] as $p | all($left[]; . != $p))))) // error("order conflict")) as $n |
	.out += [$n] | .left -= [$n]) |
.out as $active |
{schema: "cs/1", active: $active, chunks: [$a.chunks + $b.chunks | group_by(.id)[] |
	if unique | length > 1 then error("identity conflict: \(.[0].id)") else .[0] end]}
'

cmd_merge () {
	branch=$1 result=$4
	ours=$(git rev-parse --verify "$(ref "$branch")") &&
	theirs=$(git rev-parse --verify "$2^{commit}") || exit
	git merge-base --is-ancestor $theirs $ours && return
	if git merge-base --is-ancestor $ours $theirs
	then
		git update-ref "$(ref "$branch")" $theirs $ours && echo $theirs
		return
	fi
	test -n "$result" || {
		result="$GIT_DIR/CS_MERGE"
		jq -n --argjson a "$(cmd_show $ours)" --argjson b "$(cmd_show $theirs)" "$union" >"$result" || exit
	}
	merge=$theirs cmd_commit "$branch" "$result" --expect $ours
}

cmd_commit_code () {
	ctx=$(git rev-parse --verify "$2^{commit}") &&
	git cat-file -e $ctx:context.json || die "not a cs checkpoint: $2"
	shift 2
	test "$1" = -- && shift
	git update-ref refs/cs/pins/$ctx $ctx &&
	git commit --trailer "Context-Shadow: $ctx" "$@"
}

cmd_links () {
	case "$1" in
	--code)
		git log -1 --format='%(trailers:key=Context-Shadow,valueonly)' "$2" ;;
	--context)
		ctx=$(git rev-parse --verify "$2^{commit}") || exit
		shift 2
		git log --format=%H --grep="^Context-Shadow: $ctx$" "${@:---branches}" ;;
	*)
		usage ;;
	esac
}

cmd_push () {
	git push "$1" "refs/cs/heads/*:refs/cs/heads/*" "refs/cs/pins/*:refs/cs/pins/*"
}

cmd_fetch () {
	git fetch "$1" "refs/cs/heads/*:refs/cs/remotes/$1/*" "refs/cs/pins/*:refs/cs/pins/*"
}

cmd=$1
shift
case "$cmd" in
commit|show|log|diff|branch|reset|merge|links|push|fetch)
	cmd_$cmd "$@" ;;
commit-code)
	cmd_commit_code "$@" ;;
*)
	usage ;;
esac
