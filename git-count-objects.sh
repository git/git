#!/bin/sh
#
# Copyright (c) 2005 Junio C Hamano
#

. git-sh-setup

dc </dev/null 2>/dev/null || {
	# This is not a real DC at all -- it just knows how
	# this script feeds DC and does the computation itself.
	dc () {
		while read a b
		do
			case $a,$b in
			0,)	acc=0 ;;
			*,+)	acc=$(($acc + $a)) ;;
			p,)	echo "$acc" ;;
			esac
		done
	}
}

echo $(find "$GIT_DIR/objects"/?? -type f -print 2>/dev/null | wc -l) objects, \
$({
    echo 0
    # "no-such" is to help Darwin folks by not using xargs -r.
    find "$GIT_DIR/objects"/?? -type f -print 2>/dev/null |
    xargs du -k "$GIT_DIR/objects/no-such" 2>/dev/null |
    sed -e 's/[ 	].*/ +/'
    echo p
} | dc) kilobytes
