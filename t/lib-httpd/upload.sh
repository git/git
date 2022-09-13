#!/bin/sh

# In part from http://codereview.stackexchange.com/questions/79549/bash-cgi-upload-file
# Used in the httpd test server to for a remote helper to call to upload blobs.

FILES_DIR="www/files"

OLDIFS="$IFS"
IFS='&'
set -- $QUERY_STRING
IFS="$OLDIFS"

while test $# -gt 0
do
	key=${1%%=*}
	val=${1#*=}

	case "$key" in
	"oid") oid="$val" ;;
	"type") type="$val" ;;
	"size") size="$val" ;;
	"delete") delete=1 ;;
	*) echo >&2 "unknown key '$key'" ;;
	esac

	shift
done

case "$REQUEST_METHOD" in
POST)
	if test "$delete" = "1"
	then
		rm -f "$FILES_DIR/$oid-$size-$type"
	else
		mkdir -p "$FILES_DIR"
		cat >"$FILES_DIR/$oid-$size-$type"
	fi

	echo 'Status: 204 No Content'
	echo
	;;

*)
	echo 'Status: 405 Method Not Allowed'
	echo
esac
