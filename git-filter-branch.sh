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
EMPTY_TREE=$(git hash-object -t tree /dev/null)

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

# if you run 'git_commit_non_empty_tree "$@"' in a commit filter,
# it will skip commits that leave the tree untouched, commit the other.
git_commit_non_empty_tree()
{
	if test $# = 3 && test "$1" = $(git rev-parse "$3^{tree}"); then
		map "$3"
	elif test $# = 1 && test "$1" = $EMPTY_TREE; then
		:
	else
		git commit-tree "$@"
	fi
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

finish_ident() {
	# Ensure non-empty id name.
	echo "case \"\$GIT_$1_NAME\" in \"\") GIT_$1_NAME=\"\${GIT_$1_EMAIL%%@*}\" && export GIT_$1_NAME;; esac"
	# And make sure everything is exported.
	echo "export GIT_$1_NAME"
	echo "export GIT_$1_EMAIL"
	echo "export GIT_$1_DATE"
}

set_ident () {
	parse_ident_from_commit author AUTHOR committer COMMITTER
	finish_ident AUTHOR
	finish_ident COMMITTER
}

if test -z "$FILTER_BRANCH_SQUELCH_WARNING$GIT_TEST_DISALLOW_ABBREVIATED_OPTIONS"
then
	cat <<EOF
WARNING: git-filter-branch has a glut of gotchas generating mangled history
	 rewrites.  Hit Ctrl-C before proceeding to abort, then use an
	 alternative filtering tool such as 'git filter-repo'
	 (https://github.com/newren/git-filter-repo/) instead.  See the
	 filter-branch manual page for more details; to squelch this warning,
	 set FILTER_BRANCH_SQUELCH_WARNING=1.
EOF
	sleep 10
	printf "Proceeding with filter-branch...\n\n"
fi

USAGE="[--setup <command>] [--subdirectory-filter <directory>] [--env-filter <command>]
	[--tree-filter <command>] [--index-filter <command>]
	[--parent-filter <command>] [--msg-filter <command>]
	[--commit-filter <command>] [--tag-name-filter <command>]
	[--original <namespace>]
	[-d <directory>] [-f | --force] [--state-branch <branch>]
	[--] [<rev-list options>...]"

OPTIONS_SPEC=
. git-sh-setup

if [ "$(is_bare_repository)" = false ]; then
	require_clean_work_tree 'rewrite branches'
fi

tempdir=.git-rewrite
filter_setup=
filter_env=
filter_tree=
filter_index=
filter_parent=
filter_msg=cat
filter_commit=
filter_tag_name=
filter_subdir=
state_branch=
orig_namespace=refs/original/
force=
prune_empty=
remap_to_ancestor=
while :
do
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
	--remap-to-ancestor)
		# deprecated ($remap_to_ancestor is set now automatically)
		shift
		remap_to_ancestor=t
		continue
		;;
	--prune-empty)
		shift
		prune_empty=t
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
	--setup)
		filter_setup="$OPTARG"
		;;
	--subdirectory-filter)
		filter_subdir="$OPTARG"
		remap_to_ancestor=t
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
	--original)
		orig_namespace=$(expr "$OPTARG/" : '\(.*[^/]\)/*$')/
		;;
	--state-branch)
		state_branch="$OPTARG"
		;;
	*)
		usage
		;;
	esac
done

case "$prune_empty,$filter_commit" in
,)
	filter_commit='git commit-tree "$@"';;
t,)
	filter_commit="$functions;"' git_commit_non_empty_tree "$@"';;
,*)
	;;
*)
	die "Cannot set --prune-empty and --commit-filter at the same time"
esac

case "$force" in
t)
	rm -rf "$tempdir"
;;
'')
	test -d "$tempdir" &&
		die "$tempdir already exists, please remove it"
esac
orig_dir=$(pwd)
mkdir -p "$tempdir/t" &&
tempdir="$(cd "$tempdir"; pwd)" &&
cd "$tempdir/t" &&
workdir="$(pwd)" ||
die ""

# Remove tempdir on exit
trap 'cd "$orig_dir"; rm -rf "$tempdir"' 0

