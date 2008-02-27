#!/bin/sh
#
# Copyright (c) 2008 Clemens Buchacher <drizzd@aon.at>
#

if test -z "$GIT_TEST_HTTPD"
then
	say "skipping test, network testing disabled by default"
	say "(define GIT_TEST_HTTPD to enable)"
	test_done
	exit
fi

LIB_HTTPD_PATH=${LIB_HTTPD_PATH-'/usr/sbin/apache2'}
LIB_HTTPD_PORT=${LIB_HTTPD_PORT-'8111'}

TEST_PATH="$PWD"/../lib-httpd
HTTPD_ROOT_PATH="$PWD"/httpd
HTTPD_DOCUMENT_ROOT_PATH=$HTTPD_ROOT_PATH/www

if ! test -x "$LIB_HTTPD_PATH"
then
        say "skipping test, no web server found at '$LIB_HTTPD_PATH'"
        test_done
        exit
fi

HTTPD_VERSION=`$LIB_HTTPD_PATH -v | \
	sed -n 's/^Server version: Apache\/\([0-9]*\)\..*$/\1/p; q'`

if test -n "$HTTPD_VERSION"
then
	if test -z "$LIB_HTTPD_MODULE_PATH"
	then
		if ! test $HTTPD_VERSION -ge 2
		then
			say "skipping test, at least Apache version 2 is required"
			test_done
			exit
		fi

		LIB_HTTPD_MODULE_PATH='/usr/lib/apache2/modules'
	fi
else
	error "Could not identify web server at '$LIB_HTTPD_PATH'"
fi

HTTPD_PARA="-d $HTTPD_ROOT_PATH -f $TEST_PATH/apache.conf"

prepare_httpd() {
	mkdir -p $HTTPD_DOCUMENT_ROOT_PATH

	ln -s $LIB_HTTPD_MODULE_PATH $HTTPD_ROOT_PATH/modules

	if test -n "$LIB_HTTPD_SSL"
	then
		HTTPD_URL=https://127.0.0.1:$LIB_HTTPD_PORT

		RANDFILE_PATH="$HTTPD_ROOT_PATH"/.rnd openssl req \
			-config $TEST_PATH/ssl.cnf \
			-new -x509 -nodes \
			-out $HTTPD_ROOT_PATH/httpd.pem \
			-keyout $HTTPD_ROOT_PATH/httpd.pem
		export GIT_SSL_NO_VERIFY=t
		HTTPD_PARA="$HTTPD_PARA -DSSL"
	else
		HTTPD_URL=http://127.0.0.1:$LIB_HTTPD_PORT
	fi

	if test -n "$LIB_HTTPD_DAV" -o -n "$LIB_HTTPD_SVN"
	then
		HTTPD_PARA="$HTTPD_PARA -DDAV"

		if test -n "$LIB_HTTPD_SVN"
		then
			HTTPD_PARA="$HTTPD_PARA -DSVN"
			rawsvnrepo="$HTTPD_ROOT_PATH/svnrepo"
			svnrepo="http://127.0.0.1:$LIB_HTTPD_PORT/svn"
		fi
	fi
}

start_httpd() {
	prepare_httpd

	trap 'stop_httpd; die' exit

	"$LIB_HTTPD_PATH" $HTTPD_PARA \
		-c "Listen 127.0.0.1:$LIB_HTTPD_PORT" -k start
}

stop_httpd() {
	trap 'die' exit

	"$LIB_HTTPD_PATH" $HTTPD_PARA -k stop
}
