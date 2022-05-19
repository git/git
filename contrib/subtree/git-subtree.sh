#!/bin/sh
#
# but-subtree.sh: split/join but repositories in subdirectories of this one
#
# Copyright (C) 2009 Avery Pennarun <apenwarr@gmail.com>
#

if test -z "$GIT_EXEC_PATH" || ! test -f "$GIT_EXEC_PATH/but-sh-setup" || {
	test "${PATH#"${GIT_EXEC_PATH}:"}" = "$PATH" &&
	test ! "$GIT_EXEC_PATH" -ef "${PATH%%:*}" 2>/dev/null
}
then
	basename=${0##*[/\\]}
	echo >&2 'It looks like either your but installation or your'
	echo >&2 'but-subtree installation is broken.'
	echo >&2
	echo >&2 "Tips:"
	echo >&2 " - If \`but --exec-path\` does not print the correct path to"
	echo >&2 "   your but install directory, then set the GIT_EXEC_PATH"
	echo >&2 "   environment variable to the correct directory."
	echo >&2 " - Make sure that your \`$basename\` file is either in your"
	echo >&2 "   PATH or in your but exec path (\`$(but --exec-path)\`)."
	echo >&2 " - You should run but-subtree as \`but ${basename#but-}\`,"
	echo >&2 "   not as \`$basename\`." >&2
	exit 126
fi

OPTS_SPEC="\
but subtree add   --prefix=<prefix> <cummit>
but subtree add   --prefix=<prefix> <repository> <ref>
but subtree merge --prefix=<prefix> <cummit>
but subtree split --prefix=<prefix> [<cummit>]
but subtree pull  --prefix=<prefix> <repository> <ref>
but subtree push  --prefix=<prefix> <repository> <refspec>
--
h,help        show the help
q             quiet
d             show debug messages
P,prefix=     the name of the subdir to split out
 options for 'split' (also: 'push')
annotate=     add a prefix to cummit message of new cummits
b,branch=     create a new branch from the split subtree
ignore-joins  ignore prior --rejoin cummits
onto=         try connecting new tree to an existing one
rejoin        merge the new branch back into HEAD
 options for 'add' and 'merge' (also: 'pull', 'split --rejoin', and 'push --rejoin')
squash        merge subtree changes as a single cummit
m,message=    use the given message as the cummit message for the merge cummit
"

indent=0

# Usage: debug [MSG...]
debug () {
	if test -n "$arg_debug"
	then
		printf "%$(($indent * 2))s%s\n" '' "$*" >&2
	fi
}

# Usage: progress [MSG...]
progress () {
	if test -z "$GIT_QUIET"
	then
		if test -z "$arg_debug"
		then
			# Debug mode is off.
			#
			# Print one progress line that we keep updating (use
			# "\r" to return to the beginning of the line, rather
			# than "\n" to start a new line).  This only really
			# works when stderr is a terminal.
			printf "%s\r" "$*" >&2
		else
			# Debug mode is on.  The `debug` function is regularly
			# printing to stderr.
			#
			# Don't do the one-line-with-"\r" thing, because on a
			# terminal the debug output would overwrite and hide the
			# progress output.  Add a "progress:" prefix to make the
			# progress output and the debug output easy to
			# distinguish.  This ensures maximum readability whether
			# stderr is a terminal or a file.
			printf "progress: %s\n" "$*" >&2
		fi
	fi
}

# Usage: assert CMD...
assert () {
	if ! "$@"
	then
		die "assertion failed: $*"
	fi
}

main () {
	if test $# -eq 0
	then
		set -- -h
	fi
	set_args="$(echo "$OPTS_SPEC" | but rev-parse --parseopt -- "$@" || echo exit $?)"
	eval "$set_args"
	. but-sh-setup
	require_work_tree

	# First figure out the command and whether we use --rejoin, so
	# that we can provide more helpful validation when we do the
	# "real" flag parsing.
	arg_split_rejoin=
	allow_split=
	allow_addmerge=
	while test $# -gt 0
	do
		opt="$1"
		shift
		case "$opt" in
			--annotate|-b|-P|-m|--onto)
				shift
				;;
			--rejoin)
				arg_split_rejoin=1
				;;
			--no-rejoin)
				arg_split_rejoin=
				;;
			--)
				break
				;;
		esac
	done
	arg_command=$1
	case "$arg_command" in
	add|merge|pull)
		allow_addmerge=1
		;;
	split|push)
		allow_split=1
		allow_addmerge=$arg_split_rejoin
		;;
	*)
		die "Unknown command '$arg_command'"
		;;
	esac
	# Reset the arguments array for "real" flag parsing.
	eval "$set_args"

	# Begin "real" flag parsing.
	arg_debug=
	arg_prefix=
	arg_split_branch=
	arg_split_onto=
	arg_split_ignore_joins=
	arg_split_annotate=
	arg_addmerge_squash=
	arg_addmerge_message=
	while test $# -gt 0
	do
		opt="$1"
		shift

		case "$opt" in
		-q)
			GIT_QUIET=1
			;;
		-d)
			arg_debug=1
			;;
		--annotate)
			test -n "$allow_split" || die "The '$opt' flag does not make sense with 'but subtree $arg_command'."
			arg_split_annotate="$1"
			shift
			;;
		--no-annotate)
			test -n "$allow_split" || die "The '$opt' flag does not make sense with 'but subtree $arg_command'."
			arg_split_annotate=
			;;
		-b)
			test -n "$allow_split" || die "The '$opt' flag does not make sense with 'but subtree $arg_command'."
			arg_split_branch="$1"
			shift
			;;
		-P)
			arg_prefix="${1%/}"
			shift
			;;
		-m)
			test -n "$allow_addmerge" || die "The '$opt' flag does not make sense with 'but subtree $arg_command'."
			arg_addmerge_message="$1"
			shift
			;;
		--no-prefix)
			arg_prefix=
			;;
		--onto)
			test -n "$allow_split" || die "The '$opt' flag does not make sense with 'but subtree $arg_command'."
			arg_split_onto="$1"
			shift
			;;
		--no-onto)
			test -n "$allow_split" || die "The '$opt' flag does not make sense with 'but subtree $arg_command'."
			arg_split_onto=
			;;
		--rejoin)
			test -n "$allow_split" || die "The '$opt' flag does not make sense with 'but subtree $arg_command'."
			;;
		--no-rejoin)
			test -n "$allow_split" || die "The '$opt' flag does not make sense with 'but subtree $arg_command'."
			;;
		--ignore-joins)
			test -n "$allow_split" || die "The '$opt' flag does not make sense with 'but subtree $arg_command'."
			arg_split_ignore_joins=1
			;;
		--no-ignore-joins)
			test -n "$allow_split" || die "The '$opt' flag does not make sense with 'but subtree $arg_command'."
			arg_split_ignore_joins=
			;;
		--squash)
			test -n "$allow_addmerge" || die "The '$opt' flag does not make sense with 'but subtree $arg_command'."
			arg_addmerge_squash=1
			;;
		--no-squash)
			test -n "$allow_addmerge" || die "The '$opt' flag does not make sense with 'but subtree $arg_command'."
			arg_addmerge_squash=
			;;
		--)
			break
			;;
		*)
			die "Unexpected option: $opt"
			;;
		esac
	done
	shift

	if test -z "$arg_prefix"
	then
		die "You must provide the --prefix option."
	fi

	case "$arg_command" in
	add)
		test -e "$arg_prefix" &&
			die "prefix '$arg_prefix' already exists."
		;;
	*)
		test -e "$arg_prefix" ||
			die "'$arg_prefix' does not exist; use 'but subtree add'"
		;;
	esac

	dir="$(dirname "$arg_prefix/.")"

	debug "command: {$arg_command}"
	debug "quiet: {$GIT_QUIET}"
	debug "dir: {$dir}"
	debug "opts: {$*}"
	debug

	"cmd_$arg_command" "$@"
}