ORIG_GIT_DIR="$GIT_DIR"
ORIG_GIT_WORK_TREE="$GIT_WORK_TREE"
ORIG_GIT_INDEX_FILE="$GIT_INDEX_FILE"
ORIG_GIT_AUTHOR_NAME="$GIT_AUTHOR_NAME"
ORIG_GIT_AUTHOR_EMAIL="$GIT_AUTHOR_EMAIL"
ORIG_GIT_AUTHOR_DATE="$GIT_AUTHOR_DATE"
ORIG_GIT_COMMITTER_NAME="$GIT_COMMITTER_NAME"
ORIG_GIT_COMMITTER_EMAIL="$GIT_COMMITTER_EMAIL"
ORIG_GIT_COMMITTER_DATE="$GIT_COMMITTER_DATE"

GIT_WORK_TREE=.
export GIT_DIR GIT_WORK_TREE

# Make sure refs/original is empty
git for-each-ref > "$tempdir"/backup-refs || exit
while read sha1 type name
do
	case "$force,$name" in
	,$orig_namespace*)
		die "Cannot create a new backup.
A previous backup already exists in $orig_namespace
Force overwriting the backup with -f"
	;;
	t,$orig_namespace*)
		git update-ref -d "$name" $sha1
	;;
	esac
done < "$tempdir"/backup-refs

# The refs should be updated if their heads were rewritten
git rev-parse --no-flags --revs-only --symbolic-full-name \
	--default HEAD "$@" > "$tempdir"/raw-refs || exit
while read ref
do
	case "$ref" in ^?*) continue ;; esac

	if git rev-parse --verify "$ref"^0 >/dev/null 2>&1
	then
		echo "$ref"
	else
		warn "WARNING: not rewriting '$ref' (not a committish)"
	fi
done >"$tempdir"/heads <"$tempdir"/raw-refs

test -s "$tempdir"/heads ||
	die "You must specify a ref to rewrite."

GIT_INDEX_FILE="$(pwd)/../index"
export GIT_INDEX_FILE

# map old->new commit ids for rewriting parents
mkdir ../map || die "Could not create map/ directory"

if test -n "$state_branch"
then
	state_commit=$(git rev-parse --no-flags --revs-only "$state_branch")
	if test -n "$state_commit"
	then
		echo "Populating map from $state_branch ($state_commit)" 1>&2
		perl -e'open(MAP, "-|", "git show $ARGV[0]:filter.map") or die;
			while (<MAP>) {
				m/(.*):(.*)/ or die;
				open F, ">../map/$1" or die;
				print F "$2" or die;
				close(F) or die;
			}
			close(MAP) or die;' "$state_commit" \
				|| die "Unable to load state from $state_branch:filter.map"
	else
		echo "Branch $state_branch does not exist. Will create" 1>&2
	fi
fi

# we need "--" only if there are no path arguments in $@
nonrevs=$(git rev-parse --no-revs "$@") || exit
if test -z "$nonrevs"
then
	dashdash=--
else
	dashdash=
	remap_to_ancestor=t
fi

git rev-parse --revs-only "$@" >../parse

case "$filter_subdir" in
"")
	eval set -- "$(git rev-parse --sq --no-revs "$@")"
	;;
*)
	eval set -- "$(git rev-parse --sq --no-revs "$@" $dashdash \
		"$filter_subdir")"
	;;
esac

git rev-list --reverse --topo-order --default HEAD \
	--parents --simplify-merges --stdin "$@" <../parse >../revs ||
	die "Could not get the commits"
commits=$(wc -l <../revs | tr -d " ")

test $commits -eq 0 && die_with_status 2 "Found nothing to rewrite"

# Rewrite the commits
report_progress ()
{
	if test -n "$progress" &&
		test $git_filter_branch__commit_count -gt $next_sample_at
	then
		count=$git_filter_branch__commit_count

		now=$(date +%s)
		elapsed=$(($now - $start_timestamp))
		remaining=$(( ($commits - $count) * $elapsed / $count ))
		if test $elapsed -gt 0
		then
			next_sample_at=$(( ($elapsed + 1) * $count / $elapsed ))
		else
			next_sample_at=$(($next_sample_at + 1))
		fi
		progress=" ($elapsed seconds passed, remaining $remaining predicted)"
	fi
	printf "\rRewrite $commit ($count/$commits)$progress    "
}

git_filter_branch__commit_count=0

progress= start_timestamp=
if date '+%s' 2>/dev/null | grep -q '^[0-9][0-9]*$'
then
	next_sample_at=0
	progress="dummy to ensure this is not empty"
	start_timestamp=$(date '+%s')
fi

if test -n "$filter_index" ||
   test -n "$filter_tree" ||
   test -n "$filter_subdir"
then
	need_index=t
else
	need_index=
fi

