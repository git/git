#!/bin/sh

die () {
	echo "$*" >&2
	exit 1
}

test $# = 4 ||
die "Usage: $0 <share> <username> <password> <mountpoint>"

mkdir -p "$4" || die "Could not create $4"

case "$(uname -s)" in
Linux)
	sudo mount -t cifs -o vers=3.0,username="$2",password="$3",dir_mode=0777,file_mode=0777,serverino "$1" "$4"
	;;
Darwin)
	pass="$(echo "$3" | sed -e 's/\//%2F/g' -e 's/+/%2B/g')" &&
	mount -t smbfs,soft "smb://$2:$pass@${1#//}" "$4"
	;;
*)
	die "No support for $(uname -s)"
	;;
esac ||
die "Could not mount $4"
