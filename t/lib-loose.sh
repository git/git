# Support routines for hand-crafting loose objects.

# Write a loose object into the odb at $1, with object type $2 and contents
# from stdin. Writes the oid to stdout. Example:
#
#   oid=$(echo foo | loose_obj .git/objects blob)
#
# If $3 is given, it is written as the size in the object header instead
# of the real size of the contents.
#
loose_obj () {
	cat >tmp_loose.content &&
	size=${3:-$(wc -c <tmp_loose.content)} &&
	{
		# Do not quote $size here; we want the shell
		# to strip whitespace that "wc" adds on some platforms.
		printf "%s %s\0" "$2" $size &&
		cat tmp_loose.content
	} >tmp_loose.raw &&

	oid=$(test-tool $test_hash_algo <tmp_loose.raw) &&
	suffix=${oid#??} &&
	prefix=${oid%$suffix} &&
	dir=$1/$prefix &&
	file=$dir/$suffix &&

	test-tool zlib deflate <tmp_loose.raw >tmp_loose.zlib &&
	mkdir -p "$dir" &&
	mv tmp_loose.zlib "$file" &&

	rm tmp_loose.raw tmp_loose.content &&
	echo "$oid"
}
