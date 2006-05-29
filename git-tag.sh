#!/bin/sh
# Copyright (c) 2005 Linus Torvalds

USAGE='-l [<pattern>] | [-a | -s | -u <key-id>] [-f | -d] [-m <msg>] <tagname> [<head>]'
SUBDIRECTORY_OK='Yes'
. git-sh-setup

annotate=
signed=
force=
message=
username=
list=
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
	rm "$GIT_DIR/refs/tags/$tag_name" && \
	        echo "Deleted tag $tag_name."
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
if [ -e "$GIT_DIR/refs/tags/$name" -a -z "$force" ]; then
    die "tag '$name' already exists"
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
    if [ -z "$message" ]; then
        ( echo "#"
          echo "# Write a tag message"
          echo "#" ) > "$GIT_DIR"/TAG_EDITMSG
        ${VISUAL:-${EDITOR:-vi}} "$GIT_DIR"/TAG_EDITMSG || exit
    else
        echo "$message" >"$GIT_DIR"/TAG_EDITMSG
    fi

    grep -v '^#' <"$GIT_DIR"/TAG_EDITMSG |
    git-stripspace >"$GIT_DIR"/TAG_FINALMSG

    [ -s "$GIT_DIR"/TAG_FINALMSG ] || {
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

leading=`expr "refs/tags/$name" : '\(.*\)/'` &&
mkdir -p "$GIT_DIR/$leading" &&
echo $object > "$GIT_DIR/refs/tags/$name"
