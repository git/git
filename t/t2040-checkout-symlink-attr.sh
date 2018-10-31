#!/bin/sh

test_description='checkout symlinks with `symlink` attribute on Windows

Ensures that Git for Windows creates symlinks of the right type,
as specified by the `symlink` attribute in `.gitattributes`.'

# Tell MSYS to create native symlinks. Without this flag test-lib's
# prerequisite detection for SYMLINKS doesn't detect the right thing.
MSYS=winsymlinks:nativestrict && export MSYS

. ./test-lib.sh

if ! test_have_prereq MINGW,SYMLINKS
then
	skip_all='skipping $0: MinGW-only test, which requires symlink support.'
	test_done
fi

# Adds a symlink to the index without clobbering the work tree.
cache_symlink () {
	sha=$(printf '%s' "$1" | git hash-object --stdin -w) &&
	git update-index --add --cacheinfo 120000,$sha,"$2"
}

test_expect_success 'checkout symlinks with attr' '
	cache_symlink file1 file-link &&
	cache_symlink dir dir-link &&

	printf "file-link symlink=file\ndir-link symlink=dir\n" >.gitattributes &&
	git add .gitattributes &&

	git checkout . &&

	mkdir dir &&
	echo "[a]b=c" >file1 &&
	echo "[x]y=z" >dir/file2 &&

	# MSYS2 is very forgiving, it will resolve symlinks even if the
	# symlink type is incorrect. To make this test meaningful, try
	# them with a native, non-MSYS executable, such as `git config`.
	test "$(git config -f file-link a.b)" = "c" &&
	test "$(git config -f dir-link/file2 x.y)" = "z"
'

test_done
