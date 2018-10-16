#!/bin/sh
#
# git-subtree.sh: split/join git repositories in subdirectories of this one
#
# Copyright (C) 2009 Avery Pennarun <apenwarr@gmail.com>
#
if test $# -eq 0
then
	set -- -h
fi
OPTS_SPEC="\
git subtree add   --prefix=<prefix> <commit>
git subtree add   --prefix=<prefix> <repository> <ref>
git subtree merge --prefix=<prefix> <commit>
git subtree pull  --prefix=<prefix> <repository> <ref>
git subtree push  --prefix=<prefix> <repository> <ref>
git subtree split --prefix=<prefix> <commit...>
--
h,help        show the help
q             quiet
d             show debug messages
P,prefix=     the name of the subdir to split out
m,message=    use the given message as the commit message for the merge commit
 options for 'split'
annotate=     add a prefix to commit message of new commits
b,branch=     create a new branch from the split subtree
ignore-joins  ignore prior --rejoin commits
onto=         try connecting new tree to an existing one
rejoin        merge the new branch back into HEAD
 options for 'add', 'merge', and 'pull'
squash        merge subtree changes as a single commit
"
eval "$(echo "$OPTS_SPEC" | git rev-parse --parseopt -- "$@" || echo exit $?)"

PATH=$PATH:$(git --exec-path)
. git-sh-setup

require_work_tree

quiet=
branch=
debug=
command=
onto=
rejoin=
ignore_joins=
annotate=
squash=
message=
prefix=

debug () {
	if test -n "$debug"
	then
		printf "%s\n" "$*" >&2
	fi
}

say () {
	if test -z "$quiet"
	then
		printf "%s\n" "$*" >&2
	fi
}

progress () {
	if test -z "$quiet"
	then
		printf "%s\r" "$*" >&2
	fi
}

assert () {
	if ! "$@"
	then
		die "assertion failed: " "$@"
	fi
}


while test $# -gt 0
do
	opt="$1"
	shift

	case "$opt" in
	-q)
		quiet=1
		;;
	-d)
		debug=1
		;;
	--annotate)
		annotate="$1"
		shift
		;;
	--no-annotate)
		annotate=
		;;
	-b)
		branch="$1"
		shift
		;;
	-P)
		prefix="${1%/}"
		shift
		;;
	-m)
		message="$1"
		shift
		;;
	--no-prefix)
		prefix=
		;;
	--onto)
		onto="$1"
		shift
		;;
	--no-onto)
		onto=
		;;
	--rejoin)
		rejoin=1
		;;
	--no-rejoin)
		rejoin=
		;;
	--ignore-joins)
		ignore_joins=1
		;;
	--no-ignore-joins)
		ignore_joins=
		;;
	--squash)
		squash=1
		;;
	--no-squash)
		squash=
		;;
	--)
		break
		;;
	*)
		die "Unexpected option: $opt"
		;;
	esac
done

command="$1"
shift

case "$command" in
add|merge|pull)
	default=
	;;
split|push)
	default="--default HEAD"
	;;
*)
	die "Unknown command '$command'"
	;;
esac

if test -z "$prefix"
then
	die "You must provide the --prefix option."
fi

case "$command" in
add)
	test -e "$prefix" &&
		die "prefix '$prefix' already exists."
	;;
*)
	test -e "$prefix" ||
		die "'$prefix' does not exist; use 'git subtree add'"
	;;
esac

dir="$(dirname "$prefix/.")"

if test "$command" != "pull" &&
		test "$command" != "add" &&
		test "$command" != "push"
then
	revs=$(git rev-parse $default --revs-only "$@") || exit $?
	dirs=$(git rev-parse --no-revs --no-flags "$@") || exit $?
	if test -n "$dirs"
	then
		die "Error: Use --prefix instead of bare filenames."
	fi
fi

debug "command: {$command}"
debug "quiet: {$quiet}"
debug "revs: {$revs}"
debug "dir: {$dir}"
debug "opts: {$*}"
debug

cache_setup () {
	cachedir="$GIT_DIR/subtree-cache/$$"
	rm -rf "$cachedir" ||
		die "Can't delete old cachedir: $cachedir"
	mkdir -p "$cachedir" ||
		die "Can't create new cachedir: $cachedir"
	mkdir -p "$cachedir/notree" ||
		die "Can't create new cachedir: $cachedir/notree"
	debug "Using cachedir: $cachedir" >&2
}

