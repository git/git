# Library of functions shared by all CI scripts

skip_branch_tip_with_tag () {
	# Sometimes, a branch is pushed at the same time the tag that points
	# at the same commit as the tip of the branch is pushed, and building
	# both at the same time is a waste.
	#
	# Travis gives a tagname e.g. v2.14.0 in $TRAVIS_BRANCH when
	# the build is triggered by a push to a tag.  Let's see if
	# $TRAVIS_BRANCH is exactly at a tag, and if so, if it is
	# different from $TRAVIS_BRANCH.  That way, we can tell if
	# we are building the tip of a branch that is tagged and
	# we can skip the build because we won't be skipping a build
	# of a tag.

	if TAG=$(git describe --exact-match "$TRAVIS_BRANCH" 2>/dev/null) &&
		test "$TAG" != "$TRAVIS_BRANCH"
	then
		echo "Tip of $TRAVIS_BRANCH is exactly at $TAG"
		exit 0
	fi
}

# Set 'exit on error' for all CI scripts to let the caller know that
# something went wrong.
# Set tracing executed commands, primarily setting environment variables
# and installing dependencies.
set -ex

skip_branch_tip_with_tag

if test -z "$jobname"
then
	jobname="$TRAVIS_OS_NAME-$CC"
fi

export DEVELOPER=1
export DEFAULT_TEST_TARGET=prove
export GIT_PROVE_OPTS="--timer --jobs 3 --state=failed,slow,save"
export GIT_TEST_OPTS="--verbose-log"
export GIT_TEST_CLONE_2GB=YesPlease

case "$jobname" in
linux-clang|linux-gcc)
	export GIT_TEST_HTTPD=YesPlease

	# The Linux build installs the defined dependency versions below.
	# The OS X build installs the latest available versions. Keep that
	# in mind when you encounter a broken OS X build!
	export LINUX_P4_VERSION="16.2"
	export LINUX_GIT_LFS_VERSION="1.5.2"

	P4_PATH="$(pwd)/custom/p4"
	GIT_LFS_PATH="$(pwd)/custom/git-lfs"
	export PATH="$GIT_LFS_PATH:$P4_PATH:$PATH"
	;;
osx-clang|osx-gcc)
	# t9810 occasionally fails on Travis CI OS X
	# t9816 occasionally fails with "TAP out of sequence errors" on
	# Travis CI OS X
	export GIT_SKIP_TESTS="t9810 t9816"
	;;
GETTEXT_POISON)
	export GETTEXT_POISON=YesPlease
	;;
esac