# Usage: cache_setup
cache_setup () {
	assert test $# = 0
	cachedir="$GIT_DIR/subtree-cache/$$"
	rm -rf "$cachedir" ||
		die "Can't delete old cachedir: $cachedir"
	mkdir -p "$cachedir" ||
		die "Can't create new cachedir: $cachedir"
	mkdir -p "$cachedir/notree" ||
		die "Can't create new cachedir: $cachedir/notree"
	debug "Using cachedir: $cachedir" >&2
}

# Usage: cache_get [REVS...]
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

# Usage: cache_miss [REVS...]
cache_miss () {
	for oldrev in "$@"
	do
		if ! test -r "$cachedir/$oldrev"
		then
			echo $oldrev
		fi
	done
}

# Usage: check_parents [REVS...]
check_parents () {
	missed=$(cache_miss "$@") || exit $?
	local indent=$(($indent + 1))
	for miss in $missed
	do
		if ! test -r "$cachedir/notree/$miss"
		then
			debug "incorrect order: $miss"
			process_split_cummit "$miss" ""
		fi
	done
}

# Usage: set_notree REV
set_notree () {
	assert test $# = 1
	echo "1" > "$cachedir/notree/$1"
}

# Usage: cache_set OLDREV NEWREV
cache_set () {
	assert test $# = 2
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

# Usage: rev_exists REV
rev_exists () {
	assert test $# = 1
	if but rev-parse "$1" >/dev/null 2>&1
	then
		return 0
	else
		return 1
	fi
}

# Usage: try_remove_previous REV
#
# If a cummit doesn't have a parent, this might not work.  But we only want
# to remove the parent from the rev-list, and since it doesn't exist, it won't
# be there anyway, so do nothing in that case.
try_remove_previous () {
	assert test $# = 1
	if rev_exists "$1^"
	then
		echo "^$1^"
	fi
}

# Usage: find_latest_squash DIR
find_latest_squash () {
	assert test $# = 1
	debug "Looking for latest squash ($dir)..."
	local indent=$(($indent + 1))

	dir="$1"
	sq=
	main=
	sub=
	but log --grep="^but-subtree-dir: $dir/*\$" \
		--no-show-signature --pretty=format:'START %H%n%s%n%n%b%nEND%n' HEAD |
	while read a b junk
	do
		debug "$a $b $junk"
		debug "{{$sq/$main/$sub}}"
		case "$a" in
		START)
			sq="$b"
			;;
		but-subtree-mainline:)
			main="$b"
			;;
		but-subtree-split:)
			sub="$(but rev-parse "$b^{cummit}")" ||
			die "could not rev-parse split hash $b from cummit $sq"
			;;
		END)
			if test -n "$sub"
			then
				if test -n "$main"
				then
					# a rejoin cummit?
					# Pretend its sub was a squash.
					sq=$(but rev-parse --verify "$sq^2") ||
						die
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
	done || exit $?
}

