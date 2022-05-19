#!/bin/sh
#
# Rewrite revision history
# Copyright (c) Petr Baudis, 2006
# Minimal changes to "port" it to core-but (c) Johannes Schindelin, 2007
#
# Lets you rewrite the revision history of the current branch, creating
# a new branch. You can specify a number of filters to modify the cummits,
# files and trees.

# The following functions will also be available in the cummit filter:

functions=$(cat << \EOF
EMPTY_TREE=$(but hash-object -t tree /dev/null)

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

# if you run 'skip_cummit "$@"' in a cummit filter, it will print
# the (mapped) parents, effectively skipping the cummit.

skip_cummit()
{
	shift;
	while [ -n "$1" ];
	do
		shift;
		map "$1";
		shift;
	done;
}

# if you run 'but_cummit_non_empty_tree "$@"' in a cummit filter,
# it will skip cummits that leave the tree untouched, cummit the other.
but_cummit_non_empty_tree()
{
	if test $# = 3 && test "$1" = $(but rev-parse "$3^{tree}"); then
		map "$3"
	elif test $# = 1 && test "$1" = $EMPTY_TREE; then
		:
	else
		but cummit-tree "$@"
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
	echo "case \"\$BUT_$1_NAME\" in \"\") BUT_$1_NAME=\"\${BUT_$1_EMAIL%%@*}\" && export BUT_$1_NAME;; esac"
	# And make sure everything is exported.
	echo "export BUT_$1_NAME"
	echo "export BUT_$1_EMAIL"
	echo "export BUT_$1_DATE"
}

set_ident () {
	parse_ident_from_cummit author AUTHOR cummitter cummitTER
	finish_ident AUTHOR
	finish_ident cummitTER
}

if test -z "$FILTER_BRANCH_SQUELCH_WARNING$BUT_TEST_DISALLOW_ABBREVIATED_OPTIONS"
then
	cat <<EOF
WARNING: but-filter-branch has a glut of gotchas generating mangled history
	 rewrites.  Hit Ctrl-C before proceeding to abort, then use an
	 alternative filtering tool such as 'but filter-repo'
	 (https://buthub.com/newren/but-filter-repo/) instead.  See the
	 filter-branch manual page for more details; to squelch this warning,
	 set FILTER_BRANCH_SQUELCH_WARNING=1.
EOF
	sleep 10
	printf "Proceeding with filter-branch...\n\n"
fi

USAGE="[--setup <command>] [--subdirectory-filter <directory>] [--env-filter <command>]
	[--tree-filter <command>] [--index-filter <command>]
	[--parent-filter <command>] [--msg-filter <command>]
	[--cummit-filter <command>] [--tag-name-filter <command>]
	[--original <namespace>]
	[-d <directory>] [-f | --force] [--state-branch <branch>]
	[--] [<rev-list options>...]"

OPTIONS_SPEC=
. but-sh-setup

if [ "$(is_bare_repository)" = false ]; then
	require_clean_work_tree 'rewrite branches'
fi

tempdir=.but-rewrite
filter_setup=
filter_env=
filter_tree=
filter_index=
filter_parent=
filter_msg=cat
filter_cummit=
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
	--cummit-filter)
		filter_cummit="$functions; $OPTARG"
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

case "$prune_empty,$filter_cummit" in
,)
	filter_cummit='but cummit-tree "$@"';;
t,)
	filter_cummit="$functions;"' but_cummit_non_empty_tree "$@"';;
,*)
	;;
*)
	die "Cannot set --prune-empty and --cummit-filter at the same time"
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

ORIG_BUT_DIR="$BUT_DIR"
ORIG_BUT_WORK_TREE="$BUT_WORK_TREE"
ORIG_BUT_INDEX_FILE="$BUT_INDEX_FILE"
ORIG_BUT_AUTHOR_NAME="$BUT_AUTHOR_NAME"
ORIG_BUT_AUTHOR_EMAIL="$BUT_AUTHOR_EMAIL"
ORIG_BUT_AUTHOR_DATE="$BUT_AUTHOR_DATE"
ORIG_BUT_CUMMITTER_NAME="$BUT_CUMMITTER_NAME"
ORIG_BUT_CUMMITTER_EMAIL="$BUT_CUMMITTER_EMAIL"
ORIG_BUT_CUMMITTER_DATE="$BUT_CUMMITTER_DATE"

