#!/bin/sh
#
# git cs: version captured context in a history separate from code.

SUBDIRECTORY_OK=Yes
USAGE='commit <branch> <payload> [--expect <checkpoint>]
   or: show <checkpoint>
   or: log <branch>
   or: diff <checkpoint> <checkpoint>
   or: branch <name> <checkpoint> [--through <chunk>]
   or: reset <branch> <checkpoint>
   or: reflog <branch>
   or: tag <name> <checkpoint>
   or: label (add | remove) <name> <checkpoint>...
   or: label resolve <name>
   or: merge <branch> <checkpoint> [--result <payload>] [--no-ff]
   or: merge-preview <checkpoint> <checkpoint>
   or: rebase <branch> <onto>
   or: cherry-pick <branch> <checkpoint>
   or: revert <branch> <checkpoint>
   or: pin <checkpoint>...
   or: unpin <checkpoint>...
   or: commit-code --context <checkpoint> -- <git-commit-options>
   or: fsck [<code-revs>...]
   or: links (--code <commit> | --context <checkpoint> [<code-refs>...])
   or: push <remote> [<code-refspec>...]
   or: fetch <remote>
   or: bundle <file> [<code-refs>...]'

. git-sh-setup

ref () {
	echo "refs/cs/heads/$1"
}

# Store $payload as a checkpoint on top of $tip (and $merge) without moving any ref.
checkpoint () {
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
	commit=$(echo "${msg:-cs checkpoint}" | git commit-tree $tree ${tip:+-p $tip} ${merge:+-p $merge}) || exit
	cmd_pin $(jq -r '.chunks[].sources[]?' "$payload") || exit
	echo $commit
}

cmd_commit () {
	branch=$1 payload=$2 expect=$4
	test -n "$payload" || usage
	tip=$(git rev-parse -q --verify "$(ref "$branch")")
	commit=$(checkpoint) || exit
	git update-ref --create-reflog "$(ref "$branch")" $commit "${expect:-$tip}" || die "STALE_TIP"
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
	start=$2
	test "$3" != --through || {
		tip=$(git rev-parse --verify "$2^{commit}") || exit
		merge= msg="cs derived view: prefix through $4" payload="$GIT_DIR/CS_DERIVED"
		cmd_show $tip | jq -c --arg m "$4" '
			(.active | indices($m)[0] // error("not active: \($m)")) as $i |
			.active |= .[:$i + 1] | .active as $a | .chunks |= map(select(.id | IN($a[])))' >"$payload" &&
		start=$(checkpoint) || exit
	}
	git update-ref --create-reflog "$(ref "$1")" "$start" ""
}

cmd_reset () {
	git update-ref --create-reflog refs/cs/recovery/"$1" "$(ref "$1")" &&
	git update-ref --create-reflog "$(ref "$1")" "$2"
}

cmd_reflog () {
	git reflog show "$(ref "$1")"
}

cmd_tag () {
	git update-ref refs/cs/tags/"$1" "$(git rev-parse --verify "$2^{commit}")" ""
}

cmd_label () {
	op=$1 name=$2
	shift 2
	for c in "$@"
	do
		c=$(git rev-parse --verify "$c^{commit}") || exit
		case "$op" in
		add) git update-ref refs/cs/labels/$name/$c $c ;;
		remove) git update-ref -d refs/cs/labels/$name/$c ;;
		*) usage ;;
		esac || exit
	done
	test "$op" != resolve || git for-each-ref --format='%(objectname)' refs/cs/labels/$name/
}