# Usage: find_existing_splits DIR REV
find_existing_splits () {
	assert test $# = 2
	debug "Looking for prior splits..."
	local indent=$(($indent + 1))

	dir="$1"
	rev="$2"
	main=
	sub=
	local grep_format="^but-subtree-dir: $dir/*\$"
	if test -n "$arg_split_ignore_joins"
	then
		grep_format="^Add '$dir/' from cummit '"
	fi
	but log --grep="$grep_format" \
		--no-show-signature --pretty=format:'START %H%n%s%n%n%b%nEND%n' "$rev" |
	while read a b junk
	do
		case "$a" in
		START)
			sq="$b"
			;;
		but-subtree-mainline:)
			main="$b"
			;;
		but-subtree-split:)
			sub="$(but rev-parse "$b^{cummit}")" ||
			die "could not rev-parse split hash $b from cummit $sq"
			;;
		END)
			debug "Main is: '$main'"
			if test -z "$main" -a -n "$sub"
			then
				# squash cummits refer to a subtree
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
	done || exit $?
}

# Usage: copy_cummit REV TREE FLAGS_STR
copy_cummit () {
	assert test $# = 3
	# We're going to set some environment vars here, so
	# do it in a subshell to get rid of them safely later
	debug copy_cummit "{$1}" "{$2}" "{$3}"
	but log -1 --no-show-signature --pretty=format:'%an%n%ae%n%aD%n%cn%n%ce%n%cD%n%B' "$1" |
	(
		read GIT_AUTHOR_NAME
		read GIT_AUTHOR_EMAIL
		read GIT_AUTHOR_DATE
		read GIT_CUMMITTER_NAME
		read GIT_CUMMITTER_EMAIL
		read GIT_CUMMITTER_DATE
		export  GIT_AUTHOR_NAME \
			GIT_AUTHOR_EMAIL \
			GIT_AUTHOR_DATE \
			GIT_CUMMITTER_NAME \
			GIT_CUMMITTER_EMAIL \
			GIT_CUMMITTER_DATE
		(
			printf "%s" "$arg_split_annotate"
			cat
		) |
		but cummit-tree "$2" $3  # reads the rest of stdin
	) || die "Can't copy cummit $1"
}