BUT_WORK_TREE=.
export BUT_DIR BUT_WORK_TREE

# Make sure refs/original is empty
but for-each-ref > "$tempdir"/backup-refs || exit
while read sha1 type name
do
	case "$force,$name" in
	,$orig_namespace*)
		die "Cannot create a new backup.
A previous backup already exists in $orig_namespace
Force overwriting the backup with -f"
	;;
	t,$orig_namespace*)
		but update-ref -d "$name" $sha1
	;;
	esac
done < "$tempdir"/backup-refs

# The refs should be updated if their heads were rewritten
but rev-parse --no-flags --revs-only --symbolic-full-name \
	--default HEAD "$@" > "$tempdir"/raw-refs || exit
while read ref
do
	case "$ref" in ^?*) continue ;; esac

	if but rev-parse --verify "$ref"^0 >/dev/null 2>&1
	then
		echo "$ref"
	else
		warn "WARNING: not rewriting '$ref' (not a cummittish)"
	fi
done >"$tempdir"/heads <"$tempdir"/raw-refs

test -s "$tempdir"/heads ||
	die "You must specify a ref to rewrite."

BUT_INDEX_FILE="$(pwd)/../index"
export BUT_INDEX_FILE

# map old->new cummit ids for rewriting parents
mkdir ../map || die "Could not create map/ directory"

if test -n "$state_branch"
then
	state_cummit=$(but rev-parse --no-flags --revs-only "$state_branch")
	if test -n "$state_cummit"
	then
		echo "Populating map from $state_branch ($state_cummit)" 1>&2
		perl -e'open(MAP, "-|", "but show $ARGV[0]:filter.map") or die;
			while (<MAP>) {
				m/(.*):(.*)/ or die;
				open F, ">../map/$1" or die;
				print F "$2" or die;
				close(F) or die;
			}
			close(MAP) or die;' "$state_cummit" \
				|| die "Unable to load state from $state_branch:filter.map"
	else
		echo "Branch $state_branch does not exist. Will create" 1>&2
	fi
fi

# we need "--" only if there are no path arguments in $@
nonrevs=$(but rev-parse --no-revs "$@") || exit
if test -z "$nonrevs"
then
	dashdash=--
else
	dashdash=
	remap_to_ancestor=t
fi

but rev-parse --revs-only "$@" >../parse

case "$filter_subdir" in
"")
	eval set -- "$(but rev-parse --sq --no-revs "$@")"
	;;
*)
	eval set -- "$(but rev-parse --sq --no-revs "$@" $dashdash \
		"$filter_subdir")"
	;;
esac

but rev-list --reverse --topo-order --default HEAD \
	--parents --simplify-merges --stdin "$@" <../parse >../revs ||
	die "Could not get the cummits"
cummits=$(wc -l <../revs | tr -d " ")

test $cummits -eq 0 && die_with_status 2 "Found nothing to rewrite"

# Rewrite the cummits
report_progress ()
{
	if test -n "$progress" &&
		test $but_filter_branch__cummit_count -gt $next_sample_at
	then
		count=$but_filter_branch__cummit_count

		now=$(date +%s)
		elapsed=$(($now - $start_timestamp))
		remaining=$(( ($cummits - $count) * $elapsed / $count ))
		if test $elapsed -gt 0
		then
			next_sample_at=$(( ($elapsed + 1) * $count / $elapsed ))
		else
			next_sample_at=$(($next_sample_at + 1))
		fi
		progress=" ($elapsed seconds passed, remaining $remaining predicted)"
	fi
	printf "\rRewrite $cummit ($count/$cummits)$progress    "
}

but_filter_branch__cummit_count=0

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

