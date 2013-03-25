#!/bin/sh

test_description='see how we handle various forms of corruption'
. ./test-lib.sh

# convert "1234abcd" to ".git/objects/12/34abcd"
obj_to_file() {
	echo "$(git rev-parse --git-dir)/objects/$(git rev-parse "$1" | sed 's,..,&/,')"
}

# Convert byte at offset "$2" of object "$1" into '\0'
corrupt_byte() {
	obj_file=$(obj_to_file "$1") &&
	chmod +w "$obj_file" &&
	printf '\0' | dd of="$obj_file" bs=1 seek="$2" conv=notrunc
}

test_expect_success 'setup corrupt repo' '
	git init bit-error &&
	(
		cd bit-error &&
		test_commit content &&
		corrupt_byte HEAD:content.t 10
	)
'

test_expect_success 'setup repo with missing object' '
	git init missing &&
	(
		cd missing &&
		test_commit content &&
		rm -f "$(obj_to_file HEAD:content.t)"
	)
'

test_expect_success 'streaming a corrupt blob fails' '
	(
		cd bit-error &&
		test_must_fail git cat-file blob HEAD:content.t
	)
'

test_expect_success 'read-tree -u detects bit-errors in blobs' '
	(
		cd bit-error &&
		rm -f content.t &&
		test_must_fail git read-tree --reset -u HEAD
	)
'

test_expect_success 'read-tree -u detects missing objects' '
	(
		cd missing &&
		rm -f content.t &&
		test_must_fail git read-tree --reset -u HEAD
	)
'

test_done
