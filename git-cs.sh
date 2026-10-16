#!/bin/sh
#
# git cs: version captured context in a history separate from code.

SUBDIRECTORY_OK=Yes
USAGE='commit <branch> <payload> [--expect <checkpoint>]'

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

cmd=$1
shift
case "$cmd" in
commit)
	cmd_$cmd "$@" ;;
*)
	usage ;;
esac