while read cummit parents; do
	but_filter_branch__cummit_count=$(($but_filter_branch__cummit_count+1))

	report_progress
	test -f "$workdir"/../map/$cummit && continue

	case "$filter_subdir" in
	"")
		if test -n "$need_index"
		then
			BUT_ALLOW_NULL_SHA1=1 but read-tree -i -m $cummit
		fi
		;;
	*)
		# The cummit may not have the subdirectory at all
		err=$(BUT_ALLOW_NULL_SHA1=1 \
		      but read-tree -i -m $cummit:"$filter_subdir" 2>&1) || {
			if ! but rev-parse -q --verify $cummit:"$filter_subdir"
			then
				rm -f "$BUT_INDEX_FILE"
			else
				echo >&2 "$err"
				false
			fi
		}
	esac || die "Could not initialize the index"

	BUT_CUMMIT=$cummit
	export BUT_CUMMIT
	but cat-file cummit "$cummit" >../cummit ||
		die "Cannot read cummit $cummit"

	eval "$(set_ident <../cummit)" ||
		die "setting author/cummitter failed for cummit $cummit"
	eval "$filter_env" < /dev/null ||
		die "env filter failed: $filter_env"

	if [ "$filter_tree" ]; then
		but checkout-index -f -u -a ||
			die "Could not checkout the index"
		# files that $cummit removed are now still in the working tree;
		# remove them, else they would be added again
		but clean -d -q -f -x
		eval "$filter_tree" < /dev/null ||
			die "tree filter failed: $filter_tree"

		(
			but diff-index -r --name-only --ignore-submodules $cummit -- &&
			but ls-files --others
		) > "$tempdir"/tree-state || exit
		but update-index --add --replace --remove --stdin \
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
		# and output the actual cummit message
		cat
	} <../cummit |
		eval "$filter_msg" > ../message ||
			die "msg filter failed: $filter_msg"

	if test -n "$need_index"
	then
		tree=$(but write-tree)
	else
		tree=$(but rev-parse "$cummit^{tree}")
	fi
	workdir=$workdir @SHELL_PATH@ -c "$filter_cummit" "but cummit-tree" \
		"$tree" $parentstr < ../message > ../map/$cummit ||
			die "could not write rewritten cummit"
done <../revs

# If we are filtering for paths, as in the case of a subdirectory
# filter, it is possible that a specified head is not in the set of
# rewritten cummits, because it was pruned by the revision walker.
# Ancestor remapping fixes this by mapping these heads to the unique
# nearest ancestor that survived the pruning.

if test "$remap_to_ancestor" = t
then
	while read ref
	do
		sha1=$(but rev-parse "$ref"^0)
		test -f "$workdir"/../map/$sha1 && continue
		ancestor=$(but rev-list --simplify-merges -1 "$ref" "$@")
		test "$ancestor" && echo $(map $ancestor) >"$workdir"/../map/$sha1
	done < "$tempdir"/heads
fi

# Finally update the refs

echo
while read ref
do
	# avoid rewriting a ref twice
	test -f "$orig_namespace$ref" && continue

	sha1=$(but rev-parse "$ref"^0)
	rewritten=$(map $sha1)

	test $sha1 = "$rewritten" &&
		warn "WARNING: Ref '$ref' is unchanged" &&
		continue

	case "$rewritten" in
	'')
		echo "Ref '$ref' was deleted"
		but update-ref -m "filter-branch: delete" -d "$ref" $sha1 ||
			die "Could not delete $ref"
	;;
	*)
		echo "Ref '$ref' was rewritten"
		if ! but update-ref -m "filter-branch: rewrite" \
					"$ref" $rewritten $sha1 2>/dev/null; then
			if test $(but cat-file -t "$ref") = tag; then
				if test -z "$filter_tag_name"; then
					warn "WARNING: You said to rewrite tagged cummits, but not the corresponding tag."
					warn "WARNING: Perhaps use '--tag-name-filter cat' to rewrite the tag."
				fi
			else
				die "Could not rewrite $ref"
			fi
		fi
	;;
	esac
	but update-ref -m "filter-branch: backup" "$orig_namespace$ref" $sha1 ||
		 exit
done < "$tempdir"/heads

# TODO: This should possibly go, with the semantics that all positive given
#       refs are updated, and their original heads stored in refs/original/
# Filter tags

