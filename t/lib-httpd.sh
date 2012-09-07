#!/bin/sh
#
# Copyright (c) 2008 Clemens Buchacher <drizzd@aon.at>
#

if test -z "$GIT_TEST_HTTPD"
then
	skip_all="Network testing disabled (define GIT_TEST_HTTPD to enable)"
	test_done
fi

HTTPD_PARA=""

for DEFAULT_HTTPD_PATH in '/usr/sbin/httpd' '/usr/sbin/apache2'
do
	if test -x "$DEFAULT_HTTPD_PATH"
	then
		break
	fi
done

for DEFAULT_HTTPD_MODULE_PATH in '/usr/libexec/apache2' \
				 '/usr/lib/apache2/modules' \
				 '/usr/lib64/httpd/modules' \
				 '/usr/lib/httpd/modules'
do
	if test -d "$DEFAULT_HTTPD_MODULE_PATH"
	then
		break
	fi
done

case $(uname) in
	Darwin)
		HTTPD_PARA="$HTTPD_PARA -DDarwin"
	;;
esac

LIB_HTTPD_PATH=${LIB_HTTPD_PATH-"$DEFAULT_HTTPD_PATH"}
LIB_HTTPD_PORT=${LIB_HTTPD_PORT-'8111'}

TEST_PATH="$TEST_DIRECTORY"/lib-httpd
HTTPD_ROOT_PATH="$PWD"/httpd
HTTPD_DOCUMENT_ROOT_PATH=$HTTPD_ROOT_PATH/www

# hack to suppress apache PassEnv warnings
GIT_VALGRIND=$GIT_VALGRIND; export GIT_VALGRIND
GIT_VALGRIND_OPTIONS=$GIT_VALGRIND_OPTIONS; export GIT_VALGRIND_OPTIONS

if ! test -x "$LIB_HTTPD_PATH"
then
	skip_all="skipping test, no web server found at '$LIB_HTTPD_PATH'"
	test_done
fi

HTTPD_VERSION=`$LIB_HTTPD_PATH -v | \
	sed -n 's/^Server version: Apache\/\([0-9]*\)\..*$/\1/p; q'`

if test -n "$HTTPD_VERSION"
then
	if test -z "$LIB_HTTPD_MODULE_PATH"
	then
		if ! test $HTTPD_VERSION -ge 2
		then
			skip_all="skipping test, at least Apache version 2 is required"
			test_done
		fi
		if ! test -d "$DEFAULT_HTTPD_MODULE_PATH"
		then
			skip_all="Apache module directory not found.  Skipping tests."
			test_done
		fi

		LIB_HTTPD_MODULE_PATH="$DEFAULT_HTTPD_MODULE_PATH"
	fi
else
	error "Could not identify web server at '$LIB_HTTPD_PATH'"
fi

prepare_httpd() {
	mkdir -p "$HTTPD_DOCUMENT_ROOT_PATH"
	cp "$TEST_PATH"/passwd "$HTTPD_ROOT_PATH"

	ln -s "$LIB_HTTPD_MODULE_PATH" "$HTTPD_ROOT_PATH/modules"

	if test -n "$LIB_HTTPD_SSL"
	then
		HTTPD_PROTO=https

		RANDFILE_PATH="$HTTPD_ROOT_PATH"/.rnd openssl req \
			-config "$TEST_PATH/ssl.cnf" \
			-new -x509 -nodes \
			-out "$HTTPD_ROOT_PATH/httpd.pem" \
			-keyout "$HTTPD_ROOT_PATH/httpd.pem"
		GIT_SSL_NO_VERIFY=t
		export GIT_SSL_NO_VERIFY
		HTTPD_PARA="$HTTPD_PARA -DSSL"
	else
		HTTPD_PROTO=http
	fi
	HTTPD_DEST=127.0.0.1:$LIB_HTTPD_PORT
	HTTPD_URL=$HTTPD_PROTO://$HTTPD_DEST
	HTTPD_URL_USER=$HTTPD_PROTO://user%40host@$HTTPD_DEST
	HTTPD_URL_USER_PASS=$HTTPD_PROTO://user%40host:user%40host@$HTTPD_DEST

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
	prepare_httpd >&3 2>&4

	trap 'code=$?; stop_httpd; (exit $code); die' EXIT

	"$LIB_HTTPD_PATH" -d "$HTTPD_ROOT_PATH" \
		-f "$TEST_PATH/apache.conf" $HTTPD_PARA \
		-c "Listen 127.0.0.1:$LIB_HTTPD_PORT" -k start \
		>&3 2>&4
	if test $? -ne 0
	then
		skip_all="skipping test, web server setup failed"
		trap 'die' EXIT
		test_done
	fi
}

stop_httpd() {
	trap 'die' EXIT

	"$LIB_HTTPD_PATH" -d "$HTTPD_ROOT_PATH" \
		-f "$TEST_PATH/apache.conf" $HTTPD_PARA -k stop
}

test_http_push_nonff() {
	REMOTE_REPO=$1
	LOCAL_REPO=$2
	BRANCH=$3

	test_expect_success 'non-fast-forward push fails' '
		cd "$REMOTE_REPO" &&
		HEAD=$(git rev-parse --verify HEAD) &&

		cd "$LOCAL_REPO" &&
		git checkout $BRANCH &&
		echo "changed" > path2 &&
		git commit -a -m path2 --amend &&

		test_must_fail git push -v origin >output 2>&1 &&
		(cd "$REMOTE_REPO" &&
		 test $HEAD = $(git rev-parse --verify HEAD))
	'

	test_expect_success 'non-fast-forward push show ref status' '
		grep "^ ! \[rejected\][ ]*$BRANCH -> $BRANCH (non-fast-forward)$" output
	'

	test_expect_success 'non-fast-forward push shows help message' '
		test_i18ngrep "Updates were rejected because" output
	'
}

setup_askpass_helper() {
	test_expect_success 'setup askpass helper' '
		write_script "$TRASH_DIRECTORY/askpass" <<-\EOF &&
		echo >>"$TRASH_DIRECTORY/askpass-query" "askpass: $*" &&
		cat "$TRASH_DIRECTORY/askpass-response"
		EOF
		GIT_ASKPASS="$TRASH_DIRECTORY/askpass" &&
		export GIT_ASKPASS &&
		export TRASH_DIRECTORY
	'
}

set_askpass() {
	>"$TRASH_DIRECTORY/askpass-query" &&
	echo "$*" >"$TRASH_DIRECTORY/askpass-response"
}

expect_askpass() {
	dest=$HTTPD_DEST
	{
		case "$1" in
		none)
			;;
		pass)
			echo "askpass: Password for 'http://$2@$dest': "
			;;
		both)
			echo "askpass: Username for 'http://$dest': "
			echo "askpass: Password for 'http://$2@$dest': "
			;;
		*)
			false
			;;
		esac
	} >"$TRASH_DIRECTORY/askpass-expect" &&
	test_cmp "$TRASH_DIRECTORY/askpass-expect" \
		 "$TRASH_DIRECTORY/askpass-query"
}
