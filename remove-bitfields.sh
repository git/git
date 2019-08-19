#!/bin/ape/sh
# Plan 9 C compiler rejects initialization a structure including bit field.
# usage: remove-bitfields.sh [dir ...]

if ! echo abc | sed 's/(ab)c/\1/' >/dev/null 2>&1
then
	alias sed='sed -E'
fi

trap 'rm -f /tmp/remove-bitfields.$pid; exit 1' 1 2 3 15 EXIT

files=$(du -a $* | awk '/\.[ch]$/ { print $2 }')
for i in $files
do
	sed '/(^[ 	]*\*|\?)/!s/([a-z]+[a-z0-9]*) *: *[0-9]+([,;])/\1\2/g' $i >/tmp/remove-bitfields.$pid
	cp /tmp/remove-bitfields.$pid $i
done
