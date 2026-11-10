#!/bin/sh

test_description='phantom symlink through an existing junction'
MSYS=winsymlinks:nativestrict && export MSYS
. ./test-lib.sh

test_expect_success MINGW,SYMLINKS 'resolve a target below a junction' '
	mkdir realdir &&
	cmd.exe //c "mklink /j junction realdir" &&
	link=$(printf %s junction/sub/deep | git hash-object -w --stdin) &&
	file=$(printf %s content | git hash-object -w --stdin) &&
	git update-index --add --cacheinfo 120000,$link,nested &&
	git update-index --add --cacheinfo 100644,$file,realdir/sub/deep/file &&
	git checkout -- . &&
	cmd.exe //c dir . >dir-listing &&
	test_grep "SYMLINKD.*nested" dir-listing
'

test_done
