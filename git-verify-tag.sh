#!/bin/sh

USAGE='<tag>'
SUBDIRECTORY_OK='Yes'
. git-sh-setup

verbose=
while case $# in 0) break;; esac
do
	case "$1" in
	-v|--v|--ve|--ver|--verb|--verbo|--verbos|--verbose)
		verbose=t ;;
	*)
		break ;;
	esac
	shift
done

if [ "$#" != "1" ]
then
	usage
fi

type="$(git-cat-file -t "$1" 2>/dev/null)" ||
	die "$1: no such object."

test "$type" = tag ||
	die "$1: cannot verify a non-tag object of type $type."

case "$verbose" in
t)
	git-cat-file -p "$1" |
	sed -n -e '/^-----BEGIN PGP SIGNATURE-----/q' -e p
	;;
esac

trap 'rm -f "$GIT_DIR/.tmp-vtag"' 0

git-cat-file tag "$1" >"$GIT_DIR/.tmp-vtag" || exit 1

cat "$GIT_DIR/.tmp-vtag" |
sed '/-----BEGIN PGP/Q' |
gpg --verify "$GIT_DIR/.tmp-vtag" - || exit 1
rm -f "$GIT_DIR/.tmp-vtag"

