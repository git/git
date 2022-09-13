#!/bin/sh

# Used in the httpd test server to be called by a remote helper to list objects.

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
	*) echo >&2 "unknown key '$key'" ;;
	esac

	shift
done

if test -d "$FILES_DIR"
then
	if test -z "$oid"
	then
		echo 'Status: 200 OK'
		echo
		ls "$FILES_DIR" | tr '-' ' '
	else
		if test -f "$FILES_DIR/$oid"-*
		then
			echo 'Status: 200 OK'
			echo
			cat "$FILES_DIR/$oid"-*
		else
			echo 'Status: 404 Not Found'
			echo
		fi
	fi
fi
