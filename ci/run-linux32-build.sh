#!/bin/sh
#
# Build and test Git in a 32-bit environment
#
# Usage:
#   run-linux32-build.sh <host-user-id>
#

set -ex

. "${0%/*}/lib-docker.sh"

# Update packages to the latest available versions
linux32 --32bit i386 sh -c '
    apt update >/dev/null &&
    apt install -y build-essential libcurl4-openssl-dev libssl-dev \
	libexpat-dev gettext python >/dev/null
'

# Build and test
linux32 --32bit i386 su -m -l $CI_USER -c '
	set -ex
	cd /usr/src/git
	test -n "$cache_dir" && ln -s "$cache_dir/.prove" t/.prove
	make
	make test
'
