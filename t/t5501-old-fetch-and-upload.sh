#!/bin/sh
#
# Copyright (c) 2005 Johannes Schindelin
#

# Test that the current fetch-pack/upload-pack plays nicely with
# an old counterpart

cd $(dirname $0) || exit 1
: ${SHELL_PATH=/bin/sh}

tmp=`pwd`/.tmp$$

retval=0

if [ -z "$1" ]; then
	list="fetch upload"
else
	list="$@"
fi

for i in $list; do
	case "$i" in
	fetch) pgm="old-git-fetch-pack"; replace="$pgm";;
	upload) pgm="old-git-upload-pack"; replace="git-fetch-pack --exec=$pgm";;
	both) pgm="old-git-upload-pack"; replace="old-git-fetch-pack --exec=$pgm";;
	esac

	if where=`LANG=C LC_ALL=C which "$pgm" 2>/dev/null` &&
	   case "$where" in
	   "no "*) (exit 1) ;;
	   esac
	then
		echo "Testing with $pgm"
		sed -e "s/git-fetch-pack/$replace/g" \
			-e "s/# old fails/warn/" < t5500-fetch-pack.sh > $tmp

		"$SHELL_PATH" "$tmp" || retval=$?
		rm -f "$tmp"

		test $retval != 0 && exit $retval
	else
		echo "Skipping test for $i, since I cannot find $pgm"
	fi
done

exit 0

