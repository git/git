#!/bin/sh
#
# Copyright (c) 2005, Linus Torvalds
# Copyright (c) 2005, Junio C Hamano
# 
# Clone a repository into a different directory that does not yet exist.

# See git-sh-setup why.
unset CDPATH

die() {
	echo >&2 "$@"
	exit 1
}

usage() {
	die "Usage: $0 [--template=<template_directory>] [--reference <reference-repo>] [--bare] [-l [-s]] [-q] [-u <upload-pack>] [--origin <name>] [--depth <n>] [-n] <repo> [<dir>]"
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
	clone_tmp="$GIT_DIR/clone-tmp" &&
	mkdir -p "$clone_tmp" || exit 1
	if [ -n "$GIT_CURL_FTP_NO_EPSV" -o \
		"`git-config --bool http.noEPSV`" = true ]; then
		curl_extra_args="${curl_extra_args} --disable-epsv"
	fi
	http_fetch "$1/info/refs" "$clone_tmp/refs" ||
		die "Cannot get remote repository information.
Perhaps git-update-server-info needs to be run there?"
	while read sha1 refname
	do
		name=`expr "z$refname" : 'zrefs/\(.*\)'` &&
		case "$name" in
		*^*)	continue;;
		esac
		case "$bare,$name" in
		yes,* | ,heads/* | ,tags/*) ;;
		*)	continue ;;
		esac
		if test -n "$use_separate_remote" &&
		   branch_name=`expr "z$name" : 'zheads/\(.*\)'`
		then
			tname="remotes/$origin/$branch_name"
		else
			tname=$name
		fi
		git-http-fetch -v -a -w "$tname" "$name" "$1/" || exit 1
	done <"$clone_tmp/refs"
	rm -fr "$clone_tmp"
	http_fetch "$1/HEAD" "$GIT_DIR/REMOTE_HEAD" ||
	rm -f "$GIT_DIR/REMOTE_HEAD"
}

quiet=
local=no
use_local=no
local_shared=no
unset template
no_checkout=
upload_pack=
bare=
reference=
origin=
origin_override=
use_separate_remote=t
depth=
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
	1,--template) usage ;;
	*,--template)
		shift; template="--template=$1" ;;
	*,--template=*)
	  template="$1" ;;
	*,-q|*,--quiet) quiet=-q ;;
	*,--use-separate-remote) ;;
	*,--no-separate-remote)
		die "clones are always made with separate-remote layout" ;;
	1,--reference) usage ;;
	*,--reference)
		shift; reference="$1" ;;
	*,--reference=*)
		reference=`expr "z$1" : 'z--reference=\(.*\)'` ;;
	*,-o|*,--or|*,--ori|*,--orig|*,--origi|*,--origin)
		case "$2" in
		'')
		    usage ;;
		*/*)
		    die "'$2' is not suitable for an origin name"
		esac
		git-check-ref-format "heads/$2" ||
		    die "'$2' is not suitable for a branch name"
		test -z "$origin_override" ||
		    die "Do not give more than one --origin options."
		origin_override=yes
		origin="$2"; shift
		;;
	1,-u|1,--upload-pack) usage ;;
	*,-u|*,--upload-pack)
		shift
		upload_pack="--upload-pack=$1" ;;
	*,--upload-pack=*)
		upload_pack=--upload-pack=$(expr "z$1" : 'z-[^=]*=\(.*\)') ;;
	1,--depth) usage;;
	*,--depth)
		shift
		depth="--depth=$1";;
	*,-*) usage ;;
	*) break ;;
	esac
do
	shift
done

repo="$1"
test -n "$repo" ||
    die 'you must specify a repository to clone.'

# --bare implies --no-checkout and --no-separate-remote
if test yes = "$bare"
then
	if test yes = "$origin_override"
	then
		die '--bare and --origin $origin options are incompatible.'
	fi
	no_checkout=yes
	use_separate_remote=
fi

if test -z "$origin"
then
	origin=origin
fi

# Turn the source into an absolute path if
# it is local
if base=$(get_repo_base "$repo"); then
	repo="$base"
	local=yes
fi

dir="$2"
# Try using "humanish" part of source repo if user didn't specify one
[ -z "$dir" ] && dir=$(echo "$repo" | sed -e 's|/$||' -e 's|:*/*\.git$||' -e 's|.*[/:]||g')
[ -e "$dir" ] && die "destination directory '$dir' already exists."
mkdir -p "$dir" &&
D=$(cd "$dir" && pwd) &&
trap 'err=$?; cd ..; rm -rf "$D"; exit $err' 0
case "$bare" in
yes)
	GIT_DIR="$D" ;;
*)
	GIT_DIR="$D/.git" ;;
esac && export GIT_DIR && git-init ${template+"$template"} || usage

if test -n "$reference"
then
	ref_git=
	if test -d "$reference"
	then
		if test -d "$reference/.git/objects"
		then
			ref_git="$reference/.git"
		elif test -d "$reference/objects"
		then
			ref_git="$reference"
		fi
	fi
	if test -n "$ref_git"
	then
		ref_git=$(cd "$ref_git" && pwd)
		echo "$ref_git/objects" >"$GIT_DIR/objects/info/alternates"
		(
			GIT_DIR="$ref_git" git for-each-ref \
				--format='%(objectname) %(*objectname)'
		) |
		while read a b
		do
			test -z "$a" ||
			git update-ref "refs/reference-tmp/$a" "$a"
			test -z "$b" ||
			git update-ref "refs/reference-tmp/$b" "$b"
		done
	else
		die "reference repository '$reference' is not a local directory."
	fi
