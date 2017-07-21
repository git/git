#!/bin/sh
# Inflate the size of an EXISTING repo.
#
# This script should be run inside the worktree of a TEST repo.
# It will use the contents of the current HEAD to generate a
# commit containing copies of the current worktree such that the
# total size of the commit has at least <target_size> files.
#
# Usage: [-t target_size] [-b branch_name]

set -e

target_size=10000
branch_name=p0006-ballast
ballast=ballast

while test "$#" -ne 0
do
    case "$1" in
	-b)
	    shift;
	    test "$#" -ne 0 || { echo 'error: -b requires an argument' >&2; exit 1; }
	    branch_name=$1;
	    shift ;;
	-t)
	    shift;
	    test "$#" -ne 0 || { echo 'error: -t requires an argument' >&2; exit 1; }
	    target_size=$1;
	    shift ;;
	*)
	    echo "error: unknown option '$1'" >&2; exit 1 ;;
    esac
done

git ls-tree -r HEAD >GEN_src_list
nr_src_files=$(cat GEN_src_list | wc -l)

src_branch=$(git symbolic-ref --short HEAD)

echo "Branch $src_branch initially has $nr_src_files files."

if test $target_size -le $nr_src_files
then
    echo "Repository already exceeds target size $target_size."
    rm GEN_src_list
    exit 1
fi

# Create well-known branch and add 1 file change to start
# if off before the ballast.
git checkout -b $branch_name HEAD
echo "$target_size" > inflate-repo.params
git add inflate-repo.params
git commit -q -m params

# Create ballast for in our branch.
copy=1
nr_files=$nr_src_files
while test $nr_files -lt $target_size
do
    sed -e "s|	|	$ballast/$copy/|" <GEN_src_list |
	git update-index --index-info

    nr_files=$(expr $nr_files + $nr_src_files)
    copy=$(expr $copy + 1)
done
rm GEN_src_list
git commit -q -m "ballast"

# Modify 1 file and commit.
echo "$target_size" >> inflate-repo.params
git add inflate-repo.params
git commit -q -m "ballast plus 1"

nr_files=$(git ls-files | wc -l)

# Checkout master to put repo in canonical state (because
# the perf test may need to clone and enable sparse-checkout
# before attempting to checkout a commit with the ballast
# (because it may contain 100K directories and 1M files)).
git checkout $src_branch

echo "Repository inflated. Branch $branch_name has $nr_files files."

exit 0
