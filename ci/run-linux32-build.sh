#!/bin/sh
#
# Build and test Git in a 32-bit environment
#
# Usage:
#   run-linux32-build.sh [host-user-id]
#

# Update packages to the latest available versions
linux32 --32bit i386 sh -c '
    apt update >/dev/null &&
    apt install -y build-essential libcurl4-openssl-dev libssl-dev \
	libexpat-dev gettext python >/dev/null
' &&

# If this script runs inside a docker container, then all commands are
# usually executed as root. Consequently, the host user might not be
# able to access the test output files.
# If a host user id is given, then create a user "ci" with the host user
# id to make everything accessible to the host user.
HOST_UID=$1 &&
CI_USER=$USER &&
test -z $HOST_UID || (CI_USER="ci" && useradd -u $HOST_UID $CI_USER) &&

# Build and test
linux32 --32bit i386 su -m -l $CI_USER -c '
    cd /usr/src/git &&
    make --jobs=2 &&
    make --quiet test
'
