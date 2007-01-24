#!/bin/sh
#
# Emulates some cpio behavior using GNU tar

die() {
	echo >&2 "$@"
	exit 1
}

null=

while test $# -gt 0; do
	case "$1" in
	-0)	null=--null;;
	-o)	mode=o;;
	-iuv)	;;
	-pumd|-pumdl)
		mode=p
		dir="$2"
		shift
		;;
	*)	die "cpio emulation supports only -0, -o, -iuv, -pumdl";;
	esac
	shift
done

case $mode in
o)
	tar --create --file=- $null --files-from=-
	;;
p)
	tar --create --file=- $null --files-from=- |
	tar --extract --directory="$dir" --file=-
	;;
*)
	tar xvf -
esac
