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

test_expect_failure 'fsck fails' '

	git fsck
'

test_expect_success 'upload-pack fails due to error in pack-objects' '

	! echo "0032want $(git rev-parse HEAD)
00000009done
0000" | git-upload-pack . > /dev/null 2> output.err &&
	grep "pack-objects died" output.err
'

test_expect_success 'corrupt repo differently' '

	git hash-object -w file &&
	corrupt_repo HEAD^^{tree}

'

test_expect_failure 'fsck fails' '

	git fsck
'
test_expect_success 'upload-pack fails due to error in rev-list' '

	! echo "0032want $(git rev-parse HEAD)
00000009done
0000" | git-upload-pack . > /dev/null 2> output.err &&
	grep "waitpid (async) failed" output.err
'

test_expect_success 'create empty repository' '

	mkdir foo &&
	cd foo &&
	git init

'

test_expect_failure 'fetch fails' '

	git fetch .. master

'

test_done
