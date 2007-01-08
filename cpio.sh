#!/bin/sh
#
# Emulates some cpio behavior using GNU tar

die() {
	echo >&2 "$@"
	exit 1
}

tr0=cat

while test $# -gt 0; do
	case "$1" in
	-0)	tr0="tr '\0' ' '";;
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
	files=.cpiofiles$$
	$tr0 > $files
	tar --create --file=- --files-from=$files --exclude=$files
	rc=$?
	rm -f $files
	exit $rc
	;;
p)
	files=.cpiofiles$$
	$tr0 > $files
	tar --create --file=- --files-from=$files --exclude=$files |
	tar --extract --directory="$dir" --file=-
	rm -f $files
	;;
*)
	tar xvf - || exit
esac