cache_get () {
	for oldrev in "$@"
	do
		if test -r "$cachedir/$oldrev"
		then
			read newrev <"$cachedir/$oldrev"
			echo $newrev
		fi
	done
}

cache_miss () {
	for oldrev in "$@"
	do
		if ! test -r "$cachedir/$oldrev"
		then
			echo $oldrev
		fi
	done
}

check_parents () {
	missed=$(cache_miss "$1")
	local indent=$(($2 + 1))
	for miss in $missed
	do
		if ! test -r "$cachedir/notree/$miss"
		then
			debug "  incorrect order: $miss"
			process_split_commit "$miss" "" "$indent"
		fi
	done
}

set_notree () {
	echo "1" > "$cachedir/notree/$1"
}

cache_set () {
	oldrev="$1"
	newrev="$2"
	if test "$oldrev" != "latest_old" &&
		test "$oldrev" != "latest_new" &&
		test -e "$cachedir/$oldrev"
	then
		die "cache for $oldrev already exists!"
	fi
	echo "$newrev" >"$cachedir/$oldrev"
}

rev_exists () {
	if git rev-parse "$1" >/dev/null 2>&1
	then
		return 0
	else
		return 1
	fi
}

rev_is_descendant_of_branch () {
	newrev="$1"
	branch="$2"
	branch_hash=$(git rev-parse "$branch")
	match=$(git rev-list -1 "$branch_hash" "^$newrev")

	if test -z "$match"
	then
		return 0
	else
		return 1
	fi
}

# if a commit doesn't have a parent, this might not work.  But we only want
# to remove the parent from the rev-list, and since it doesn't exist, it won't
# be there anyway, so do nothing in that case.
try_remove_previous () {
	if rev_exists "$1^"
	then
		echo "^$1^"
	fi
}

find_latest_squash () {
	debug "Looking for latest squash ($dir)..."
	dir="$1"
	sq=
	main=
	sub=
	git log --grep="^git-subtree-dir: $dir/*\$" \
		--no-show-signature --pretty=format:'START %H%n%s%n%n%b%nEND%n' HEAD |
	while read a b junk
	do
		debug "$a $b $junk"
		debug "{{$sq/$main/$sub}}"
		case "$a" in
		START)
			sq="$b"
			;;
		git-subtree-mainline:)
			main="$b"
			;;
		git-subtree-split:)
			sub="$(git rev-parse "$b^0")" ||
			die "could not rev-parse split hash $b from commit $sq"
			;;
		END)
			if test -n "$sub"
			then
				if test -n "$main"
				then
					# a rejoin commit?
					# Pretend its sub was a squash.
					sq="$sub"
				fi
				debug "Squash found: $sq $sub"
				echo "$sq" "$sub"
				break
			fi
			sq=
			main=
			sub=
			;;
		esac
	done
}

find_existing_splits () {
	debug "Looking for prior splits..."
	dir="$1"
	revs="$2"
	main=
	sub=
	local grep_format="^git-subtree-dir: $dir/*\$"
	if test -n "$ignore_joins"
	then
		grep_format="^Add '$dir/' from commit '"
	fi
	git log --grep="$grep_format" \
		--no-show-signature --pretty=format:'START %H%n%s%n%n%b%nEND%n' $revs |
	while read a b junk
	do
		case "$a" in
		START)
			sq="$b"
			;;
		git-subtree-mainline:)
			main="$b"
			;;
		git-subtree-split:)
			sub="$(git rev-parse "$b^0")" ||
			die "could not rev-parse split hash $b from commit $sq"
			;;
		END)
			debug "  Main is: '$main'"
			if test -z "$main" -a -n "$sub"
			then
				# squash commits refer to a subtree
				debug "  Squash: $sq from $sub"
				cache_set "$sq" "$sub"
			fi
			if test -n "$main" -a -n "$sub"
			then
				debug "  Prior: $main -> $sub"
				cache_set $main $sub
				cache_set $sub $sub
				try_remove_previous "$main"
				try_remove_previous "$sub"
			fi
			main=
			sub=
			;;
		esac
	done
}