fi

rm -f "$GIT_DIR/CLONE_HEAD"

# We do local magic only when the user tells us to.
case "$local,$use_local" in
yes,yes)
	( cd "$repo/objects" ) ||
		die "-l flag seen but repository '$repo' is not local."

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
	    find objects -depth -print | cpio -pumd$l "$GIT_DIR/" || exit 1
	    ;;
	yes)
	    mkdir -p "$GIT_DIR/objects/info"
	    echo "$repo/objects" >> "$GIT_DIR/objects/info/alternates"
	    ;;
	esac
	git-ls-remote "$repo" >"$GIT_DIR/CLONE_HEAD" || exit 1
	;;
*)
	case "$repo" in
	rsync://*)
		case "$depth" in
		"") ;;
		*) die "shallow over rsync not supported" ;;
		esac
		rsync $quiet -av --ignore-existing  \
			--exclude info "$repo/objects/" "$GIT_DIR/objects/" ||
		exit
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
		git-ls-remote "$repo" >"$GIT_DIR/CLONE_HEAD" || exit 1
		;;
	https://*|http://*|ftp://*)
		case "$depth" in
		"") ;;
		*) die "shallow over http or ftp not supported" ;;
		esac
		if test -z "@@NO_CURL@@"
		then
			clone_dumb_http "$repo" "$D"
		else
			die "http transport not supported, rebuild Git with curl support"
		fi
		;;
	*)
		case "$upload_pack" in
		'') git-fetch-pack --all -k $quiet $depth "$repo" ;;
		*) git-fetch-pack --all -k $quiet "$upload_pack" $depth "$repo" ;;
		esac >"$GIT_DIR/CLONE_HEAD" ||
			die "fetch-pack from '$repo' failed."
		;;
	esac
	;;
esac
test -d "$GIT_DIR/refs/reference-tmp" && rm -fr "$GIT_DIR/refs/reference-tmp"

if test -f "$GIT_DIR/CLONE_HEAD"
then
	# Read git-fetch-pack -k output and store the remote branches.
	if [ -n "$use_separate_remote" ]
	then
		branch_top="remotes/$origin"
	else
		branch_top="heads"
	fi
	tag_top="tags"
	while read sha1 name
	do
		case "$name" in
		*'^{}')
			continue ;;
		HEAD)
			destname="REMOTE_HEAD" ;;
		refs/heads/*)
			destname="refs/$branch_top/${name#refs/heads/}" ;;
		refs/tags/*)
			destname="refs/$tag_top/${name#refs/tags/}" ;;
		*)
			continue ;;
		esac
		git-update-ref -m "clone: from $repo" "$destname" "$sha1" ""
	done < "$GIT_DIR/CLONE_HEAD"
fi

cd "$D" || exit

if test -z "$bare" && test -f "$GIT_DIR/REMOTE_HEAD"
then
	# a non-bare repository is always in separate-remote layout
	remote_top="refs/remotes/$origin"
	head_sha1=`cat "$GIT_DIR/REMOTE_HEAD"`
	case "$head_sha1" in
	'ref: refs/'*)
		# Uh-oh, the remote told us (http transport done against
		# new style repository with a symref HEAD).
		# Ideally we should skip the guesswork but for now
		# opt for minimum change.
		head_sha1=`expr "z$head_sha1" : 'zref: refs/heads/\(.*\)'`
		head_sha1=`cat "$GIT_DIR/$remote_top/$head_sha1"`
		;;
	esac

	# The name under $remote_top the remote HEAD seems to point at.
	head_points_at=$(
		(
			test -f "$GIT_DIR/$remote_top/master" && echo "master"
			cd "$GIT_DIR/$remote_top" &&
			find . -type f -print | sed -e 's/^\.\///'
		) | (
		done=f
		while read name
		do
			test t = $done && continue
			branch_tip=`cat "$GIT_DIR/$remote_top/$name"`
			if test "$head_sha1" = "$branch_tip"
			then
				echo "$name"
				done=t
			fi
		done
		)
	)

	# Write out remote.$origin config, and update our "$head_points_at".
	case "$head_points_at" in
	?*)
		# Local default branch
		git-symbolic-ref HEAD "refs/heads/$head_points_at" &&

		# Tracking branch for the primary branch at the remote.
		origin_track="$remote_top/$head_points_at" &&
		git-update-ref HEAD "$head_sha1" &&

		# Upstream URL
		git-config remote."$origin".url "$repo" &&

		# Set up the mappings to track the remote branches.
		git-config remote."$origin".fetch \
			"+refs/heads/*:$remote_top/*" '^$' &&
		rm -f "refs/remotes/$origin/HEAD"
		git-symbolic-ref "refs/remotes/$origin/HEAD" \
			"refs/remotes/$origin/$head_points_at" &&

		git-config branch."$head_points_at".remote "$origin" &&
		git-config branch."$head_points_at".merge "refs/heads/$head_points_at"
	esac

	case "$no_checkout" in
	'')
		test "z$quiet" = z && v=-v || v=
		git-read-tree -m -u $v HEAD HEAD
	esac
fi
rm -f "$GIT_DIR/CLONE_HEAD" "$GIT_DIR/REMOTE_HEAD"

trap - 0

