#!/bin/sh
# Copyright (c) 2005 Linus Torvalds

. git-sh-setup || die "Not a git archive"

usage () {
    echo >&2 "Usage: git-tag [-a | -s] [-f] [-m "tag message"] tagname [head]"
    exit 1
}

annotate=
signed=
force=
message=
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
    -m)
    	annotate=1
	shift
	message="$1"
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

object=$(git-rev-parse --verify --default HEAD "$@") || exit 1
type=$(git-cat-file -t $object) || exit 1
tagger=$(git-var GIT_COMMITTER_IDENT) || exit 1

trap 'rm -f .tmp-tag* .tagmsg .editmsg' 0

if [ "$annotate" ]; then
    if [ -z "$message" ]; then
        ( echo "#"
          echo "# Write a tag message"
          echo "#" ) > .editmsg
        ${VISUAL:-${EDITOR:-vi}} .editmsg || exit
    else
        echo "$message" > .editmsg
    fi

    grep -v '^#' < .editmsg | git-stripspace > .tagmsg

    [ -s .tagmsg ] || exit

    ( echo -e "object $object\ntype $type\ntag $name\ntagger $tagger\n"; cat .tagmsg ) > .tmp-tag
    rm -f .tmp-tag.asc .tagmsg
    if [ "$signed" ]; then
	me=$(expr "$tagger" : '\(.*>\)') &&
	gpg -bsa -u "$me" .tmp-tag &&
	cat .tmp-tag.asc >>.tmp-tag ||
	die "failed to sign the tag with GPG."
    fi
    object=$(git-mktag < .tmp-tag)
fi

mkdir -p "$GIT_DIR/refs/tags"
echo $object > "$GIT_DIR/refs/tags/$name"
