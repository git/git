#!/bin/sh
#
# Performs an initial import of a directory. This is the equivalent
# of doing 'but init; but add .; but cummit'. It's a lot slower,
# but is meant to be a simple fast-import example.

if [ -z "$1" -o -z "$2" ]; then
	echo "usage: but-import branch import-message"
	exit 1
fi

USERNAME="$(but config user.name)"
EMAIL="$(but config user.email)"

if [ -z "$USERNAME" -o -z "$EMAIL" ]; then
	echo "You need to set user name and email"
	exit 1
fi

but init

(
	cat <<EOF
cummit refs/heads/$1
cummitter $USERNAME <$EMAIL> now
data <<MSGEOF
$2
MSGEOF

EOF
	find * -type f|while read i;do
		echo "M 100644 inline $i"
		echo data $(stat -c '%s' "$i")
		cat "$i"
		echo
	done
	echo
) | but fast-import --date-format=now