# Ordered union: keep each side's order, break ties by chunk ID.
union='def union($a; $b):
[$a.active, $b.active | [.[:-1], .[1:]] | transpose[]] as $edges |
{out: [], left: ($a.active + $b.active | unique)} |
until(.left == [];
	.left as $left |
	(first($left[] | select(. as $n | all($edges[];
		.[1] != $n or (.[0] as $p | all($left[]; . != $p))))) // error("order conflict")) as $n |
	.out += [$n] | .left -= [$n]) |
.out as $active |
{schema: "cs/1", active: $active, chunks: [$a.chunks + $b.chunks | group_by(.id)[] |
	if unique | length > 1 then error("identity conflict: \(.[0].id)") else .[0] end |
	select(.id | IN($active[]))]};
'

# Apply the change $from -> $to on top of $cur; removals must still be present.
replay='
($from.active - $to.active) as $rm | ($to.active - $from.active) as $add |
if all($rm[]; IN($cur.active[])) then . else error("replay conflict: removed chunk missing") end |
union($cur; $to | .active |= map(select(IN($cur.active[], $add[])))) | .active -= $rm |
.chunks |= map(select(.id | IN($rm[]) | not))
'

# Print a new checkpoint on $1 that repeats the change from $2 to $3.
replay () {
	tip=$1 merge= payload="$GIT_DIR/CS_REPLAY"
	jq -n --argjson cur "$(cmd_show $1)" --argjson from "$(cmd_show $2)" --argjson to "$(cmd_show $3)" \
		"$union $replay" >"$payload" && checkpoint
}

cmd_rebase () {
	old=$(git rev-parse --verify "$(ref "$1")") &&
	new=$(git rev-parse --verify "$2^{commit}") || exit
	test -z "$(git rev-list --min-parents=2 $new..$old)" || die "cannot replay merge checkpoints"
	for c in $(git rev-list --reverse $new..$old)
	do
		new=$(replay $new $c^ $c) || exit
	done
	git update-ref --create-reflog refs/cs/recovery/"$1" $old &&
	git update-ref --create-reflog "$(ref "$1")" $new $old && echo $new
}

# Move $1 to a replay of $2 -> $3 on top of it.
apply () {
	old=$(git rev-parse --verify "$(ref "$1")") &&
	new=$(replay $old "$2" "$3") &&
	git update-ref --create-reflog "$(ref "$1")" $new $old && echo $new
}

cmd_cherry_pick () {
	apply "$1" "$2^" "$2"
}

cmd_revert () {
	apply "$1" "$2" "$2^"
}

cmd_merge_preview () {
	jq -cn --argjson a "$(cmd_show "$1")" --argjson b "$(cmd_show "$2")" "$union union(\$a; \$b)"
}

cmd_merge () {
	branch=$1 result= noff=
	ours=$(git rev-parse --verify "$(ref "$branch")") &&
	theirs=$(git rev-parse --verify "$2^{commit}") || exit
	shift 2
	while test $# -gt 0
	do
		case "$1" in
		--result) result=$2; shift ;;
		--no-ff) noff=1 ;;
		*) usage ;;
		esac
		shift
	done
	git merge-base --is-ancestor $theirs $ours && return
	if git merge-base --is-ancestor $ours $theirs
	then
		test -n "$noff" || {
			git update-ref --create-reflog "$(ref "$branch")" $theirs $ours && echo $theirs
			return
		}
		result="$GIT_DIR/CS_MERGE"
		cmd_show $theirs >"$result" || exit
	fi
	test -n "$result" || {
		result="$GIT_DIR/CS_MERGE"
		cmd_merge_preview $ours $theirs >"$result" || exit
	}
	merge=$theirs cmd_commit "$branch" "$result" --expect $ours
}

cmd_pin () {
	for c in "$@"
	do
		c=$(git rev-parse --verify "$c^{commit}") &&
		git update-ref refs/cs/pins/$c $c || exit
	done
}

cmd_unpin () {
	for c in "$@"
	do
		git update-ref -d refs/cs/pins/$(git rev-parse --verify "$c^{commit}") || exit
	done
}

cmd_commit_code () {
	ctx=$(git rev-parse --verify "$2^{commit}") &&
	git cat-file -e $ctx:context.json || die "not a cs checkpoint: $2"
	shift 2
	test "$1" = -- && shift
	cmd_pin $ctx &&
	git -c trailer.Context-Shadow.ifExists=replace commit --trailer "Context-Shadow: $ctx" "$@"
}

cmd_fsck () {
	bad=$(git for-each-ref --format='%(objectname) %(refname)' refs/cs/ |
	while read -r c name
	do
		git cat-file -e $c:context.json 2>/dev/null || echo "not a checkpoint: $name"
	done
	git log --format='%H %(trailers:key=Context-Shadow,valueonly,separator=%x20)' "${@:---branches}" |
	while read -r code ctx
	do
		case "$ctx" in
		("") echo "unlinked: $code" ;;
		(*" "*) echo "several links: $code" ;;
		(*) git rev-parse -q --verify refs/cs/pins/$ctx >/dev/null || echo "unpinned link: $code -> $ctx" ;;
		esac
	done)
	test -z "$bad" || die "$bad"
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
	remote=$1
	shift
	git push --atomic "$remote" "refs/cs/heads/*:refs/cs/heads/*" "refs/cs/pins/*:refs/cs/pins/*" "refs/cs/tags/*:refs/cs/tags/*" "refs/cs/labels/*:refs/cs/labels/*" "$@"
}

cmd_fetch () {
	git fetch "$1" "refs/cs/heads/*:refs/cs/remotes/$1/*" "refs/cs/pins/*:refs/cs/pins/*" "refs/cs/tags/*:refs/cs/tags/*" "refs/cs/labels/*:refs/cs/labels/*"
}

cmd_bundle () {
	file=$1
	shift
	{
		git for-each-ref --format='%(refname)' refs/cs/heads refs/cs/pins refs/cs/tags refs/cs/labels
		printf '%s\n' "$@"
	} | git bundle create "$file" --stdin
}

cmd=$1
shift
case "$cmd" in
commit|show|log|diff|branch|reset|reflog|tag|label|merge|rebase|revert|pin|unpin|fsck|links|push|fetch|bundle)
	cmd_$cmd "$@" ;;
commit-code)
	cmd_commit_code "$@" ;;
cherry-pick)
	cmd_cherry_pick "$@" ;;
merge-preview)
	cmd_merge_preview "$@" ;;
*)
	usage ;;
esac
