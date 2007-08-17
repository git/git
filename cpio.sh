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

prev=

filterdirs() {
	while read f; do
		if test -d "$f"; then
			# list only empty directories
			# here we assume that directories are listed after
			# its files (aka 'find -depth'), hence, a directory
			# that is not empty will be a leading sub-string
			# of the preceding entry
			case "$prev" in
			"$f"/* ) ;;
			*) echo "$f";;
			esac
		else
			echo "$f"
		fi
		prev="$f"
	done
}

case $mode in
o)
	tar --create --file=- $null --files-from=-
	;;
p)
	test -z "$null" || die "cpio: cannot use -0 in pass-through mode"
	filterdirs |
	tar --create --file=- --files-from=- |
	tar --extract --directory="$dir" --file=-
	;;
*)
	tar xvf -
esac
