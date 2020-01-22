#!/bin/sh

error () {
	echo >&2 "error: $@"
	exit 1
}

find_embedded_trash () {
	while read -r line
	do
		case "$line" in
		*Start\ of\ trash\ directory\ of\ \'t[0-9][0-9][0-9][0-9]-*\':*)
			test_name="${line#*\'}"
			test_name="${test_name%\'*}"

			return 0
		esac
	done

	return 1
}

extract_embedded_trash () {
	while read -r line
	do
		case "$line" in
		*End\ of\ trash\ directory\ of\ \'$test_name\'*)
			return
			;;
		*)
			printf '%s\n' "$line"
			;;
		esac
	done

	error "unexpected end of input"
}

# Raw logs from Linux build jobs have CRLF line endings, while OSX
# build jobs mostly have CRCRLF, except an odd line every now and
# then that has CRCRCRLF.  'base64 -d' from 'coreutils' doesn't like
# CRs and complains about "invalid input", so remove all CRs at the
# end of lines.
sed -e 's/\r*$//' | \
while find_embedded_trash
do
	echo "Extracting trash directory of '$test_name'"

	extract_embedded_trash |base64 -d |tar xzp
done