if [ "$filter_tag_name" ]; then
	but for-each-ref --format='%(objectname) %(objecttype) %(refname)' refs/tags |
	while read sha1 type ref; do
		ref="${ref#refs/tags/}"
		# XXX: Rewrite tagged trees as well?
		if [ "$type" != "cummit" -a "$type" != "tag" ]; then
			continue;
		fi

		if [ "$type" = "tag" ]; then
			# Dereference to a cummit
			sha1t="$sha1"
			sha1="$(but rev-parse -q "$sha1"^{cummit})" || continue
		fi

		[ -f "../map/$sha1" ] || continue
		new_sha1="$(cat "../map/$sha1")"
		BUT_CUMMIT="$sha1"
		export BUT_CUMMIT
		new_ref="$(echo "$ref" | eval "$filter_tag_name")" ||
			die "tag name filter failed: $filter_tag_name"

		echo "$ref -> $new_ref ($sha1 -> $new_sha1)"

		if [ "$type" = "tag" ]; then
			new_sha1=$( ( printf 'object %s\ntype cummit\ntag %s\n' \
						"$new_sha1" "$new_ref"
				but cat-file tag "$ref" |
				sed -n \
				    -e '1,/^$/{
					  /^object /d
					  /^type /d
					  /^tag /d
					}' \
				    -e '/^-----BEGIN PGP SIGNATURE-----/q' \
				    -e 'p' ) |
				but hash-object -t tag -w --stdin) ||
				die "Could not create new tag object for $ref"
			if but cat-file tag "$ref" | \
			   grep '^-----BEGIN PGP SIGNATURE-----' >/dev/null 2>&1
			then
				warn "gpg signature stripped from tag object $sha1t"
			fi
		fi

		but update-ref "refs/tags/$new_ref" "$new_sha1" ||
			die "Could not write tag $new_ref"
	done
fi

unset BUT_DIR BUT_WORK_TREE BUT_INDEX_FILE
unset BUT_AUTHOR_NAME BUT_AUTHOR_EMAIL BUT_AUTHOR_DATE
unset BUT_CUMMITTER_NAME BUT_CUMMITTER_EMAIL BUT_CUMMITTER_DATE
test -z "$ORIG_BUT_DIR" || {
	BUT_DIR="$ORIG_BUT_DIR" && export BUT_DIR
}
test -z "$ORIG_BUT_WORK_TREE" || {
	BUT_WORK_TREE="$ORIG_BUT_WORK_TREE" &&
	export BUT_WORK_TREE
}
test -z "$ORIG_BUT_INDEX_FILE" || {
	BUT_INDEX_FILE="$ORIG_BUT_INDEX_FILE" &&
	export BUT_INDEX_FILE
}
test -z "$ORIG_BUT_AUTHOR_NAME" || {
	BUT_AUTHOR_NAME="$ORIG_BUT_AUTHOR_NAME" &&
	export BUT_AUTHOR_NAME
}
test -z "$ORIG_BUT_AUTHOR_EMAIL" || {
	BUT_AUTHOR_EMAIL="$ORIG_BUT_AUTHOR_EMAIL" &&
	export BUT_AUTHOR_EMAIL
}
test -z "$ORIG_BUT_AUTHOR_DATE" || {
	BUT_AUTHOR_DATE="$ORIG_BUT_AUTHOR_DATE" &&
	export BUT_AUTHOR_DATE
}
test -z "$ORIG_BUT_CUMMITTER_NAME" || {
	BUT_CUMMITTER_NAME="$ORIG_BUT_CUMMITTER_NAME" &&
	export BUT_CUMMITTER_NAME
}
test -z "$ORIG_BUT_CUMMITTER_EMAIL" || {
	BUT_CUMMITTER_EMAIL="$ORIG_BUT_CUMMITTER_EMAIL" &&
	export BUT_CUMMITTER_EMAIL
}
test -z "$ORIG_BUT_CUMMITTER_DATE" || {
	BUT_CUMMITTER_DATE="$ORIG_BUT_CUMMITTER_DATE" &&
	export BUT_CUMMITTER_DATE
}

if test -n "$state_branch"
then
	echo "Saving rewrite state to $state_branch" 1>&2
	state_blob=$(
		perl -e'opendir D, "../map" or die;
			open H, "|-", "but hash-object -w --stdin" or die;
			foreach (sort readdir(D)) {
				next if m/^\.\.?$/;
				open F, "<../map/$_" or die;
				chomp($f = <F>);
				print H "$_:$f\n" or die;
			}
			close(H) or die;' || die "Unable to save state")
	state_tree=$(printf '100644 blob %s\tfilter.map\n' "$state_blob" | but mktree)
	if test -n "$state_cummit"
	then
		state_cummit=$(echo "Sync" | but cummit-tree "$state_tree" -p "$state_cummit")
	else
		state_cummit=$(echo "Sync" | but cummit-tree "$state_tree" )
	fi
	but update-ref "$state_branch" "$state_cummit"
fi

cd "$orig_dir"
rm -rf "$tempdir"

trap - 0

if [ "$(is_bare_repository)" = false ]; then
	but read-tree -u -m HEAD || exit
fi

exit 0
