# Shell library for working with "chunk" files (commit-graph, midx, etc).

# corrupt_chunk_file <fn> <chunk> <offset> <bytes>
#
# Corrupt a chunk-based file (like a commit-graph) by overwriting the bytes
# found in the chunk specified by the 4-byte <chunk> identifier. If <offset> is
# "clear", replace the chunk entirely. Otherwise, overwrite data <offset> bytes
# into the chunk.
#
# The <bytes> are interpreted as pairs of hex digits (so "000000FE" would be
# big-endian 254).
corrupt_chunk_file () {
	fn=$1; shift
	perl "$TEST_DIRECTORY"/lib-chunk/corrupt-chunk-file.pl \
		"$@" <"$fn" >"$fn.tmp" &&
	# some vintages of macOS 'mv' fails to overwrite a read-only file.
	mv -f "$fn.tmp" "$fn"
}
