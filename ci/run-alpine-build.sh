#!/bin/sh
#
# Build and test Git in Alpine Linux
#
# Usage:
#   run-alpine-build.sh <host-user-id>
#

set -ex

useradd () {
	adduser -D "$@"
}

. "${0%/*}/lib-docker.sh"

# Update packages to the latest available versions
apk add --update autoconf build-base curl-dev openssl-dev expat-dev \
	gettext pcre2-dev python3 musl-libintl >/dev/null

# Build and test
su -m -l $CI_USER -c '
	set -ex
	cd /usr/src/git
	test -n "$cache_dir" && ln -s "$cache_dir/.prove" t/.prove
	autoconf
	echo "PYTHON_PATH=/usr/bin/python3" >config.mak
	./configure --with-libpcre
	make
	make test
'