# Usage: add_msg DIR LATEST_OLD LATEST_NEW
add_msg () {
	assert test $# = 3
	dir="$1"
	latest_old="$2"
	latest_new="$3"
	if test -n "$arg_addmerge_message"
	then
		cummit_message="$arg_addmerge_message"
	else
		cummit_message="Add '$dir/' from cummit '$latest_new'"
	fi
	if test -n "$arg_split_rejoin"
	then
		# If this is from a --rejoin, then rejoin_msg has
		# already inserted the `but-subtree-xxx:` tags
		echo "$cummit_message"
		return
	fi
	cat <<-EOF
		$cummit_message

		but-subtree-dir: $dir
		but-subtree-mainline: $latest_old
		but-subtree-split: $latest_new
	EOF
}

# Usage: add_squashed_msg REV DIR
add_squashed_msg () {
	assert test $# = 2
	if test -n "$arg_addmerge_message"
	then
		echo "$arg_addmerge_message"
	else
		echo "Merge cummit '$1' as '$2'"
	fi
}

# Usage: rejoin_msg DIR LATEST_OLD LATEST_NEW
rejoin_msg () {
	assert test $# = 3
	dir="$1"
	latest_old="$2"
	latest_new="$3"
	if test -n "$arg_addmerge_message"
	then
		cummit_message="$arg_addmerge_message"
	else
		cummit_message="Split '$dir/' into cummit '$latest_new'"
	fi
	cat <<-EOF
		$cummit_message

		but-subtree-dir: $dir
		but-subtree-mainline: $latest_old
		but-subtree-split: $latest_new
	EOF
}

# Usage: squash_msg DIR OLD_SUBTREE_cummit NEW_SUBTREE_CUMMIT
squash_msg () {
	assert test $# = 3
	dir="$1"
	oldsub="$2"
	newsub="$3"
	newsub_short=$(but rev-parse --short "$newsub")

	if test -n "$oldsub"
	then
		oldsub_short=$(but rev-parse --short "$oldsub")
		echo "Squashed '$dir/' changes from $oldsub_short..$newsub_short"
		echo
		but log --no-show-signature --pretty=tformat:'%h %s' "$oldsub..$newsub"
		but log --no-show-signature --pretty=tformat:'REVERT: %h %s' "$newsub..$oldsub"
	else
		echo "Squashed '$dir/' content from cummit $newsub_short"
	fi

	echo
	echo "but-subtree-dir: $dir"
	echo "but-subtree-split: $newsub"
}

# Usage: toptree_for_cummit cummit
toptree_for_cummit () {
	assert test $# = 1
	cummit="$1"
	but rev-parse --verify "$cummit^{tree}" || exit $?
}

