#!/bin/sh
#
# Rewrite revision history
# Copyright (c) Petr Baudis, 2006
# Minimal changes to "port" it to core-git (c) Johannes Schindelin, 2007
#
# Lets you rewrite the revision history of the current branch, creating
# a new branch. You can specify a number of filters to modify the commits,
# files and trees.

set -e

USAGE="git-filter-branch [-d TEMPDIR] [FILTERS] DESTBRANCH [REV-RANGE]"
. git-sh-setup

map()
{
	# if it was not rewritten, take the original
	if test -r "$workdir/../map/$1"
	then
		cat "$workdir/../map/$1"
	else
		echo "$1"
	fi
}

# When piped a commit, output a script to set the ident of either
# "author" or "committer

set_ident () {
	lid="$(echo "$1" | tr "A-Z" "a-z")"
	uid="$(echo "$1" | tr "a-z" "A-Z")"
	pick_id_script='
		/^'$lid' /{
			s/'\''/'\''\\'\'\''/g
			h
			s/^'$lid' \([^<]*\) <[^>]*> .*$/\1/
			s/'\''/'\''\'\'\''/g
			s/.*/export GIT_'$uid'_NAME='\''&'\''/p

			g
			s/^'$lid' [^<]* <\([^>]*\)> .*$/\1/
			s/'\''/'\''\'\'\''/g
			s/.*/export GIT_'$uid'_EMAIL='\''&'\''/p

			g
			s/^'$lid' [^<]* <[^>]*> \(.*\)$/\1/
			s/'\''/'\''\'\'\''/g
			s/.*/export GIT_'$uid'_DATE='\''&'\''/p

			q
		}
	'

	LANG=C LC_ALL=C sed -ne "$pick_id_script"
	# Ensure non-empty id name.
	echo "[ -n \"\$GIT_${uid}_NAME\" ] || export GIT_${uid}_NAME=\"\${GIT_${uid}_EMAIL%%@*}\""
}

tempdir=.git-rewrite
filter_env=
filter_tree=
filter_index=
filter_parent=
filter_msg=cat
filter_commit='git commit-tree "$@"'
filter_tag_name=
filter_subdir=
while case "$#" in 0) usage;; esac
do
	case "$1" in
	--)
		shift
		break
		;;
	-*)
		;;
	*)
		break;
	esac

	# all switches take one argument
	ARG="$1"
	case "$#" in 1) usage ;; esac
	shift
	OPTARG="$1"
	shift

	case "$ARG" in
	-d)
		tempdir="$OPTARG"
		;;
	--env-filter)
		filter_env="$OPTARG"
		;;
	--tree-filter)
		filter_tree="$OPTARG"
		;;
	--index-filter)
		filter_index="$OPTARG"
		;;
	--parent-filter)
		filter_parent="$OPTARG"
		;;
	--msg-filter)
		filter_msg="$OPTARG"
		;;
	--commit-filter)
		filter_commit="$OPTARG"
		;;
	--tag-name-filter)
		filter_tag_name="$OPTARG"
		;;
	--subdirectory-filter)
		filter_subdir="$OPTARG"
		;;
	*)
		usage
		;;
	esac
done

dstbranch="$1"
shift
test -n "$dstbranch" || die "missing branch name"
git show-ref "refs/heads/$dstbranch" 2> /dev/null &&
	die "branch $dstbranch already exists"

test ! -e "$tempdir" || die "$tempdir already exists, please remove it"
mkdir -p "$tempdir/t"
cd "$tempdir/t"
workdir="$(pwd)"

