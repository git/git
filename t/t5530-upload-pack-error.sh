#!/bin/sh

test_description='errors in upload-pack'

. ./test-lib.sh

D=`pwd`

corrupt_repo () {
	object_sha1=$(git rev-parse "$1") &&
	ob=$(expr "$object_sha1" : "\(..\)") &&
	ject=$(expr "$object_sha1" : "..\(..*\)") &&
	rm -f ".git/objects/$ob/$ject"
}

test_expect_success 'setup and corrupt repository' '

	echo file >file &&
	git add file &&
	git rev-parse :file &&
	git commit -a -m original &&
	test_tick &&
	echo changed >file &&
	git commit -a -m changed &&
	corrupt_repo HEAD:file

'

test_expect_success 'fsck fails' '
	test_must_fail git fsck
'

test_expect_success 'upload-pack fails due to error in pack-objects packing' '

	printf "0032want %s\n00000009done\n0000" \
		$(git rev-parse HEAD) >input &&
	test_must_fail git upload-pack . <input >/dev/null 2>output.err &&
	grep "unable to read" output.err &&
	grep "pack-objects died" output.err
'

test_expect_success 'corrupt repo differently' '

	git hash-object -w file &&
	corrupt_repo HEAD^^{tree}

'

test_expect_success 'fsck fails' '
	test_must_fail git fsck
'
test_expect_success 'upload-pack fails due to error in rev-list' '

	printf "0032want %s\n0034shallow %s00000009done\n0000" \
		$(git rev-parse HEAD) $(git rev-parse HEAD^) >input &&
	test_must_fail git upload-pack . <input >/dev/null 2>output.err &&
	# pack-objects survived
	grep "Total.*, reused" output.err &&
	# but there was an error, which must have been in rev-list
	grep "bad tree object" output.err
'

test_expect_success 'upload-pack fails due to error in pack-objects enumeration' '

	printf "0032want %s\n00000009done\n0000" \
		$(git rev-parse HEAD) >input &&
	test_must_fail git upload-pack . <input >/dev/null 2>output.err &&
	grep "bad tree object" output.err &&
	grep "pack-objects died" output.err
'

test_expect_success 'create empty repository' '

	mkdir foo &&
	cd foo &&
	git init

'

test_expect_success 'fetch fails' '

	test_must_fail git fetch .. master

'

test_done
