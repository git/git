#!/bin/sh
#
# Performs an initial import of a directory. This is the equivalent
# of doing 'git init; git add .; git commit'. It's a lot slower,
# but is meant to be a simple fast-import example.

if [ -z "$1" -o -z "$2" ]; then
	echo "Usage: git-import branch import-message"
	exit 1
fi

USERNAME="$(git config user.name)"
EMAIL="$(git config user.email)"

if [ -z "$USERNAME" -o -z "$EMAIL" ]; then
	echo "You need to set user name and email"
	exit 1
fi

git init

(
	cat <<EOF
commit refs/heads/$1
committer $USERNAME <$EMAIL> now
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
) | git fast-import --date-format=now