case "$GIT_DIR" in
/*)
	;;
*)
	GIT_DIR="$(pwd)/../../$GIT_DIR"
	;;
esac
export GIT_DIR GIT_WORK_TREE=.

export GIT_INDEX_FILE="$(pwd)/../index"
git read-tree # seed the index file

ret=0


mkdir ../map # map old->new commit ids for rewriting parents

case "$filter_subdir" in
"")
	git rev-list --reverse --topo-order --default HEAD \
		--parents "$@"
	;;
*)
	git rev-list --reverse --topo-order --default HEAD \
		--parents --full-history "$@" -- "$filter_subdir"
esac > ../revs
commits=$(cat ../revs | wc -l | tr -d " ")

test $commits -eq 0 && die "Found nothing to rewrite"

i=0
while read commit parents; do
	i=$(($i+1))
	printf "\rRewrite $commit ($i/$commits)"

	case "$filter_subdir" in
	"")
		git read-tree -i -m $commit
		;;
	*)
		git read-tree -i -m $commit:"$filter_subdir"
	esac

	export GIT_COMMIT=$commit
	git cat-file commit "$commit" >../commit

	eval "$(set_ident AUTHOR <../commit)"
	eval "$(set_ident COMMITTER <../commit)"
	eval "$filter_env" < /dev/null

	if [ "$filter_tree" ]; then
		git checkout-index -f -u -a
		# files that $commit removed are now still in the working tree;
		# remove them, else they would be added again
		git ls-files -z --others | xargs -0 rm -f
		eval "$filter_tree" < /dev/null
		git diff-index -r $commit | cut -f 2- | tr '\n' '\0' | \
			xargs -0 git update-index --add --replace --remove
		git ls-files -z --others | \
			xargs -0 git update-index --add --replace --remove
	fi

	eval "$filter_index" < /dev/null

	parentstr=
	for parent in $parents; do
		for reparent in $(map "$parent"); do
			parentstr="$parentstr -p $reparent"
		done
	done
	if [ "$filter_parent" ]; then
		parentstr="$(echo "$parentstr" | eval "$filter_parent")"
	fi

	sed -e '1,/^$/d' <../commit | \
		eval "$filter_msg" | \
		sh -c "$filter_commit" "git commit-tree" $(git write-tree) \
			$parentstr > ../map/$commit
done <../revs

src_head=$(tail -n 1 ../revs | sed -e 's/ .*//')
target_head=$(head -n 1 ../map/$src_head)
case "$target_head" in
'')
	echo Nothing rewritten
	;;
*)
	git update-ref refs/heads/"$dstbranch" $target_head
	if [ $(cat ../map/$src_head | wc -l) -gt 1 ]; then
		echo "WARNING: Your commit filter caused the head commit to expand to several rewritten commits. Only the first such commit was recorded as the current $dstbranch head but you will need to resolve the situation now (probably by manually merging the other commits). These are all the commits:" >&2
		sed 's/^/	/' ../map/$src_head >&2
		ret=1
	fi
	;;
esac

if [ "$filter_tag_name" ]; then
	git for-each-ref --format='%(objectname) %(objecttype) %(refname)' refs/tags |
	while read sha1 type ref; do
		ref="${ref#refs/tags/}"
		# XXX: Rewrite tagged trees as well?
		if [ "$type" != "commit" -a "$type" != "tag" ]; then
			continue;
		fi

		if [ "$type" = "tag" ]; then
			# Dereference to a commit
			sha1t="$sha1"
			sha1="$(git rev-parse "$sha1"^{commit} 2>/dev/null)" || continue
		fi

		[ -f "../map/$sha1" ] || continue
		new_sha1="$(cat "../map/$sha1")"
		export GIT_COMMIT="$sha1"
		new_ref="$(echo "$ref" | eval "$filter_tag_name")"

		echo "$ref -> $new_ref ($sha1 -> $new_sha1)"

		if [ "$type" = "tag" ]; then
			# Warn that we are not rewriting the tag object itself.
			warn "unreferencing tag object $sha1t"
		fi

		git update-ref "refs/tags/$new_ref" "$new_sha1"
	done
fi

cd ../..
rm -rf "$tempdir"
printf "\nRewritten history saved to the $dstbranch branch\n"

exit $ret
