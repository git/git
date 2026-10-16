#!/bin/sh
#
# git cs: version captured context in a history separate from code.

SUBDIRECTORY_OK=Yes
USAGE='commit <branch> <payload> [--expect <checkpoint>]
   or: show <checkpoint>
   or: log <branch>
   or: branch <name> <checkpoint>
   or: reset <branch> <checkpoint>'

. git-sh-setup

ref () {
	echo "refs/cs/heads/$1"
}

cmd_commit () {
	branch=$1 payload=$2 expect=$4
	test -n "$payload" || usage
	tip=$(git rev-parse -q --verify "$(ref "$branch")")
	tree=$(jq -c '.chunks[]' "$payload" | while read -r chunk
	do
		id=$(echo "$chunk" | jq -r .id)
		blob=$(echo "$chunk" | jq -cS . | git hash-object -w --stdin)
		printf '100644 blob %s\t%s.json\n' "$blob" "$id"
	done | git mktree) &&
	manifest=$(jq -cS '{schema, active}' "$payload" | git hash-object -w --stdin) &&
	tree=$(printf '100644 blob %s\tcontext.json\n040000 tree %s\tchunks\n' "$manifest" "$tree" | git mktree) &&
	commit=$(echo "cs checkpoint" | git commit-tree $tree ${tip:+-p $tip}) || exit
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

cmd_branch () {
	git update-ref "$(ref "$1")" "$2" ""
}

cmd_reset () {
	git update-ref refs/cs/recovery/"$1" "$(ref "$1")" &&
	git update-ref "$(ref "$1")" "$2"
}

cmd=$1
shift
case "$cmd" in
commit|show|log|branch|reset)
	cmd_$cmd "$@" ;;
*)
	usage ;;
esac
