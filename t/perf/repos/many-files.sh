#!/bin/sh
# Generate test data repository using the given parameters.
# When omitted, we create "gen-many-files-d-w-f.git".
#
# Usage: [-r repo] [-d depth] [-w width] [-f files]
#
# -r repo: path to the new repo to be generated
# -d depth: the depth of sub-directories
# -w width: the number of sub-directories at each level
# -f files: the number of files created in each directory
#
# Note that all files will have the same SHA-1 and each
# directory at a level will have the same SHA-1, so we
# will potentially have a large index, but not a large
# ODB.
#
# Ballast will be created under "ballast/".

EMPTY_BLOB=e69de29bb2d1d6434b8b29ae775ad8c2e48c5391

set -e

# (5, 10, 9) will create 999,999 ballast files.
# (4, 10, 9) will create  99,999 ballast files.
depth=5
width=10
files=9

while test "$#" -ne 0
do
    case "$1" in
	-r)
	    shift;
	    test "$#" -ne 0 || { echo 'error: -r requires an argument' >&2; exit 1; }
	    repo=$1;
	    shift ;;
	-d)
	    shift;
	    test "$#" -ne 0 || { echo 'error: -d requires an argument' >&2; exit 1; }
	    depth=$1;
	    shift ;;
	-w)
	    shift;
	    test "$#" -ne 0 || { echo 'error: -w requires an argument' >&2; exit 1; }
	    width=$1;
	    shift ;;
	-f)
	    shift;
	    test "$#" -ne 0 || { echo 'error: -f requires an argument' >&2; exit 1; }
	    files=$1;
	    shift ;;
	*)
	    echo "error: unknown option '$1'" >&2; exit 1 ;;
	esac
done

# Inflate the index with thousands of empty files.
# usage: dir depth width files
fill_index() {
	awk -v arg_dir=$1 -v arg_depth=$2 -v arg_width=$3 -v arg_files=$4 '
		function make_paths(dir, depth, width, files, f, w) {
			for (f = 1; f <= files; f++) {
				print dir "/file" f
			}
			if (depth > 0) {
				for (w = 1; w <= width; w++) {
					make_paths(dir "/dir" w, depth - 1, width, files)
				}
			}
		}
		END { make_paths(arg_dir, arg_depth, arg_width, arg_files) }
		' </dev/null |
	sed "s/^/100644 $EMPTY_BLOB	/" |
	git update-index --index-info
	return 0
}

[ -z "$repo" ] && repo=gen-many-files-$depth.$width.$files.git

mkdir $repo
cd $repo
git init .

# Create an initial commit just to define master.
touch many-files.empty
echo "$depth $width $files" >many-files.params
git add many-files.*
git commit -q -m params

# Create ballast for p0006 based upon the given params and
# inflate the index with thousands of empty files and commit.
git checkout -b p0006-ballast
fill_index "ballast" $depth $width $files
git commit -q -m "ballast"

nr_files=$(git ls-files | wc -l)

# Modify 1 file and commit.
echo "$depth $width $files" >>many-files.params
git add many-files.params
git commit -q -m "ballast plus 1"

# Checkout master to put repo in canonical state (because
# the perf test may need to clone and enable sparse-checkout
# before attempting to checkout a commit with the ballast
# (because it may contain 100K directories and 1M files)).
git checkout master

echo "Repository "$repo" ($depth, $width, $files) created.  Ballast $nr_files."
exit 0