# Usage: subtree_for_cummit cummit DIR
subtree_for_cummit () {
	assert test $# = 2
	cummit="$1"
	dir="$2"
	but ls-tree "$cummit" -- "$dir" |
	while read mode type tree name
	do
		assert test "$name" = "$dir"
		assert test "$type" = "tree" -o "$type" = "cummit"
		test "$type" = "cummit" && continue  # ignore submodules
		echo $tree
		break
	done || exit $?
}

# Usage: tree_changed TREE [PARENTS...]
tree_changed () {
	assert test $# -gt 0
	tree=$1
	shift
	if test $# -ne 1
	then
		return 0   # weird parents, consider it changed
	else
		ptree=$(toptree_for_cummit $1) || exit $?
		if test "$ptree" != "$tree"
		then
			return 0   # changed
		else
			return 1   # not changed
		fi
	fi
}

# Usage: new_squash_cummit OLD_SQUASHED_cummit OLD_NONSQUASHED_cummit NEW_NONSQUASHED_CUMMIT
new_squash_cummit () {
	assert test $# = 3
	old="$1"
	oldsub="$2"
	newsub="$3"
	tree=$(toptree_for_cummit $newsub) || exit $?
	if test -n "$old"
	then
		squash_msg "$dir" "$oldsub" "$newsub" |
		but cummit-tree "$tree" -p "$old" || exit $?
	else
		squash_msg "$dir" "" "$newsub" |
		but cummit-tree "$tree" || exit $?
	fi
}

# Usage: copy_or_skip REV TREE NEWPARENTS
copy_or_skip () {
	assert test $# = 3
	rev="$1"
	tree="$2"
	newparents="$3"
	assert test -n "$tree"

	identical=
	nonidentical=
	p=
	gotparents=
	copycummit=
	for parent in $newparents
	do
		ptree=$(toptree_for_cummit $parent) || exit $?
		test -z "$ptree" && continue
		if test "$ptree" = "$tree"
		then
			# an identical parent could be used in place of this rev.
			if test -n "$identical"
			then
				# if a previous identical parent was found, check whether
				# one is already an ancestor of the other
				mergebase=$(but merge-base $identical $parent)
				if test "$identical" = "$mergebase"
				then
					# current identical cummit is an ancestor of parent
					identical="$parent"
				elif test "$parent" != "$mergebase"
				then
					# no common history; cummit must be copied
					copycummit=1
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
		extras=$(but rev-list --count $identical..$nonidentical)
		if test "$extras" -ne 0
		then
			# we need to preserve history along the other branch
			copycummit=1
		fi
	fi
	if test -n "$identical" && test -z "$copycummit"
	then
		echo $identical
	else
		copy_cummit "$rev" "$tree" "$p" || exit $?
	fi
}

# Usage: ensure_clean
ensure_clean () {
	assert test $# = 0
	if ! but diff-index HEAD --exit-code --quiet 2>&1
	then
		die "Working tree has modifications.  Cannot add."
	fi
	if ! but diff-index --cached HEAD --exit-code --quiet 2>&1
	then
		die "Index has modifications.  Cannot add."
	fi
}

# Usage: ensure_valid_ref_format REF
ensure_valid_ref_format () {
	assert test $# = 1
	but check-ref-format "refs/heads/$1" ||
		die "'$1' does not look like a ref"
}

