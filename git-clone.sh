#!/bin/sh
#
# Copyright (c) 2005, Linus Torvalds
# Copyright (c) 2005, Junio C Hamano
# 
# Clone a repository into a different directory that does not yet exist.

# See git-sh-setup why.
unset CDPATH

usage() {
	echo >&2 "Usage: $0 [--bare] [-l [-s]] [-q] [-u <upload-pack>] [-o <name>] [-n] <repo> [<dir>]"
	exit 1
}

get_repo_base() {
	(cd "$1" && (cd .git ; pwd)) 2> /dev/null
}

if [ -n "$GIT_SSL_NO_VERIFY" ]; then
    curl_extra_args="-k"
fi

http_fetch () {
	# $1 = Remote, $2 = Local
	curl -nsfL $curl_extra_args "$1" >"$2"
}

clone_dumb_http () {
	# $1 - remote, $2 - local
	cd "$2" &&
	clone_tmp='.git/clone-tmp' &&
	mkdir -p "$clone_tmp" || exit 1
	http_fetch "$1/info/refs" "$clone_tmp/refs" || {
		echo >&2 "Cannot get remote repository information.
Perhaps git-update-server-info needs to be run there?"
		exit 1;
	}
	while read sha1 refname
	do
		name=`expr "$refname" : 'refs/\(.*\)'` &&
		case "$name" in
		*^*)	;;
		*)
			git-http-fetch -v -a -w "$name" "$name" "$1/" || exit 1
		esac
	done <"$clone_tmp/refs"
	rm -fr "$clone_tmp"
}

quiet=
use_local=no
local_shared=no
no_checkout=
upload_pack=
bare=
origin=origin
while
	case "$#,$1" in
	0,*) break ;;
	*,-n|*,--no|*,--no-|*,--no-c|*,--no-ch|*,--no-che|*,--no-chec|\
	*,--no-check|*,--no-checko|*,--no-checkou|*,--no-checkout)
	  no_checkout=yes ;;
	*,--na|*,--nak|*,--nake|*,--naked|\
	*,-b|*,--b|*,--ba|*,--bar|*,--bare) bare=yes ;;
	*,-l|*,--l|*,--lo|*,--loc|*,--loca|*,--local) use_local=yes ;;
        *,-s|*,--s|*,--sh|*,--sha|*,--shar|*,--share|*,--shared) 
          local_shared=yes; use_local=yes ;;
	*,-q|*,--quiet) quiet=-q ;;
	1,-o) usage;;
	*,-o)
		git-check-ref-format "$2" || {
		    echo >&2 "'$2' is not suitable for a branch name"
		    exit 1
		}
		origin="$2"; shift
		;;
	1,-u|1,--upload-pack) usage ;;
	*,-u|*,--upload-pack)
		shift
		upload_pack="--exec=$1" ;;
	*,-*) usage ;;
	*) break ;;
	esac
do
	shift
done

# --bare implies --no-checkout
test =z "$bare" || no_checkout=yes

# Turn the source into an absolute path if
# it is local
repo="$1"
local=no
if base=$(get_repo_base "$repo"); then
	repo="$base"
	local=yes
fi

dir="$2"
# Try using "humanish" part of source repo if user didn't specify one
[ -z "$dir" ] && dir=$(echo "$repo" | sed -e 's|/$||' -e 's|:*/*\.git$||' -e 's|.*[/:]||g')
[ -e "$dir" ] && echo "$dir already exists." && usage
mkdir -p "$dir" &&
D=$(cd "$dir" && pwd) &&
case "$bare" in
yes) GIT_DIR="$D" ;;
*) GIT_DIR="$D/.git" ;;
esac && export GIT_DIR && git-init-db || usage
case "$bare" in
yes)
	GIT_DIR="$D" ;;
*)
	GIT_DIR="$D/.git" ;;
esac