copy_commit () {
	# We're going to set some environment vars here, so
	# do it in a subshell to get rid of them safely later
	debug copy_commit "{$1}" "{$2}" "{$3}"
	git log -1 --no-show-signature --pretty=format:'%an%n%ae%n%aD%n%cn%n%ce%n%cD%n%B' "$1" |
	(
		read GIT_AUTHOR_NAME
		read GIT_AUTHOR_EMAIL
		read GIT_AUTHOR_DATE
		read GIT_COMMITTER_NAME
		read GIT_COMMITTER_EMAIL
		read GIT_COMMITTER_DATE
		export  GIT_AUTHOR_NAME \
			GIT_AUTHOR_EMAIL \
			GIT_AUTHOR_DATE \
			GIT_COMMITTER_NAME \
			GIT_COMMITTER_EMAIL \
			GIT_COMMITTER_DATE
		(
			printf "%s" "$annotate"
			cat
		) |
		git commit-tree "$2" $3  # reads the rest of stdin
	) || die "Can't copy commit $1"
}

add_msg () {
	dir="$1"
	latest_old="$2"
	latest_new="$3"
	if test -n "$message"
	then
		commit_message="$message"
	else
		commit_message="Add '$dir/' from commit '$latest_new'"
	fi
	cat <<-EOF
		$commit_message

		git-subtree-dir: $dir
		git-subtree-mainline: $latest_old
		git-subtree-split: $latest_new
	EOF
}

add_squashed_msg () {
	if test -n "$message"
	then
		echo "$message"
	else
		echo "Merge commit '$1' as '$2'"
	fi
}

rejoin_msg () {
	dir="$1"
	latest_old="$2"
	latest_new="$3"
	if test -n "$message"
	then
		commit_message="$message"
	else
		commit_message="Split '$dir/' into commit '$latest_new'"
	fi
	cat <<-EOF
		$commit_message

		git-subtree-dir: $dir
		git-subtree-mainline: $latest_old
		git-subtree-split: $latest_new
	EOF
}

squash_msg () {
	dir="$1"
	oldsub="$2"
	newsub="$3"
	newsub_short=$(git rev-parse --short "$newsub")

	if test -n "$oldsub"
	then
		oldsub_short=$(git rev-parse --short "$oldsub")
		echo "Squashed '$dir/' changes from $oldsub_short..$newsub_short"
		echo
		git log --no-show-signature --pretty=tformat:'%h %s' "$oldsub..$newsub"
		git log --no-show-signature --pretty=tformat:'REVERT: %h %s' "$newsub..$oldsub"
	else
		echo "Squashed '$dir/' content from commit $newsub_short"
	fi

	echo
	echo "git-subtree-dir: $dir"
	echo "git-subtree-split: $newsub"
}

toptree_for_commit () {
	commit="$1"
	git rev-parse --verify "$commit^{tree}" || exit $?
}

subtree_for_commit () {
	commit="$1"
	dir="$2"
	git ls-tree "$commit" -- "$dir" |
	while read mode type tree name
	do
		assert test "$name" = "$dir"
		assert test "$type" = "tree" -o "$type" = "commit"
		test "$type" = "commit" && continue  # ignore submodules
		echo $tree
		break
	done
}

tree_changed () {
	tree=$1
	shift
	if test $# -ne 1
	then
		return 0   # weird parents, consider it changed
	else
		ptree=$(toptree_for_commit $1)
		if test "$ptree" != "$tree"
		then
			return 0   # changed
		else
			return 1   # not changed
		fi
	fi
}

new_squash_commit () {
	old="$1"
	oldsub="$2"
	newsub="$3"
	tree=$(toptree_for_commit $newsub) || exit $?
	if test -n "$old"
	then
		squash_msg "$dir" "$oldsub" "$newsub" |
		git commit-tree "$tree" -p "$old" || exit $?
	else
		squash_msg "$dir" "" "$newsub" |
		git commit-tree "$tree" || exit $?
	fi
}

