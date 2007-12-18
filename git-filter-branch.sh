#!/bin/sh
#
# Rewrite revision history
# Copyright (c) Petr Baudis, 2006
# Minimal changes to "port" it to core-git (c) Johannes Schindelin, 2007
#
# Lets you rewrite the revision history of the current branch, creating
# a new branch. You can specify a number of filters to modify the commits,
# files and trees.

# The following functions will also be available in the commit filter:

functions=$(cat << \EOF
warn () {
        echo "$*" >&2
}

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

# if you run 'skip_commit "$@"' in a commit filter, it will print
# the (mapped) parents, effectively skipping the commit.

skip_commit()
{
	shift;
	while [ -n "$1" ];
	do
		shift;
		map "$1";
		shift;
	done;
}

# override die(): this version puts in an extra line break, so that
# the progress is still visible

die()
{
	echo >&2
	echo "$*" >&2
	exit 1
}
EOF
)

eval "$functions"

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
			s/.*/GIT_'$uid'_NAME='\''&'\''; export GIT_'$uid'_NAME/p

			g
			s/^'$lid' [^<]* <\([^>]*\)> .*$/\1/
			s/'\''/'\''\'\'\''/g
			s/.*/GIT_'$uid'_EMAIL='\''&'\''; export GIT_'$uid'_EMAIL/p

			g
			s/^'$lid' [^<]* <[^>]*> \(.*\)$/\1/
			s/'\''/'\''\'\'\''/g
			s/.*/GIT_'$uid'_DATE='\''&'\''; export GIT_'$uid'_DATE/p

			q
		}
	'

	LANG=C LC_ALL=C sed -ne "$pick_id_script"
	# Ensure non-empty id name.
	echo "case \"\$GIT_${uid}_NAME\" in \"\") GIT_${uid}_NAME=\"\${GIT_${uid}_EMAIL%%@*}\" && export GIT_${uid}_NAME;; esac"
}

USAGE="[--env-filter <command>] [--tree-filter <command>] \
[--index-filter <command>] [--parent-filter <command>] \
[--msg-filter <command>] [--commit-filter <command>] \
[--tag-name-filter <command>] [--subdirectory-filter <directory>] \
[--original <namespace>] [-d <directory>] [-f | --force] \
[<rev-list options>...]"

OPTIONS_SPEC=
. git-sh-setup

git diff-files --quiet &&
	git diff-index --cached --quiet HEAD -- ||
	die "Cannot rewrite branch(es) with a dirty working directory."

tempdir=.git-rewrite
filter_env=
filter_tree=
filter_index=
filter_parent=
filter_msg=cat
filter_commit='git commit-tree "$@"'
filter_tag_name=
filter_subdir=
orig_namespace=refs/original/
force=
while :
do
	test $# = 0 && usage
	case "$1" in
	--)
		shift
		break
		;;
	--force|-f)
		shift
		force=t
		continue
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
		filter_commit="$functions; $OPTARG"
		;;
	--tag-name-filter)
		filter_tag_name="$OPTARG"
		;;
	--subdirectory-filter)
		filter_subdir="$OPTARG"
		;;
	--original)
		orig_namespace=$(expr "$OPTARG/" : '\(.*[^/]\)/*$')/
		;;
	*)
		usage
		;;
	esac
done

case "$force" in
t)
	rm -rf "$tempdir"
;;
'')
	test -d "$tempdir" &&
		die "$tempdir already exists, please remove it"
esac
mkdir -p "$tempdir/t" &&
tempdir="$(cd "$tempdir"; pwd)" &&
cd "$tempdir/t" &&
workdir="$(pwd)" ||
die ""

# Make sure refs/original is empty
git for-each-ref > "$tempdir"/backup-refs
while read sha1 type name
do
	case "$force,$name" in
	,$orig_namespace*)
		die "Namespace $orig_namespace not empty"
	;;
	t,$orig_namespace*)
		git update-ref -d "$name" $sha1
	;;
	esac
done < "$tempdir"/backup-refs

ORIG_GIT_DIR="$GIT_DIR"
ORIG_GIT_WORK_TREE="$GIT_WORK_TREE"
ORIG_GIT_INDEX_FILE="$GIT_INDEX_FILE"
GIT_WORK_TREE=.
export GIT_DIR GIT_WORK_TREE

# These refs should be updated if their heads were rewritten

