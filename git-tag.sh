#!/bin/sh
# Copyright (c) 2005 Linus Torvalds

USAGE='[-n [<num>]] -l [<pattern>] | [-a | -s | -u <key-id>] [-f | -d | -v] [-m <msg>] <tagname> [<head>]'
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
LINES=0
while case "$#" in 0) break ;; esac
do
    case "$1" in
    -a)
	annotate=1
	shift
	;;
    -s)
	annotate=1
	signed=1
	shift
	;;
    -f)
	force=1
	shift
	;;
    -n)
        case "$#,$2" in
	1,* | *,-*)
		LINES=1 	# no argument
		;;
	*)	shift
		LINES=$(expr "$1" : '\([0-9]*\)')
		[ -z "$LINES" ] && LINES=1 # 1 line is default when -n is used
		;;
	esac
	shift
	;;
    -l)
	list=1
	shift
	case $# in
	0)	PATTERN=
		;;
	*)
		PATTERN="$1"	# select tags by shell pattern, not re
		shift
		;;
	esac
	git rev-parse --symbolic --tags | sort |
	    while read TAG
	    do
	        case "$TAG" in
		*$PATTERN*) ;;
		*)	    continue ;;
		esac
		[ "$LINES" -le 0 ] && { echo "$TAG"; continue ;}
		OBJTYPE=$(git cat-file -t "$TAG")
		case $OBJTYPE in
		tag)
			ANNOTATION=$(git cat-file tag "$TAG" |
				sed -e '1,/^$/d' |
				sed -n -e "
					/^-----BEGIN PGP SIGNATURE-----\$/q
					2,\$s/^/    /
					p
					${LINES}q
				")
			printf "%-15s %s\n" "$TAG" "$ANNOTATION"
			;;
		*)      echo "$TAG"
			;;
		esac
	    done
	;;
    -m)
	annotate=1
	shift
	message="$1"
	if test "$#" = "0"; then
	    die "error: option -m needs an argument"
	else
	    message="$1"
	    message_given=1
	    shift
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
	    shift
	fi
	;;
    -u)
	annotate=1
	signed=1
	shift
	if test "$#" = "0"; then
	    die "error: option -u needs an argument"
	else
	    username="$1"
	    shift
	fi
	;;
    -d)
	shift
	had_error=0
	for tag
	do
		cur=$(git-show-ref --verify --hash -- "refs/tags/$tag") || {
			echo >&2 "Seriously, what tag are you talking about?"
			had_error=1
			continue
		}
		git-update-ref -m 'tag: delete' -d "refs/tags/$tag" "$cur" || {
			had_error=1
			continue
		}
		echo "Deleted tag $tag."
	done
	exit $had_error
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
done

[ -n "$list" ] && exit 0

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

test -n "$username" ||
	username=$(git-repo-config user.signingkey) ||
	username=$(expr "z$tagger" : 'z\(.*>\)')

trap 'rm -f "$GIT_DIR"/TAG_TMP* "$GIT_DIR"/TAG_FINALMSG "$GIT_DIR"/TAG_EDITMSG' 0

if [ "$annotate" ]; then
    if [ -z "$message_given" ]; then
        ( echo "#"
          echo "# Write a tag message"
          echo "#" ) > "$GIT_DIR"/TAG_EDITMSG
        ${VISUAL:-${EDITOR:-vi}} "$GIT_DIR"/TAG_EDITMSG || exit
    else
        printf '%s\n' "$message" >"$GIT_DIR"/TAG_EDITMSG
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
