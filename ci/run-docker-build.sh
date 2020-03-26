#!/bin/sh
#
# Build and test Git inside container
#
# Usage:
#   run-docker-build.sh <host-user-id>
#

set -ex

if test $# -ne 1 || test -z "$1"
then
	echo >&2 "usage: run-docker-build.sh <host-user-id>"
	exit 1
fi

case "$jobname" in
Linux32)
	switch_cmd="linux32 --32bit i386"
	;;
linux-musl)
	switch_cmd=
	useradd () { adduser -D "$@"; }
	;;
*)
	exit 1
	;;
esac

"${0%/*}/install-docker-dependencies.sh"

# If this script runs inside a docker container, then all commands are
# usually executed as root. Consequently, the host user might not be
# able to access the test output files.
# If a non 0 host user id is given, then create a user "ci" with that
# user id to make everything accessible to the host user.
HOST_UID=$1
if test $HOST_UID -eq 0
then
	# Just in case someone does want to run the test suite as root.
	CI_USER=root
else
	CI_USER=ci
	if test "$(id -u $CI_USER 2>/dev/null)" = $HOST_UID
	then
		echo "user '$CI_USER' already exists with the requested ID $HOST_UID"
	else
		useradd -u $HOST_UID $CI_USER
	fi

	# Due to a bug the test suite was run as root in the past, so
	# a prove state file created back then is only accessible by
	# root.  Now that bug is fixed, the test suite is run as a
	# regular user, but the prove state file coming from Travis
	# CI's cache might still be owned by root.
	# Make sure that this user has rights to any cached files,
	# including an existing prove state file.
	test -n "$cache_dir" && chown -R $HOST_UID:$HOST_UID "$cache_dir"
fi

# Build and test
command $switch_cmd su -m -l $CI_USER -c "
	set -ex
	export DEVELOPER='$DEVELOPER'
	export DEFAULT_TEST_TARGET='$DEFAULT_TEST_TARGET'
	export GIT_PROVE_OPTS='$GIT_PROVE_OPTS'
	export GIT_TEST_OPTS='$GIT_TEST_OPTS'
	export GIT_TEST_CLONE_2GB='$GIT_TEST_CLONE_2GB'
	export MAKEFLAGS='$MAKEFLAGS'
	export cache_dir='$cache_dir'
	cd /usr/src/git
	test -n '$cache_dir' && ln -s '$cache_dir/.prove' t/.prove
	make
	make test
"
