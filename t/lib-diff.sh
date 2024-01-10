. "$TEST_DIRECTORY"/lib-diff-data.sh

:

sanitize_diff_raw='/^:/s/ '"\($OID_REGEX\)"' '"\($OID_REGEX\)"' \([A-Z]\)[0-9]*	/ \1 \2 \3#	/'
compare_diff_raw () {
    # When heuristics are improved, the score numbers would change.
    # Ignore them while comparing.
    # Also we do not check SHA1 hash generation in this test, which
    # is a job for t0000-basic.sh

    sed -e "$sanitize_diff_raw" <"$1" >.tmp-1
    sed -e "$sanitize_diff_raw" <"$2" >.tmp-2
    test_cmp .tmp-1 .tmp-2 && rm -f .tmp-1 .tmp-2
}

sanitize_diff_raw_z='/^:/s/ '"$OID_REGEX"' '"$OID_REGEX"' \([A-Z]\)[0-9]*$/ X X \1#/'
compare_diff_raw_z () {
    # When heuristics are improved, the score numbers would change.
    # Ignore them while comparing.
    # Also we do not check SHA1 hash generation in this test, which
    # is a job for t0000-basic.sh

    perl -pe 'y/\000/\012/' <"$1" | sed -e "$sanitize_diff_raw_z" >.tmp-1
    perl -pe 'y/\000/\012/' <"$2" | sed -e "$sanitize_diff_raw_z" >.tmp-2
    test_cmp .tmp-1 .tmp-2 && rm -f .tmp-1 .tmp-2
}

compare_diff_patch () {
    # When heuristics are improved, the score numbers would change.
    # Ignore them while comparing.
    sed -e '
	/^[dis]*imilarity index [0-9]*%$/d
	/^index [0-9a-f]*\.\.[0-9a-f]/d
    ' <"$1" >.tmp-1
    sed -e '
	/^[dis]*imilarity index [0-9]*%$/d
	/^index [0-9a-f]*\.\.[0-9a-f]/d
    ' <"$2" >.tmp-2
    test_cmp .tmp-1 .tmp-2 && rm -f .tmp-1 .tmp-2
}
