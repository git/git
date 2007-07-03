#!/bin/sh
#
# Rewrite revision history
# Copyright (c) Petr Baudis, 2006
# Minimal changes to "port" it to core-git (c) Johannes Schindelin, 2007
#
# Lets you rewrite GIT revision history by creating a new branch from
# your current branch by applying custom filters on each revision.
# Those filters can modify each tree (e.g. removing a file or running
# a perl rewrite on all files) or information about each commit.
# Otherwise, all information (including original commit times or merge
# information) will be preserved.
#
# The command takes the new branch name as a mandatory argument and
# the filters as optional arguments. If you specify no filters, the
# commits will be recommitted without any changes, which would normally
# have no effect and result with the new branch pointing to the same
# branch as your current branch. (Nevertheless, this may be useful in
# the future for compensating for some Git bugs or such, therefore
# such a usage is permitted.)
#
# WARNING! The rewritten history will have different ids for all the
# objects and will not converge with the original branch. You will not
# be able to easily push and distribute the rewritten branch. Please do
# not use this command if you do not know the full implications, and
# avoid using it anyway - do not do what a simple single commit on top
# of the current version would fix.
#
# Always verify that the rewritten version is correct before disposing
# the original branch.
#
# Note that since this operation is extensively I/O expensive, it might
# be a good idea to do it off-disk, e.g. on tmpfs. Reportedly the speedup
# is very noticeable.
#
# OPTIONS
# -------
# -d TEMPDIR:: The path to the temporary tree used for rewriting
#	When applying a tree filter, the command needs to temporary
#	checkout the tree to some directory, which may consume
#	considerable space in case of large projects. By default it
#	does this in the '.git-rewrite/' directory but you can override
#	that choice by this parameter.
#
# Filters
# ~~~~~~~
# The filters are applied in the order as listed below. The COMMAND
# argument is always evaluated in shell using the 'eval' command.
# The $GIT_COMMIT environment variable is permanently set to contain
# the id of the commit being rewritten. The author/committer environment
# variables are set before the first filter is run.
#
# A 'map' function is available that takes an "original sha1 id" argument
# and outputs a "rewritten sha1 id" if the commit has been already
# rewritten, fails otherwise; the 'map' function can return several
# ids on separate lines if your commit filter emitted multiple commits
# (see below).
#
# --env-filter COMMAND:: The filter for modifying environment
#	This is the filter for modifying the environment in which
#	the commit will be performed. Specifically, you might want
#	to rewrite the author/committer name/email/time environment
#	variables (see `git-commit` for details). Do not forget to
#	re-export the variables.
#
# --tree-filter COMMAND:: The filter for rewriting tree (and its contents)
#	This is the filter for rewriting the tree and its contents.
#	The COMMAND argument is evaluated in shell with the working
#	directory set to the root of the checked out tree. The new tree
#	is then used as-is (new files are auto-added, disappeared files
#	are auto-removed - .gitignore files nor any other ignore rules
#	HAVE NO EFFECT!).
#
# --index-filter COMMAND:: The filter for rewriting index
#	This is the filter for rewriting the Git's directory index.
#	It is similar to the tree filter but does not check out the
#	tree, which makes it much faster. However, you must use the
#	lowlevel Git index manipulation commands to do your work.
#
# --parent-filter COMMAND:: The filter for rewriting parents
#	This is the filter for rewriting the commit's parent list.
#	It will receive the parent string on stdin and shall output
#	the new parent string on stdout. The parent string is in
#	format accepted by `git commit-tree`: empty for initial
#	commit, "-p parent" for a normal commit and "-p parent1
#	-p parent2 -p parent3 ..." for a merge commit.
#
# --msg-filter COMMAND:: The filter for rewriting commit message
#	This is the filter for rewriting the commit messages.
#	The COMMAND argument is evaluated in shell with the original
#	commit message on standard input; its standard output is
#	is used as the new commit message.
#
# --commit-filter COMMAND:: The filter for performing the commit
#	If this filter is passed, it will be called instead of the
#	`git commit-tree` command, with those arguments:
#
#		TREE_ID [-p PARENT_COMMIT_ID]...
#
#	and the log message on stdin. The commit id is expected on
#	stdout. As a special extension, the commit filter may emit
#	multiple commit ids; in that case, all of them will be used
#	as parents instead of the original commit in further commits.
#
# --tag-name-filter COMMAND:: The filter for rewriting tag names.
#	If this filter is passed, it will be called for every tag ref
#	that points to a rewritten object (or to a tag object which
#	points to a rewritten object). The original tag name is passed
#	via standard input, and the new tag name is expected on standard
#	output.
#
#	The original tags are not deleted, but can be overwritten;
#	use "--tag-name-filter=cat" to simply update the tags. In this
#	case, be very careful and make sure you have the old tags
#	backed up in case the conversion has run afoul.
#
#	Note that there is currently no support for proper rewriting of
#	tag objects; in layman terms, if the tag has a message or signature
#	attached, the rewritten tag won't have it. Sorry. (It is by
#	definition impossible to preserve signatures at any rate, though.)
#
# --subdirectory-filter DIRECTORY:: Only regard the history, as seen by
#	the given subdirectory. The result will contain that directory as
#	its project root.
#
# EXAMPLE USAGE
# -------------
# Suppose you want to remove a file (containing confidential information
# or copyright violation) from all commits:
#
#	git-filter-branch --tree-filter 'rm filename' newbranch
#
# A significantly faster version:
#
#	git-filter-branch --index-filter 'git update-index --remove filename' newbranch
#
# Now, you will get the rewritten history saved in the branch 'newbranch'
# (your current branch is left untouched).
#
# To "etch-graft" a commit to the revision history (set a commit to be
# the parent of the current initial commit and propagate that):
#
#	git-filter-branch --parent-filter sed\ 's/^$/-p graftcommitid/' newbranch
#
# (if the parent string is empty - therefore we are dealing with the
# initial commit - add graftcommit as a parent). Note that this assumes
# history with a single root (that is, no git-merge without common ancestors
# happened). If this is not the case, use:
#
#	git-filter-branch --parent-filter 'cat; [ "$GIT_COMMIT" = "COMMIT" ] && echo "-p GRAFTCOMMIT"' newbranch
#
# To remove commits authored by "Darl McBribe" from the history:
#
#	git-filter-branch --commit-filter 'if [ "$GIT_AUTHOR_NAME" = "Darl McBribe" ]; then shift; while [ -n "$1" ]; do shift; echo "$1"; shift; done; else git commit-tree "$@"; fi' newbranch
#
# (the shift magic first throws away the tree id and then the -p
# parameters). Note that this handles merges properly! In case Darl
# committed a merge between P1 and P2, it will be propagated properly
# and all children of the merge will become merge commits with P1,P2
# as their parents instead of the merge commit.
#
# To restrict rewriting to only part of the history, specify a revision
# range in addition to the new branch name. The new branch name will
# point to the top-most revision that a 'git rev-list' of this range
# will print.
#
# Consider this history:
#
#	     D--E--F--G--H
#	    /     /
#	A--B-----C
#
# To rewrite commits D,E,F,G,H, use:
#
#	git-filter-branch ... new-H C..H
#
# To rewrite commits E,F,G,H, use one of these:
#
#	git-filter-branch ... new-H C..H --not D
#	git-filter-branch ... new-H D..H --not C
#
# To move the whole tree into a subdirectory, or remove it from there:
#
# git-filter-branch --index-filter \
#	'git ls-files -s | sed "s-\t-&newsubdir/-" |
#		GIT_INDEX_FILE=$GIT_INDEX_FILE.new \
#			git update-index --index-info &&
#	 mv $GIT_INDEX_FILE.new $GIT_INDEX_FILE' directorymoved

# Testsuite: TODO

set -e

USAGE="git-filter-branch [-d TEMPDIR] [FILTERS] DESTBRANCH [REV-RANGE]"
. git-sh-setup

map()
{
	# if it was not rewritten, take the original
	test -r "$workdir/../map/$1" || echo "$1"
	cat "$workdir/../map/$1"
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
	printf "$commit ($i/$commits) "

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
		sh -c "$filter_commit" "git commit-tree" $(git write-tree) $parentstr | \
		tee ../map/$commit
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
echo "Rewritten history saved to the $dstbranch branch"

exit $ret
