#!/bin/sh

do_filename() {
	desc=$1
	postimage=$2

	rm -fr file-creation &&
	git init file-creation &&
	(
		cd file-creation &&
		git commit --allow-empty -m init &&
		echo postimage >"$postimage" &&
		git add -N "$postimage" &&
		git diff HEAD >"../git-$desc.diff"
	) &&

	rm -fr trad-modification &&
	mkdir trad-modification &&
	(
		cd trad-modification &&
		echo preimage >"$postimage.orig" &&
		echo postimage >"$postimage" &&
		! diff -u "$postimage.orig" "$postimage" >"../diff-$desc.diff"
	) &&

	rm -fr trad-creation &&
	mkdir trad-creation &&
	(
		cd trad-creation &&
		mkdir a b &&
		echo postimage >"b/$postimage" &&
		! diff -pruN a b >"../add-$desc.diff"
	)
}

do_filename plain postimage.txt &&
do_filename 'with spaces' 'post image.txt' &&
do_filename 'with tab' 'post	image.txt' &&
do_filename 'with backslash' 'post\image.txt' &&
do_filename 'with quote' '"postimage".txt' &&
expand add-plain.diff >damaged.diff ||
{
	echo >&2 Failed. &&
	exit 1
}
