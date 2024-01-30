#!/bin/sh
#
# Install dependencies required to build and test Git inside container
#

. ${0%/*}/lib.sh

begin_group "Install dependencies"

case "$jobname" in
linux32)
	linux32 --32bit i386 sh -c '
		apt update >/dev/null &&
		apt install -y build-essential libcurl4-openssl-dev \
			libssl-dev libexpat-dev gettext python >/dev/null
	'
	;;
linux-musl)
	apk add --update shadow sudo build-base curl-dev openssl-dev expat-dev gettext \
		pcre2-dev python3 musl-libintl perl-utils ncurses \
		apache2 apache2-http2 apache2-proxy apache2-ssl apache2-webdav apr-util-dbd_sqlite3 \
		bash cvs gnupg perl-cgi perl-dbd-sqlite >/dev/null
	;;
linux-*|StaticAnalysis)
	# Required so that apt doesn't wait for user input on certain packages.
	export DEBIAN_FRONTEND=noninteractive

	apt update -q &&
	apt install -q -y sudo git make language-pack-is libsvn-perl apache2 libssl-dev \
		libcurl4-openssl-dev libexpat-dev tcl tk gettext zlib1g-dev \
		perl-modules liberror-perl libauthen-sasl-perl libemail-valid-perl \
		libdbd-sqlite3-perl libio-socket-ssl-perl libnet-smtp-ssl-perl ${CC_PACKAGE:-${CC:-gcc}} \
		apache2 cvs cvsps gnupg libcgi-pm-perl subversion

	if test "$jobname" = StaticAnalysis
	then
		apt install -q -y coccinelle
	fi
	;;
pedantic)
	dnf -yq update >/dev/null &&
	dnf -yq install make gcc findutils diffutils perl python3 gettext zlib-devel expat-devel openssl-devel curl-devel pcre2-devel >/dev/null
	;;
esac

end_group "Install dependencies"
