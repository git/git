# Compare a file containing rev-list bitmap traversal output to its non-bitmap
# counterpart. You can't just use test_cmp for this, because the two produce
# subtly different output:
#
#   - regular output is in traversal order, whereas bitmap is split by type,
#     with non-packed objects at the end
#
#   - regular output has a space and the pathname appended to non-commit
#     objects; bitmap output omits this
#
# This function normalizes and compares the two. The second file should
# always be the bitmap output.
test_bitmap_traversal () {
	if test "$1" = "--no-confirm-bitmaps"
	then
		shift
	elif cmp "$1" "$2"
	then
		echo >&2 "identical raw outputs; are you sure bitmaps were used?"
		return 1
	fi &&
	cut -d' ' -f1 "$1" | sort >"$1.normalized" &&
	sort "$2" >"$2.normalized" &&
	test_cmp "$1.normalized" "$2.normalized" &&
	rm -f "$1.normalized" "$2.normalized"
}
