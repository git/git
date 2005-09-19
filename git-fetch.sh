#!/bin/sh
#
. git-sh-setup || die "Not a git archive"
. git-parse-remote
_x40='[0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f]'
_x40="$_x40$_x40$_x40$_x40$_x40$_x40$_x40$_x40"

append=
force=
update_head_ok=
while case "$#" in 0) break ;; esac
do
	case "$1" in
	-a|--a|--ap|--app|--appe|--appen|--append)
		append=t
		;;
	-f|--f|--fo|--for|--forc|--force)
		force=t
		;;
	-u|--u|--up|--upd|--upda|--updat|--update|--update-|--update-h|\
	--update-he|--update-hea|--update-head|--update-head-|\
	--update-head-o|--update-head-ok)
		update_head_ok=t
		;;
	*)
		break
		;;
	esac
	shift
done

case "$#" in
0)
	test -f "$GIT_DIR/branches/origin" ||
		test -f "$GIT_DIR/remotes/origin" ||
			die "Where do you want to fetch from today?"
	set origin ;;
esac

remote_nick="$1"
remote=$(get_remote_url "$@")
refs=
rref=
rsync_slurped_objects=

if test "" = "$append"
then
	: >$GIT_DIR/FETCH_HEAD
fi

append_fetch_head () {
    head_="$1"
    remote_="$2"
    remote_name_="$3"
    remote_nick_="$4"
    local_name_="$5"

    # remote-nick is the URL given on the command line (or a shorthand)
    # remote-name is the $GIT_DIR relative refs/ path we computed
    # for this refspec.
    remote_1_=$(expr "$remote_" : '\(.*\)\.git/*$') &&
	remote_="$remote_1_"
    case "$remote_" in
    . | ./) where_= ;;
    *) where_=" of $remote_" ;;
    esac
    case "$remote_name_" in
    HEAD)
	note_= ;;
    refs/heads/*)
	note_="$(expr "$remote_name_" : 'refs/heads/\(.*\)')"
	note_="branch '$note_'" ;;
    refs/tags/*)
	note_="$(expr "$remote_name_" : 'refs/tags/\(.*\)')"
	note_="tag '$note_'" ;;
    *)
	note_="$remote_name" ;;
    esac
    note_="$note_$where_"

    # 2.6.11-tree tag would not be happy to be fed to resolve.
    if git-cat-file commit "$head_" >/dev/null 2>&1
    then
	headc_=$(git-rev-parse --verify "$head_^0") || exit
	echo "$headc_	$note_" >>$GIT_DIR/FETCH_HEAD
	echo >&2 "* committish: $head_"
	echo >&2 "  $note_"
    else
	echo >&2 "* non-commit: $head_"
	echo >&2 "  $note_"
    fi
    if test "$local_name_" != ""
    then
	# We are storing the head locally.  Make sure that it is
	# a fast forward (aka "reverse push").
	fast_forward_local "$local_name_" "$head_" "$note_"
    fi
}

fast_forward_local () {
    mkdir -p "$(dirname "$GIT_DIR/$1")"
    case "$1" in
    refs/tags/*)
	# Tags need not be pointing at commits so there
	# is no way to guarantee "fast-forward" anyway.
	if test -f "$GIT_DIR/$1"
	then
		echo >&2 "* $1: updating with $3"
	else
		echo >&2 "* $1: storing $3"
	fi
	echo "$2" >"$GIT_DIR/$1" ;;

    refs/heads/*)
	# NEEDSWORK: use the same cmpxchg protocol here.
	echo "$2" >"$GIT_DIR/$1.lock"
	if test -f "$GIT_DIR/$1"
	then
	    local=$(git-rev-parse --verify "$1^0") &&
	    mb=$(git-merge-base "$local" "$2") &&
	    case "$2,$mb" in
	    $local,*)
		echo >&2 "* $1: same as $3"
		;;
	    *,$local)
		echo >&2 "* $1: fast forward to $3"
		;;
	    *)
		false
		;;
	    esac || {
		echo >&2 "* $1: does not fast forward to $3;"
		case "$force,$single_force" in
		t,* | *,t)
			echo >&2 "  forcing update."
			;;
		*)
			mv "$GIT_DIR/$1.lock" "$GIT_DIR/$1.remote"
			echo >&2 "  leaving it in '$1.remote'"
			;;
		esac
	    }
	else
		echo >&2 "* $1: storing $3"
	fi
	test -f "$GIT_DIR/$1.lock" &&
	    mv "$GIT_DIR/$1.lock" "$GIT_DIR/$1"
	;;
    esac
}

case "$update_head_ok" in
'')
	orig_head=$(cat "$GIT_DIR/HEAD" 2>/dev/null)
	;;
esac

for ref in $(get_remote_refs_for_fetch "$@")
do
    refs="$refs $ref"

    # These are relative path from $GIT_DIR, typically starting at refs/
    # but may be HEAD
    if expr "$ref" : '\+' >/dev/null
    then
	single_force=t
	ref=$(expr "$ref" : '\+\(.*\)')
    else
	single_force=
    fi
    remote_name=$(expr "$ref" : '\([^:]*\):')
    local_name=$(expr "$ref" : '[^:]*:\(.*\)')

    rref="$rref $remote_name"

    # There are transports that can fetch only one head at a time...
    case "$remote" in
    http://* | https://*)
	if [ -n "$GIT_SSL_NO_VERIFY" ]; then
	    curl_extra_args="-k"
	fi
	head=$(curl -nsf $curl_extra_args "$remote/$remote_name") &&
	expr "$head" : "$_x40\$" >/dev/null ||
		die "Failed to fetch $remote_name from $remote"
	echo Fetching "$remote_name from $remote" using http
	git-http-fetch -v -a "$head" "$remote/" || exit
	;;
    rsync://*)
	TMP_HEAD="$GIT_DIR/TMP_HEAD"
	rsync -L -q "$remote/$remote_name" "$TMP_HEAD" || exit 1
	head=$(git-rev-parse TMP_HEAD)
	rm -f "$TMP_HEAD"
	test "$rsync_slurped_objects" || {
	    rsync -av --ignore-existing --exclude info \
		"$remote/objects/" "$GIT_OBJECT_DIRECTORY/" || exit

	    # Look at objects/info/alternates for rsync -- http will
	    # support it natively and git native ones will do it on the remote
	    # end.  Not having that file is not a crime.
	    rsync -q "$remote/objects/info/alternates" \
		"$GIT_DIR/TMP_ALT" 2>/dev/null ||
		rm -f "$GIT_DIR/TMP_ALT"
	    if test -f "$GIT_DIR/TMP_ALT"
	    then
		resolve_alternates "$remote" <"$GIT_DIR/TMP_ALT" |
		while read alt
		do
		    case "$alt" in 'bad alternate: '*) die "$alt";; esac
		    echo >&2 "Getting alternate: $alt"
		    rsync -av --ignore-existing --exclude info \
		    "$alt" "$GIT_OBJECT_DIRECTORY/" || exit
		done
		rm -f "$GIT_DIR/TMP_ALT"
	    fi
	    rsync_slurped_objects=t
	}
	;;
    *)
	# We will do git native transport with just one call later.
	continue ;;
    esac

    append_fetch_head "$head" "$remote" "$remote_name" "$remote_nick" "$local_name"

done

case "$remote" in
http://* | https://* | rsync://* )
    ;; # we are already done.
*)
    (
	git-fetch-pack "$remote" $rref || echo failed "$remote"
    ) |
    while read sha1 remote_name
    do
	case "$sha1" in
	failed)
		echo >&2 "Fetch failure: $remote"
		exit 1 ;;
	esac
	found=
	single_force=
	for ref in $refs
	do
	    case "$ref" in
	    +$remote_name:*)
		single_force=t
		found="$ref"
		break ;;
	    $remote_name:*)
		found="$ref"
		break ;;
	    esac
	done

	local_name=$(expr "$found" : '[^:]*:\(.*\)')
	append_fetch_head "$sha1" "$remote" "$remote_name" "$remote_nick" "$local_name"
    done || exit
    ;;
esac

# If the original head was empty (i.e. no "master" yet), or
# if we were told not to worry, we do not have to check.
case ",$update_head_ok,$orig_head," in
*,, | t,* )
	;;
*)
	curr_head=$(cat "$GIT_DIR/HEAD" 2>/dev/null)
	if test "$curr_head" != "$orig_head"
	then
		echo "$orig_head" >$GIT_DIR/HEAD
		die "Cannot fetch into the current branch."
	fi
	;;
esac