copy_or_skip () {
	rev="$1"
	tree="$2"
	newparents="$3"
	assert test -n "$tree"

	identical=
	nonidentical=
	p=
	gotparents=
	copycommit=
	for parent in $newparents
	do
		ptree=$(toptree_for_commit $parent) || exit $?
		test -z "$ptree" && continue
		if test "$ptree" = "$tree"
		then
			# an identical parent could be used in place of this rev.
			if test -n "$identical"
			then
				# if a previous identical parent was found, check whether
				# one is already an ancestor of the other
				mergebase=$(git merge-base $identical $parent)
				if test "$identical" = "$mergebase"
				then
					# current identical commit is an ancestor of parent
					identical="$parent"
				elif test "$parent" != "$mergebase"
				then
					# no common history; commit must be copied
					copycommit=1
				fi
			else
				# first identical parent detected
				identical="$parent"
			fi
		else
			nonidentical="$parent"
		fi

		# sometimes both old parents map to the same newparent;
		# eliminate duplicates
		is_new=1
		for gp in $gotparents
		do
			if test "$gp" = "$parent"
			then
				is_new=
				break
			fi
		done
		if test -n "$is_new"
		then
			gotparents="$gotparents $parent"
			p="$p -p $parent"
		fi
	done

	if test -n "$identical" && test -n "$nonidentical"
	then
		extras=$(git rev-list --count $identical..$nonidentical)
		if test "$extras" -ne 0
		then
			# we need to preserve history along the other branch
			copycommit=1
		fi
	fi
	if test -n "$identical" && test -z "$copycommit"
	then
		echo $identical
	else
		copy_commit "$rev" "$tree" "$p" || exit $?
	fi
}

ensure_clean () {
	if ! git diff-index HEAD --exit-code --quiet 2>&1
	then
		die "Working tree has modifications.  Cannot add."
	fi
	if ! git diff-index --cached HEAD --exit-code --quiet 2>&1
	then
		die "Index has modifications.  Cannot add."
	fi
}

ensure_valid_ref_format () {
	git check-ref-format "refs/heads/$1" ||
		die "'$1' does not look like a ref"
}

process_split_commit () {
	local rev="$1"
	local parents="$2"
	local indent=$3

	if test $indent -eq 0
	then
		revcount=$(($revcount + 1))
	else
		# processing commit without normal parent information;
		# fetch from repo
		parents=$(git rev-parse "$rev^@")
		extracount=$(($extracount + 1))
	fi

	progress "$revcount/$revmax ($createcount) [$extracount]"

	debug "Processing commit: $rev"
	exists=$(cache_get "$rev")
	if test -n "$exists"
	then
		debug "  prior: $exists"
		return
	fi
	createcount=$(($createcount + 1))
	debug "  parents: $parents"
	check_parents "$parents" "$indent"
	newparents=$(cache_get $parents)
	debug "  newparents: $newparents"

	tree=$(subtree_for_commit "$rev" "$dir")
	debug "  tree is: $tree"

	# ugly.  is there no better way to tell if this is a subtree
	# vs. a mainline commit?  Does it matter?
	if test -z "$tree"
	then
		set_notree "$rev"
		if test -n "$newparents"
		then
			cache_set "$rev" "$rev"
		fi
		return
	fi

	newrev=$(copy_or_skip "$rev" "$tree" "$newparents") || exit $?
	debug "  newrev is: $newrev"
	cache_set "$rev" "$newrev"
	cache_set latest_new "$newrev"
	cache_set latest_old "$rev"
}

cmd_add () {
	if test -e "$dir"
	then
		die "'$dir' already exists.  Cannot add."
	fi

	ensure_clean

	if test $# -eq 1
	then
		git rev-parse -q --verify "$1^{commit}" >/dev/null ||
			die "'$1' does not refer to a commit"

		cmd_add_commit "$@"

	elif test $# -eq 2
	then
		# Technically we could accept a refspec here but we're
		# just going to turn around and add FETCH_HEAD under the
		# specified directory.  Allowing a refspec might be
		# misleading because we won't do anything with any other
		# branches fetched via the refspec.
		ensure_valid_ref_format "$2"

		cmd_add_repository "$@"
	else
		say "error: parameters were '$@'"
		die "Provide either a commit or a repository and commit."
	fi
}

cmd_add_repository () {
	echo "git fetch" "$@"
	repository=$1
	refspec=$2
	git fetch "$@" || exit $?
	revs=FETCH_HEAD
	set -- $revs
	cmd_add_commit "$@"
}

cmd_add_commit () {
	revs=$(git rev-parse $default --revs-only "$@") || exit $?
	set -- $revs
	rev="$1"

	debug "Adding $dir as '$rev'..."
	git read-tree --prefix="$dir" $rev || exit $?
	git checkout -- "$dir" || exit $?
	tree=$(git write-tree) || exit $?

	headrev=$(git rev-parse HEAD) || exit $?
	if test -n "$headrev" && test "$headrev" != "$rev"
	then
		headp="-p $headrev"
	else
		headp=
	fi

	if test -n "$squash"
	then
		rev=$(new_squash_commit "" "" "$rev") || exit $?
		commit=$(add_squashed_msg "$rev" "$dir" |
			git commit-tree "$tree" $headp -p "$rev") || exit $?
	else
		revp=$(peel_committish "$rev") &&
		commit=$(add_msg "$dir" $headrev "$rev" |
			git commit-tree "$tree" $headp -p "$revp") || exit $?
	fi
	git reset "$commit" || exit $?

	say "Added dir '$dir'"
}

