#!/bin/sh
#
# Build and test Git in a 32-bit environment
#
# Usage:
#   run-linux32-build.sh <host-user-id>
#

set -ex

if test $# -ne 1 || test -z "$1"
then
	echo >&2 "usage: run-linux32-build.sh <host-user-id>"
	exit 1
fi

# Update packages to the latest available versions
linux32 --32bit i386 sh -c '
    apt update >/dev/null &&
    apt install -y build-essential libcurl4-openssl-dev libssl-dev \
	libexpat-dev gettext python >/dev/null
'

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
linux32 --32bit i386 su -m -l $CI_USER -c '
	set -ex
	cd /usr/src/git
	test -n "$cache_dir" && ln -s "$cache_dir/.prove" t/.prove
	make
	make test
'