# Usage: process_split_cummit REV PARENTS
process_split_cummit () {
	assert test $# = 2
	local rev="$1"
	local parents="$2"

	if test $indent -eq 0
	then
		revcount=$(($revcount + 1))
	else
		# processing cummit without normal parent information;
		# fetch from repo
		parents=$(but rev-parse "$rev^@")
		extracount=$(($extracount + 1))
	fi

	progress "$revcount/$revmax ($createcount) [$extracount]"

	debug "Processing cummit: $rev"
	local indent=$(($indent + 1))
	exists=$(cache_get "$rev") || exit $?
	if test -n "$exists"
	then
		debug "prior: $exists"
		return
	fi
	createcount=$(($createcount + 1))
	debug "parents: $parents"
	check_parents $parents
	newparents=$(cache_get $parents) || exit $?
	debug "newparents: $newparents"

	tree=$(subtree_for_cummit "$rev" "$dir") || exit $?
	debug "tree is: $tree"

	# ugly.  is there no better way to tell if this is a subtree
	# vs. a mainline cummit?  Does it matter?
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
	debug "newrev is: $newrev"
	cache_set "$rev" "$newrev"
	cache_set latest_new "$newrev"
	cache_set latest_old "$rev"
}

# Usage: cmd_add REV
#    Or: cmd_add REPOSITORY REF
cmd_add () {

	ensure_clean

	if test $# -eq 1
	then
		but rev-parse -q --verify "$1^{cummit}" >/dev/null ||
			die "'$1' does not refer to a cummit"

		cmd_add_cummit "$@"

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
		say >&2 "error: parameters were '$*'"
		die "Provide either a cummit or a repository and cummit."
	fi
}

# Usage: cmd_add_repository REPOSITORY REFSPEC
cmd_add_repository () {
	assert test $# = 2
	echo "but fetch" "$@"
	repository=$1
	refspec=$2
	but fetch "$@" || exit $?
	cmd_add_cummit FETCH_HEAD
}

# Usage: cmd_add_cummit REV
cmd_add_cummit () {
	# The rev has already been validated by cmd_add(), we just
	# need to normalize it.
	assert test $# = 1
	rev=$(but rev-parse --verify "$1^{cummit}") || exit $?

	debug "Adding $dir as '$rev'..."
	if test -z "$arg_split_rejoin"
	then
		# Only bother doing this if this is a genuine 'add',
		# not a synthetic 'add' from '--rejoin'.
		but read-tree --prefix="$dir" $rev || exit $?
	fi
	but checkout -- "$dir" || exit $?
	tree=$(but write-tree) || exit $?

	headrev=$(but rev-parse HEAD) || exit $?
	if test -n "$headrev" && test "$headrev" != "$rev"
	then
		headp="-p $headrev"
	else
		headp=
	fi

	if test -n "$arg_addmerge_squash"
	then
		rev=$(new_squash_cummit "" "" "$rev") || exit $?
		cummit=$(add_squashed_msg "$rev" "$dir" |
			but cummit-tree "$tree" $headp -p "$rev") || exit $?
	else
		revp=$(peel_cummittish "$rev") || exit $?
		cummit=$(add_msg "$dir" $headrev "$rev" |
			but cummit-tree "$tree" $headp -p "$revp") || exit $?
	fi
	but reset "$cummit" || exit $?

	say >&2 "Added dir '$dir'"
}