cmd_split () {
	debug "Splitting $dir..."
	cache_setup || exit $?

	if test -n "$onto"
	then
		debug "Reading history for --onto=$onto..."
		git rev-list $onto |
		while read rev
		do
			# the 'onto' history is already just the subdir, so
			# any parent we find there can be used verbatim
			debug "  cache: $rev"
			cache_set "$rev" "$rev"
		done
	fi

	unrevs="$(find_existing_splits "$dir" "$revs")"

	# We can't restrict rev-list to only $dir here, because some of our
	# parents have the $dir contents the root, and those won't match.
	# (and rev-list --follow doesn't seem to solve this)
	grl='git rev-list --topo-order --reverse --parents $revs $unrevs'
	revmax=$(eval "$grl" | wc -l)
	revcount=0
	createcount=0
	extracount=0
	eval "$grl" |
	while read rev parents
	do
		process_split_commit "$rev" "$parents" 0
	done || exit $?

	latest_new=$(cache_get latest_new)
	if test -z "$latest_new"
	then
		die "No new revisions were found"
	fi

	if test -n "$rejoin"
	then
		debug "Merging split branch into HEAD..."
		latest_old=$(cache_get latest_old)
		git merge -s ours \
			--allow-unrelated-histories \
			-m "$(rejoin_msg "$dir" "$latest_old" "$latest_new")" \
			"$latest_new" >&2 || exit $?
	fi
	if test -n "$branch"
	then
		if rev_exists "refs/heads/$branch"
		then
			if ! rev_is_descendant_of_branch "$latest_new" "$branch"
			then
				die "Branch '$branch' is not an ancestor of commit '$latest_new'."
			fi
			action='Updated'
		else
			action='Created'
		fi
		git update-ref -m 'subtree split' \
			"refs/heads/$branch" "$latest_new" || exit $?
		say "$action branch '$branch'"
	fi
	echo "$latest_new"
	exit 0
}

cmd_merge () {
	revs=$(git rev-parse $default --revs-only "$@") || exit $?
	ensure_clean

	set -- $revs
	if test $# -ne 1
	then
		die "You must provide exactly one revision.  Got: '$revs'"
	fi
	rev="$1"

	if test -n "$squash"
	then
		first_split="$(find_latest_squash "$dir")"
		if test -z "$first_split"
		then
			die "Can't squash-merge: '$dir' was never added."
		fi
		set $first_split
		old=$1
		sub=$2
		if test "$sub" = "$rev"
		then
			say "Subtree is already at commit $rev."
			exit 0
		fi
		new=$(new_squash_commit "$old" "$sub" "$rev") || exit $?
		debug "New squash commit: $new"
		rev="$new"
	fi

	version=$(git version)
	if test "$version" \< "git version 1.7"
	then
		if test -n "$message"
		then
			git merge -s subtree --message="$message" "$rev"
		else
			git merge -s subtree "$rev"
		fi
	else
		if test -n "$message"
		then
			git merge -Xsubtree="$prefix" \
				--message="$message" "$rev"
		else
			git merge -Xsubtree="$prefix" $rev
		fi
	fi
}

cmd_pull () {
	if test $# -ne 2
	then
		die "You must provide <repository> <ref>"
	fi
	ensure_clean
	ensure_valid_ref_format "$2"
	git fetch "$@" || exit $?
	revs=FETCH_HEAD
	set -- $revs
	cmd_merge "$@"
}

cmd_push () {
	if test $# -ne 2
	then
		die "You must provide <repository> <ref>"
	fi
	ensure_valid_ref_format "$2"
	if test -e "$dir"
	then
		repository=$1
		refspec=$2
		echo "git push using: " "$repository" "$refspec"
		localrev=$(git subtree split --prefix="$prefix") || die
		git push "$repository" "$localrev":"refs/heads/$refspec"
	else
		die "'$dir' must already exist. Try 'git subtree add'."
	fi
}

"cmd_$command" "$@"