git rev-parse --revs-only --symbolic "$@" |
while read ref
do
	# normalize ref
	case "$ref" in
	HEAD)
		ref="$(git symbolic-ref "$ref")"
	;;
	refs/*)
	;;
	*)
		ref="$(git for-each-ref --format='%(refname)' |
			grep /"$ref")"
	esac

	git check-ref-format "$ref" && echo "$ref"
done > "$tempdir"/heads

test -s "$tempdir"/heads ||
	die "Which ref do you want to rewrite?"

GIT_INDEX_FILE="$(pwd)/../index"
export GIT_INDEX_FILE
git read-tree || die "Could not seed the index"

ret=0

# map old->new commit ids for rewriting parents
mkdir ../map || die "Could not create map/ directory"

case "$filter_subdir" in
"")
	git rev-list --reverse --topo-order --default HEAD \
		--parents "$@"
	;;
*)
	git rev-list --reverse --topo-order --default HEAD \
		--parents --full-history "$@" -- "$filter_subdir"
esac > ../revs || die "Could not get the commits"
commits=$(wc -l <../revs | tr -d " ")

test $commits -eq 0 && die "Found nothing to rewrite"

# Rewrite the commits

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
	esac || die "Could not initialize the index"

	GIT_COMMIT=$commit
	export GIT_COMMIT
	git cat-file commit "$commit" >../commit ||
		die "Cannot read commit $commit"

	eval "$(set_ident AUTHOR <../commit)" ||
		die "setting author failed for commit $commit"
	eval "$(set_ident COMMITTER <../commit)" ||
		die "setting committer failed for commit $commit"
	eval "$filter_env" < /dev/null ||
		die "env filter failed: $filter_env"

	if [ "$filter_tree" ]; then
		git checkout-index -f -u -a ||
			die "Could not checkout the index"
		# files that $commit removed are now still in the working tree;
		# remove them, else they would be added again
		git ls-files -z --others | xargs -0 rm -f
		eval "$filter_tree" < /dev/null ||
			die "tree filter failed: $filter_tree"

		git diff-index -r $commit | cut -f 2- | tr '\n' '\000' | \
			xargs -0 git update-index --add --replace --remove
		git ls-files -z --others | \
			xargs -0 git update-index --add --replace --remove
	fi

	eval "$filter_index" < /dev/null ||
		die "index filter failed: $filter_index"

	parentstr=
	for parent in $parents; do
		for reparent in $(map "$parent"); do
			parentstr="$parentstr -p $reparent"
		done
	done
	if [ "$filter_parent" ]; then
		parentstr="$(echo "$parentstr" | eval "$filter_parent")" ||
				die "parent filter failed: $filter_parent"
	fi

	sed -e '1,/^$/d' <../commit | \
		eval "$filter_msg" > ../message ||
			die "msg filter failed: $filter_msg"
	sh -c "$filter_commit" "git commit-tree" \
		$(git write-tree) $parentstr < ../message > ../map/$commit
done <../revs

# In case of a subdirectory filter, it is possible that a specified head
# is not in the set of rewritten commits, because it was pruned by the
# revision walker.  Fix it by mapping these heads to the next rewritten
# ancestor(s), i.e. the boundaries in the set of rewritten commits.

# NEEDSWORK: we should sort the unmapped refs topologically first
while read ref
do
	sha1=$(git rev-parse "$ref"^0)
	test -f "$workdir"/../map/$sha1 && continue
	# Assign the boundarie(s) in the set of rewritten commits
	# as the replacement commit(s).
	# (This would look a bit nicer if --not --stdin worked.)
	for p in $( (cd "$workdir"/../map; ls | sed "s/^/^/") |
		git rev-list $ref --boundary --stdin |
		sed -n "s/^-//p")
	do
		map $p >> "$workdir"/../map/$sha1
	done
done < "$tempdir"/heads

# Finally update the refs

_x40='[0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f]'
_x40="$_x40$_x40$_x40$_x40$_x40$_x40$_x40$_x40"
echo
while read ref
do
	# avoid rewriting a ref twice
	test -f "$orig_namespace$ref" && continue

	sha1=$(git rev-parse "$ref"^0)
	rewritten=$(map $sha1)

	test $sha1 = "$rewritten" &&
		warn "WARNING: Ref '$ref' is unchanged" &&
		continue

	case "$rewritten" in
	'')
		echo "Ref '$ref' was deleted"
		git update-ref -m "filter-branch: delete" -d "$ref" $sha1 ||
			die "Could not delete $ref"
	;;
	$_x40)
		echo "Ref '$ref' was rewritten"
		git update-ref -m "filter-branch: rewrite" \
				"$ref" $rewritten $sha1 ||
			die "Could not rewrite $ref"
	;;
	*)
		# NEEDSWORK: possibly add -Werror, making this an error
		warn "WARNING: '$ref' was rewritten into multiple commits:"
		warn "$rewritten"
		warn "WARNING: Ref '$ref' points to the first one now."
		rewritten=$(echo "$rewritten" | head -n 1)
		git update-ref -m "filter-branch: rewrite to first" \
				"$ref" $rewritten $sha1 ||
			die "Could not rewrite $ref"
	;;
	esac
	git update-ref -m "filter-branch: backup" "$orig_namespace$ref" $sha1
done < "$tempdir"/heads

# TODO: This should possibly go, with the semantics that all positive given
#       refs are updated, and their original heads stored in refs/original/
# Filter tags

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
		GIT_COMMIT="$sha1"
		export GIT_COMMIT
		new_ref="$(echo "$ref" | eval "$filter_tag_name")" ||
			die "tag name filter failed: $filter_tag_name"

		echo "$ref -> $new_ref ($sha1 -> $new_sha1)"

		if [ "$type" = "tag" ]; then
			# Warn that we are not rewriting the tag object itself.
			warn "unreferencing tag object $sha1t"
		fi

		git update-ref "refs/tags/$new_ref" "$new_sha1" ||
			die "Could not write tag $new_ref"
	done
fi

cd ../..
rm -rf "$tempdir"

unset GIT_DIR GIT_WORK_TREE GIT_INDEX_FILE
test -z "$ORIG_GIT_DIR" || GIT_DIR="$ORIG_GIT_DIR" && export GIT_DIR
test -z "$ORIG_GIT_WORK_TREE" || GIT_WORK_TREE="$ORIG_GIT_WORK_TREE" &&
	export GIT_WORK_TREE
test -z "$ORIG_GIT_INDEX_FILE" || GIT_INDEX_FILE="$ORIG_GIT_INDEX_FILE" &&
	export GIT_INDEX_FILE
git read-tree -u -m HEAD

exit $ret