eval "$filter_setup" < /dev/null ||
	die "filter setup failed: $filter_setup"

while read commit parents; do
	git_filter_branch__commit_count=$(($git_filter_branch__commit_count+1))

	report_progress
	test -f "$workdir"/../map/$commit && continue

	case "$filter_subdir" in
	"")
		if test -n "$need_index"
		then
			GIT_ALLOW_NULL_SHA1=1 git read-tree -i -m $commit
		fi
		;;
	*)
		# The commit may not have the subdirectory at all
		err=$(GIT_ALLOW_NULL_SHA1=1 \
		      git read-tree -i -m $commit:"$filter_subdir" 2>&1) || {
			if ! git rev-parse -q --verify $commit:"$filter_subdir"
			then
				rm -f "$GIT_INDEX_FILE"
			else
				echo >&2 "$err"
				false
			fi
		}
	esac || die "Could not initialize the index"

	GIT_COMMIT=$commit
	export GIT_COMMIT
	git cat-file commit "$commit" >../commit ||
		die "Cannot read commit $commit"

	eval "$(set_ident <../commit)" ||
		die "setting author/committer failed for commit $commit"
	eval "$filter_env" < /dev/null ||
		die "env filter failed: $filter_env"

	if [ "$filter_tree" ]; then
		git checkout-index -f -u -a ||
			die "Could not checkout the index"
		# files that $commit removed are now still in the working tree;
		# remove them, else they would be added again
		git clean -d -q -f -x
		eval "$filter_tree" < /dev/null ||
			die "tree filter failed: $filter_tree"

		(
			git diff-index -r --name-only --ignore-submodules $commit -- &&
			git ls-files --others
		) > "$tempdir"/tree-state || exit
		git update-index --add --replace --remove --stdin \
			< "$tempdir"/tree-state || exit
	fi

	eval "$filter_index" < /dev/null ||
		die "index filter failed: $filter_index"

	parentstr=
	for parent in $parents; do
		for reparent in $(map "$parent"); do
			case "$parentstr " in
			*" -p $reparent "*)
				;;
			*)
				parentstr="$parentstr -p $reparent"
				;;
			esac
		done
	done
	if [ "$filter_parent" ]; then
		parentstr="$(echo "$parentstr" | eval "$filter_parent")" ||
				die "parent filter failed: $filter_parent"
	fi

	{
		while IFS='' read -r header_line && test -n "$header_line"
		do
			# skip header lines...
			:;
		done
		# and output the actual commit message
		cat
	} <../commit |
		eval "$filter_msg" > ../message ||
			die "msg filter failed: $filter_msg"

	if test -n "$need_index"
	then
		tree=$(git write-tree)
	else
		tree=$(git rev-parse "$commit^{tree}")
	fi
	workdir=$workdir @SHELL_PATH@ -c "$filter_commit" "git commit-tree" \
		"$tree" $parentstr < ../message > ../map/$commit ||
			die "could not write rewritten commit"
done <../revs

# If we are filtering for paths, as in the case of a subdirectory
# filter, it is possible that a specified head is not in the set of
# rewritten commits, because it was pruned by the revision walker.
# Ancestor remapping fixes this by mapping these heads to the unique
# nearest ancestor that survived the pruning.

if test "$remap_to_ancestor" = t
then
	while read ref
	do
		sha1=$(git rev-parse "$ref"^0)
		test -f "$workdir"/../map/$sha1 && continue
		ancestor=$(git rev-list --simplify-merges -1 "$ref" "$@")
		test "$ancestor" && echo $(map $ancestor) >"$workdir"/../map/$sha1
	done < "$tempdir"/heads
fi

