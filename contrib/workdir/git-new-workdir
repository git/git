#!/bin/sh

usage () {
	echo "usage:" $@
	exit 127
}

die () {
	echo $@
	exit 128
}

failed () {
	die "unable to create new workdir '$new_workdir'!"
}

if test $# -lt 2 || test $# -gt 3
then
	usage "$0 <repository> <new_workdir> [<branch>]"
fi

orig_git=$1
new_workdir=$2
branch=$3

# want to make sure that what is pointed to has a .git directory ...
git_dir=$(cd "$orig_git" 2>/dev/null &&
  git rev-parse --git-dir 2>/dev/null) ||
  die "Not a git repository: \"$orig_git\""

case "$git_dir" in
.git)
	git_dir="$orig_git/.git"
	;;
.)
	git_dir=$orig_git
	;;
esac

# don't link to a configured bare repository
isbare=$(git --git-dir="$git_dir" config --bool --get core.bare)
if test ztrue = "z$isbare"
then
	die "\"$git_dir\" has core.bare set to true," \
		" remove from \"$git_dir/config\" to use $0"
fi

# don't link to a workdir
if test -h "$git_dir/config"
then
	die "\"$orig_git\" is a working directory only, please specify" \
		"a complete repository."
fi

# make sure the links in the workdir have full paths to the original repo
git_dir=$(cd "$git_dir" && pwd) || exit 1

# don't recreate a workdir over an existing directory, unless it's empty
if test -d "$new_workdir"
then
	if test $(ls -a1 "$new_workdir/." | wc -l) -ne 2
	then
		die "destination directory '$new_workdir' is not empty."
	fi
	cleandir="$new_workdir/.git"
else
	cleandir="$new_workdir"
fi

mkdir -p "$new_workdir/.git" || failed
cleandir=$(cd "$cleandir" && pwd) || failed

cleanup () {
	rm -rf "$cleandir"
}
siglist="0 1 2 15"
trap cleanup $siglist

# create the links to the original repo.  explicitly exclude index, HEAD and
# logs/HEAD from the list since they are purely related to the current working
# directory, and should not be shared.
for x in config refs logs/refs objects info hooks packed-refs remotes rr-cache svn
do
	# create a containing directory if needed
	case $x in
	*/*)
		mkdir -p "$new_workdir/.git/${x%/*}"
		;;
	esac

	ln -s "$git_dir/$x" "$new_workdir/.git/$x" || failed
done

# commands below this are run in the context of the new workdir
cd "$new_workdir" || failed

# copy the HEAD from the original repository as a default branch
cp "$git_dir/HEAD" .git/HEAD || failed

# the workdir is set up.  if the checkout fails, the user can fix it.
trap - $siglist

# checkout the branch (either the same as HEAD from the original repository,
# or the one that was asked for)
git checkout -f $branch