# Usage: cmd_split [REV]
cmd_split () {
	if test $# -eq 0
	then
		rev=$(but rev-parse HEAD)
	elif test $# -eq 1
	then
		rev=$(but rev-parse -q --verify "$1^{cummit}") ||
			die "'$1' does not refer to a cummit"
	else
		die "You must provide exactly one revision.  Got: '$*'"
	fi

	if test -n "$arg_split_rejoin"
	then
		ensure_clean
	fi

	debug "Splitting $dir..."
	cache_setup || exit $?

	if test -n "$arg_split_onto"
	then
		debug "Reading history for --onto=$arg_split_onto..."
		but rev-list $arg_split_onto |
		while read rev
		do
			# the 'onto' history is already just the subdir, so
			# any parent we find there can be used verbatim
			debug "cache: $rev"
			cache_set "$rev" "$rev"
		done || exit $?
	fi

	unrevs="$(find_existing_splits "$dir" "$rev")" || exit $?

	# We can't restrict rev-list to only $dir here, because some of our
	# parents have the $dir contents the root, and those won't match.
	# (and rev-list --follow doesn't seem to solve this)
	grl='but rev-list --topo-order --reverse --parents $rev $unrevs'
	revmax=$(eval "$grl" | wc -l)
	revcount=0
	createcount=0
	extracount=0
	eval "$grl" |
	while read rev parents
	do
		process_split_cummit "$rev" "$parents"
	done || exit $?

	latest_new=$(cache_get latest_new) || exit $?
	if test -z "$latest_new"
	then
		die "No new revisions were found"
	fi

	if test -n "$arg_split_rejoin"
	then
		debug "Merging split branch into HEAD..."
		latest_old=$(cache_get latest_old) || exit $?
		arg_addmerge_message="$(rejoin_msg "$dir" "$latest_old" "$latest_new")" || exit $?
		if test -z "$(find_latest_squash "$dir")"
		then
			cmd_add "$latest_new" >&2 || exit $?
		else
			cmd_merge "$latest_new" >&2 || exit $?
		fi
	fi
	if test -n "$arg_split_branch"
	then
		if rev_exists "refs/heads/$arg_split_branch"
		then
			if ! but merge-base --is-ancestor "$arg_split_branch" "$latest_new"
			then
				die "Branch '$arg_split_branch' is not an ancestor of cummit '$latest_new'."
			fi
			action='Updated'
		else
			action='Created'
		fi
		but update-ref -m 'subtree split' \
			"refs/heads/$arg_split_branch" "$latest_new" || exit $?
		say >&2 "$action branch '$arg_split_branch'"
	fi
	echo "$latest_new"
	exit 0
}

# Usage: cmd_merge REV
cmd_merge () {
	test $# -eq 1 ||
		die "You must provide exactly one revision.  Got: '$*'"
	rev=$(but rev-parse -q --verify "$1^{cummit}") ||
		die "'$1' does not refer to a cummit"
	ensure_clean

	if test -n "$arg_addmerge_squash"
	then
		first_split="$(find_latest_squash "$dir")" || exit $?
		if test -z "$first_split"
		then
			die "Can't squash-merge: '$dir' was never added."
		fi
		set $first_split
		old=$1
		sub=$2
		if test "$sub" = "$rev"
		then
			say >&2 "Subtree is already at cummit $rev."
			exit 0
		fi
		new=$(new_squash_cummit "$old" "$sub" "$rev") || exit $?
		debug "New squash cummit: $new"
		rev="$new"
	fi

	if test -n "$arg_addmerge_message"
	then
		but merge --no-ff -Xsubtree="$arg_prefix" \
			--message="$arg_addmerge_message" "$rev"
	else
		but merge --no-ff -Xsubtree="$arg_prefix" $rev
	fi
}

# Usage: cmd_pull REPOSITORY REMOTEREF
cmd_pull () {
	if test $# -ne 2
	then
		die "You must provide <repository> <ref>"
	fi
	ensure_clean
	ensure_valid_ref_format "$2"
	but fetch "$@" || exit $?
	cmd_merge FETCH_HEAD
}

# Usage: cmd_push REPOSITORY [+][LOCALREV:]REMOTEREF
cmd_push () {
	if test $# -ne 2
	then
		die "You must provide <repository> <refspec>"
	fi
	if test -e "$dir"
	then
		repository=$1
		refspec=${2#+}
		remoteref=${refspec#*:}
		if test "$remoteref" = "$refspec"
		then
			localrevname_presplit=HEAD
		else
			localrevname_presplit=${refspec%%:*}
		fi
		ensure_valid_ref_format "$remoteref"
		localrev_presplit=$(but rev-parse -q --verify "$localrevname_presplit^{cummit}") ||
			die "'$localrevname_presplit' does not refer to a cummit"

		echo "but push using: " "$repository" "$refspec"
		localrev=$(cmd_split "$localrev_presplit") || die
		but push "$repository" "$localrev":"refs/heads/$remoteref"
	else
		die "'$dir' must already exist. Try 'but subtree add'."
	fi
}

main "$@"