# Finally update the refs

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
	*)
		echo "Ref '$ref' was rewritten"
		if ! git update-ref -m "filter-branch: rewrite" \
					"$ref" $rewritten $sha1 2>/dev/null; then
			if test $(git cat-file -t "$ref") = tag; then
				if test -z "$filter_tag_name"; then
					warn "WARNING: You said to rewrite tagged commits, but not the corresponding tag."
					warn "WARNING: Perhaps use '--tag-name-filter cat' to rewrite the tag."
				fi
			else
				die "Could not rewrite $ref"
			fi
		fi
	;;
	esac
	git update-ref -m "filter-branch: backup" "$orig_namespace$ref" $sha1 ||
		 exit
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
			sha1="$(git rev-parse -q "$sha1"^{commit})" || continue
		fi

		[ -f "../map/$sha1" ] || continue
		new_sha1="$(cat "../map/$sha1")"
		GIT_COMMIT="$sha1"
		export GIT_COMMIT
		new_ref="$(echo "$ref" | eval "$filter_tag_name")" ||
			die "tag name filter failed: $filter_tag_name"

		echo "$ref -> $new_ref ($sha1 -> $new_sha1)"

		if [ "$type" = "tag" ]; then
			new_sha1=$( ( printf 'object %s\ntype commit\ntag %s\n' \
						"$new_sha1" "$new_ref"
				git cat-file tag "$ref" |
				sed -n \
				    -e '1,/^$/{
					  /^object /d
					  /^type /d
					  /^tag /d
					}' \
				    -e '/^-----BEGIN PGP SIGNATURE-----/q' \
				    -e 'p' ) |
				git hash-object -t tag -w --stdin) ||
				die "Could not create new tag object for $ref"
			if git cat-file tag "$ref" | \
			   sane_grep '^-----BEGIN PGP SIGNATURE-----' >/dev/null 2>&1
			then
				warn "gpg signature stripped from tag object $sha1t"
			fi
		fi

		git update-ref "refs/tags/$new_ref" "$new_sha1" ||
			die "Could not write tag $new_ref"
	done
fi

unset GIT_DIR GIT_WORK_TREE GIT_INDEX_FILE
unset GIT_AUTHOR_NAME GIT_AUTHOR_EMAIL GIT_AUTHOR_DATE
unset GIT_COMMITTER_NAME GIT_COMMITTER_EMAIL GIT_COMMITTER_DATE
test -z "$ORIG_GIT_DIR" || {
	GIT_DIR="$ORIG_GIT_DIR" && export GIT_DIR
}
test -z "$ORIG_GIT_WORK_TREE" || {
	GIT_WORK_TREE="$ORIG_GIT_WORK_TREE" &&
	export GIT_WORK_TREE
}
test -z "$ORIG_GIT_INDEX_FILE" || {
	GIT_INDEX_FILE="$ORIG_GIT_INDEX_FILE" &&
	export GIT_INDEX_FILE
}
test -z "$ORIG_GIT_AUTHOR_NAME" || {
	GIT_AUTHOR_NAME="$ORIG_GIT_AUTHOR_NAME" &&
	export GIT_AUTHOR_NAME
}
test -z "$ORIG_GIT_AUTHOR_EMAIL" || {
	GIT_AUTHOR_EMAIL="$ORIG_GIT_AUTHOR_EMAIL" &&
	export GIT_AUTHOR_EMAIL
}
test -z "$ORIG_GIT_AUTHOR_DATE" || {
	GIT_AUTHOR_DATE="$ORIG_GIT_AUTHOR_DATE" &&
	export GIT_AUTHOR_DATE
}
test -z "$ORIG_GIT_COMMITTER_NAME" || {
	GIT_COMMITTER_NAME="$ORIG_GIT_COMMITTER_NAME" &&
	export GIT_COMMITTER_NAME
}
test -z "$ORIG_GIT_COMMITTER_EMAIL" || {
	GIT_COMMITTER_EMAIL="$ORIG_GIT_COMMITTER_EMAIL" &&
	export GIT_COMMITTER_EMAIL
}
test -z "$ORIG_GIT_COMMITTER_DATE" || {
	GIT_COMMITTER_DATE="$ORIG_GIT_COMMITTER_DATE" &&
	export GIT_COMMITTER_DATE
}

if test -n "$state_branch"
then
	echo "Saving rewrite state to $state_branch" 1>&2
	state_blob=$(
		perl -e'opendir D, "../map" or die;
			open H, "|-", "git hash-object -w --stdin" or die;
			foreach (sort readdir(D)) {
				next if m/^\.\.?$/;
				open F, "<../map/$_" or die;
				chomp($f = <F>);
				print H "$_:$f\n" or die;
			}
			close(H) or die;' || die "Unable to save state")
	state_tree=$(printf '100644 blob %s\tfilter.map\n' "$state_blob" | git mktree)
	if test -n "$state_commit"
	then
		state_commit=$(echo "Sync" | git commit-tree "$state_tree" -p "$state_commit")
	else
		state_commit=$(echo "Sync" | git commit-tree "$state_tree" )
	fi
	git update-ref "$state_branch" "$state_commit"
fi

cd "$orig_dir"
rm -rf "$tempdir"

trap - 0

if [ "$(is_bare_repository)" = false ]; then
	git read-tree -u -m HEAD || exit
fi

exit 0
