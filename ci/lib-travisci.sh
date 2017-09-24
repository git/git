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
# something went wrong
set -e

skip_branch_tip_with_tag
