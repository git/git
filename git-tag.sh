#!/bin/sh
# Copyright (c) 2005 Linus Torvalds

USAGE='-l [<pattern>] | [-a | -s | -u <key-id>] [-f | -d | -v] [-m <msg>] <tagname> [<head>]'
SUBDIRECTORY_OK='Yes'
. git-sh-setup

message_given=
annotate=
signed=
force=
message=
username=
list=
verify=
while case "$#" in 0) break ;; esac
do
    case "$1" in
    -a)
	annotate=1
	;;
    -s)
	annotate=1
	signed=1
	;;
    -f)
	force=1
	;;
    -l)
	case "$#" in
	1)
		set x . ;;
	esac
	shift
	git rev-parse --symbolic --tags | sort | grep "$@"
	exit $?
	;;
    -m)
    	annotate=1
	shift
	message="$1"
	if test "$#" = "0"; then
	    die "error: option -m needs an argument"
	else
	    message_given=1
	fi
	;;
    -F)
	annotate=1
	shift
	if test "$#" = "0"; then
	    die "error: option -F needs an argument"
	else
	    message="$(cat "$1")"
	    message_given=1
	fi
	;;
    -u)
	annotate=1
	signed=1
	shift
	username="$1"
	;;
    -d)
    	shift
	tag_name="$1"
	tag=$(git-show-ref --verify --hash -- "refs/tags/$tag_name") ||
		die "Seriously, what tag are you talking about?"
	git-update-ref -m 'tag: delete' -d "refs/tags/$tag_name" "$tag" &&
		echo "Deleted tag $tag_name."
	exit $?
	;;
    -v)
	shift
	tag_name="$1"
	tag=$(git-show-ref --verify --hash -- "refs/tags/$tag_name") ||
		die "Seriously, what tag are you talking about?"
	git-verify-tag -v "$tag"
	exit $?
	;;
    -*)
        usage
	;;
    *)
	break
	;;
    esac
    shift
done

name="$1"
[ "$name" ] || usage
prev=0000000000000000000000000000000000000000
if git-show-ref --verify --quiet -- "refs/tags/$name"
then
    test -n "$force" || die "tag '$name' already exists"
    prev=`git rev-parse "refs/tags/$name"`
fi
shift
git-check-ref-format "tags/$name" ||
	die "we do not like '$name' as a tag name."

object=$(git-rev-parse --verify --default HEAD "$@") || exit 1
type=$(git-cat-file -t $object) || exit 1
tagger=$(git-var GIT_COMMITTER_IDENT) || exit 1
: ${username:=$(expr "z$tagger" : 'z\(.*>\)')}

trap 'rm -f "$GIT_DIR"/TAG_TMP* "$GIT_DIR"/TAG_FINALMSG "$GIT_DIR"/TAG_EDITMSG' 0

if [ "$annotate" ]; then
    if [ -z "$message_given" ]; then
        ( echo "#"
          echo "# Write a tag message"
          echo "#" ) > "$GIT_DIR"/TAG_EDITMSG
        ${VISUAL:-${EDITOR:-vi}} "$GIT_DIR"/TAG_EDITMSG || exit
    else
        echo "$message" >"$GIT_DIR"/TAG_EDITMSG
    fi

    grep -v '^#' <"$GIT_DIR"/TAG_EDITMSG |
    git-stripspace >"$GIT_DIR"/TAG_FINALMSG

    [ -s "$GIT_DIR"/TAG_FINALMSG -o -n "$message_given" ] || {
	echo >&2 "No tag message?"
	exit 1
    }

    ( printf 'object %s\ntype %s\ntag %s\ntagger %s\n\n' \
	"$object" "$type" "$name" "$tagger";
      cat "$GIT_DIR"/TAG_FINALMSG ) >"$GIT_DIR"/TAG_TMP
    rm -f "$GIT_DIR"/TAG_TMP.asc "$GIT_DIR"/TAG_FINALMSG
    if [ "$signed" ]; then
	gpg -bsa -u "$username" "$GIT_DIR"/TAG_TMP &&
	cat "$GIT_DIR"/TAG_TMP.asc >>"$GIT_DIR"/TAG_TMP ||
	die "failed to sign the tag with GPG."
    fi
    object=$(git-mktag < "$GIT_DIR"/TAG_TMP)
fi

git update-ref "refs/tags/$name" "$object" "$prev"