# We do local magic only when the user tells us to.
case "$local,$use_local" in
yes,yes)
	( cd "$repo/objects" ) || {
		echo >&2 "-l flag seen but $repo is not local."
		exit 1
	}

	case "$local_shared" in
	no)
	    # See if we can hardlink and drop "l" if not.
	    sample_file=$(cd "$repo" && \
			  find objects -type f -print | sed -e 1q)

	    # objects directory should not be empty since we are cloning!
	    test -f "$repo/$sample_file" || exit

	    l=
	    if ln "$repo/$sample_file" "$GIT_DIR/objects/sample" 2>/dev/null
	    then
		    l=l
	    fi &&
	    rm -f "$GIT_DIR/objects/sample" &&
	    cd "$repo" &&
	    find objects -depth -print | cpio -puamd$l "$GIT_DIR/" || exit 1
	    ;;
	yes)
	    mkdir -p "$GIT_DIR/objects/info"
	    {
		test -f "$repo/objects/info/alternates" &&
		cat "$repo/objects/info/alternates";
		echo "$repo/objects"
	    } >"$GIT_DIR/objects/info/alternates"
	    ;;
	esac

	# Make a duplicate of refs and HEAD pointer
	HEAD=
	if test -f "$repo/HEAD"
	then
		HEAD=HEAD
	fi
	(cd "$repo" && tar cf - refs $HEAD) |
	(cd "$GIT_DIR" && tar xf -) || exit 1
	;;
*)
	case "$repo" in
	rsync://*)
		rsync $quiet -av --ignore-existing  \
			--exclude info "$repo/objects/" "$GIT_DIR/objects/" &&
		rsync $quiet -av --ignore-existing  \
			--exclude info "$repo/refs/" "$GIT_DIR/refs/" || exit

		# Look at objects/info/alternates for rsync -- http will
		# support it natively and git native ones will do it on the
		# remote end.  Not having that file is not a crime.
		rsync -q "$repo/objects/info/alternates" \
			"$GIT_DIR/TMP_ALT" 2>/dev/null ||
			rm -f "$GIT_DIR/TMP_ALT"
		if test -f "$GIT_DIR/TMP_ALT"
		then
		    ( cd "$D" &&
		      . git-parse-remote &&
		      resolve_alternates "$repo" <"$GIT_DIR/TMP_ALT" ) |
		    while read alt
		    do
			case "$alt" in 'bad alternate: '*) die "$alt";; esac
			case "$quiet" in
			'')	echo >&2 "Getting alternate: $alt" ;;
			esac
			rsync $quiet -av --ignore-existing  \
			    --exclude info "$alt" "$GIT_DIR/objects" || exit
		    done
		    rm -f "$GIT_DIR/TMP_ALT"
		fi
		;;
	http://*)
		clone_dumb_http "$repo" "$D"
		;;
	*)
		cd "$D" && case "$upload_pack" in
		'') git-clone-pack $quiet "$repo" ;;
		*) git-clone-pack $quiet "$upload_pack" "$repo" ;;
		esac || {
			echo >&2 "clone-pack from '$repo' failed."
			exit 1
		}
		;;
	esac
	;;
esac

cd "$D" || exit

if test -f "$GIT_DIR/HEAD" && test -z "$bare"
then
	head_points_at=`git-symbolic-ref HEAD`
	case "$head_points_at" in
	refs/heads/*)
		head_points_at=`expr "$head_points_at" : 'refs/heads/\(.*\)'`
		mkdir -p "$GIT_DIR/remotes" &&
		echo >"$GIT_DIR/remotes/origin" \
		"URL: $repo
Pull: $head_points_at:$origin" &&
		git-update-ref "refs/heads/$origin" $(git-rev-parse HEAD) &&
		(cd "$GIT_DIR" && find "refs/heads" -type f -print) |
		while read ref
		do
			head=`expr "$ref" : 'refs/heads/\(.*\)'` &&
			test "$head_points_at" = "$head" ||
			test "$origin" = "$head" ||
			echo "Pull: ${head}:${head}"
		done >>"$GIT_DIR/remotes/origin"
	esac

	case "$no_checkout" in
	'')
		git checkout
	esac
fi
