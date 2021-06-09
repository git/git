# Helps shared by the test scripts for comparing log graphs.

sanitize_log_output () {
	sed -e 's/ *$//' \
	    -e 's/commit [0-9a-f]*$/commit COMMIT_OBJECT_NAME/' \
	    -e 's/Merge: [ 0-9a-f]*$/Merge: MERGE_PARENTS/' \
	    -e 's/Merge tag.*/Merge HEADS DESCRIPTION/' \
	    -e 's/Merge commit.*/Merge HEADS DESCRIPTION/' \
	    -e 's/index [0-9a-f]*\.\.[0-9a-f]*/index BEFORE..AFTER/'
}

lib_test_cmp_graph () {
	git log --graph "$@" >output &&
	sed 's/ *$//' >output.sanitized <output &&
	test_cmp expect output.sanitized
}

lib_test_cmp_short_graph () {
	git log --graph --pretty=short "$@" >output &&
	sanitize_log_output >output.sanitized <output &&
	test_cmp expect output.sanitized
}

lib_test_cmp_colored_graph () {
	git log --graph --color=always "$@" >output.colors.raw &&
	test_decode_color <output.colors.raw | sed "s/ *\$//" >output.colors &&
	test_cmp expect.colors output.colors
}
