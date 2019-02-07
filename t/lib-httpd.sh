# Shell library to run an HTTP server for use in tests.
# Ends the test early if httpd tests should not be run,
# for example because the user has not enabled them.
#
# Usage:
#
#	. ./test-lib.sh
#	. "$TEST_DIRECTORY"/lib-httpd.sh
#	start_httpd
#
#	test_expect_success '...' '
#		...
#	'
#
#	test_expect_success ...
#
#	stop_httpd
#	test_done
#
# Can be configured using the following variables.
#
#    GIT_TEST_HTTPD              enable HTTPD tests
#    LIB_HTTPD_PATH              web server path
#    LIB_HTTPD_MODULE_PATH       web server modules path
#    LIB_HTTPD_PORT              listening port
#    LIB_HTTPD_DAV               enable DAV
#    LIB_HTTPD_SVN               enable SVN at given location (e.g. "svn")
#    LIB_HTTPD_SSL               enable SSL
#
# Copyright (c) 2008 Clemens Buchacher <drizzd@aon.at>
#

if test -n "$NO_CURL"
then
	skip_all='skipping test, git built without http support'
	test_done
fi

if test -n "$NO_EXPAT" && test -n "$LIB_HTTPD_DAV"
then
	skip_all='skipping test, git built without expat support'
	test_done
fi

test_tristate GIT_TEST_HTTPD
if test "$GIT_TEST_HTTPD" = false
then
	skip_all="Network testing disabled (unset GIT_TEST_HTTPD to enable)"
	test_done
fi

if ! test_have_prereq NOT_ROOT; then
	test_skip_or_die $GIT_TEST_HTTPD \
		"Cannot run httpd tests as root"
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
test_set_port LIB_HTTPD_PORT

TEST_PATH="$TEST_DIRECTORY"/lib-httpd
HTTPD_ROOT_PATH="$PWD"/httpd
HTTPD_DOCUMENT_ROOT_PATH=$HTTPD_ROOT_PATH/www

# hack to suppress apache PassEnv warnings
GIT_VALGRIND=$GIT_VALGRIND; export GIT_VALGRIND
GIT_VALGRIND_OPTIONS=$GIT_VALGRIND_OPTIONS; export GIT_VALGRIND_OPTIONS
GIT_TRACE=$GIT_TRACE; export GIT_TRACE

if ! test -x "$LIB_HTTPD_PATH"
then
	test_skip_or_die $GIT_TEST_HTTPD "no web server found at '$LIB_HTTPD_PATH'"
fi

HTTPD_VERSION=$($LIB_HTTPD_PATH -v | \
	sed -n 's/^Server version: Apache\/\([0-9]*\)\..*$/\1/p; q')

if test -n "$HTTPD_VERSION"
then
	if test -z "$LIB_HTTPD_MODULE_PATH"
	then
		if ! test $HTTPD_VERSION -ge 2
		then
			test_skip_or_die $GIT_TEST_HTTPD \
				"at least Apache version 2 is required"
		fi
		if ! test -d "$DEFAULT_HTTPD_MODULE_PATH"
		then
			test_skip_or_die $GIT_TEST_HTTPD \
				"Apache module directory not found"
		fi

		LIB_HTTPD_MODULE_PATH="$DEFAULT_HTTPD_MODULE_PATH"
	fi
else
	test_skip_or_die $GIT_TEST_HTTPD \
		"Could not identify web server at '$LIB_HTTPD_PATH'"
fi

install_script () {
	write_script "$HTTPD_ROOT_PATH/$1" <"$TEST_PATH/$1"
}

prepare_httpd() {
	mkdir -p "$HTTPD_DOCUMENT_ROOT_PATH"
	cp "$TEST_PATH"/passwd "$HTTPD_ROOT_PATH"
	install_script broken-smart-http.sh
	install_script error-smart-http.sh
	install_script error.sh
	install_script apply-one-time-sed.sh

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
	HTTPD_URL_USER_PASS=$HTTPD_PROTO://user%40host:pass%40host@$HTTPD_DEST

	if test -n "$LIB_HTTPD_DAV" || test -n "$LIB_HTTPD_SVN"
	then
		HTTPD_PARA="$HTTPD_PARA -DDAV"

		if test -n "$LIB_HTTPD_SVN"
		then
			HTTPD_PARA="$HTTPD_PARA -DSVN"
			LIB_HTTPD_SVNPATH="$rawsvnrepo"
			svnrepo="http://127.0.0.1:$LIB_HTTPD_PORT/"
			svnrepo="$svnrepo$LIB_HTTPD_SVN"
			export LIB_HTTPD_SVN LIB_HTTPD_SVNPATH
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
		trap 'die' EXIT
		cat "$HTTPD_ROOT_PATH"/error.log >&4 2>/dev/null
		test_skip_or_die $GIT_TEST_HTTPD "web server setup failed"
	fi
}

stop_httpd() {
	trap 'die' EXIT

	"$LIB_HTTPD_PATH" -d "$HTTPD_ROOT_PATH" \
		-f "$TEST_PATH/apache.conf" $HTTPD_PARA -k stop
}

test_http_push_nonff () {
	REMOTE_REPO=$1
	LOCAL_REPO=$2
	BRANCH=$3
	EXPECT_CAS_RESULT=${4-failure}

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

	test_expect_${EXPECT_CAS_RESULT} 'force with lease aka cas' '
		HEAD=$(	cd "$REMOTE_REPO" && git rev-parse --verify HEAD ) &&
		test_when_finished '\''
			(cd "$REMOTE_REPO" && git update-ref HEAD "$HEAD")
		'\'' &&
		(
			cd "$LOCAL_REPO" &&
			git push -v --force-with-lease=$BRANCH:$HEAD origin
		) &&
		git rev-parse --verify "$BRANCH" >expect &&
		(
			cd "$REMOTE_REPO" && git rev-parse --verify HEAD
		) >actual &&
		test_cmp expect actual
	'
}

setup_askpass_helper() {
	test_expect_success 'setup askpass helper' '
		write_script "$TRASH_DIRECTORY/askpass" <<-\EOF &&
		echo >>"$TRASH_DIRECTORY/askpass-query" "askpass: $*" &&
		case "$*" in
		*Username*)
			what=user
			;;
		*Password*)
			what=pass
			;;
		esac &&
		cat "$TRASH_DIRECTORY/askpass-$what"
		EOF
		GIT_ASKPASS="$TRASH_DIRECTORY/askpass" &&
		export GIT_ASKPASS &&
		export TRASH_DIRECTORY
	'
}

set_askpass() {
	>"$TRASH_DIRECTORY/askpass-query" &&
	echo "$1" >"$TRASH_DIRECTORY/askpass-user" &&
	echo "$2" >"$TRASH_DIRECTORY/askpass-pass"
}

expect_askpass() {
	dest=$HTTPD_DEST${3+/$3}

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

strip_access_log() {
	sed -e "
		s/^.* \"//
		s/\"//
		s/ [1-9][0-9]*\$//
		s/^GET /GET  /
	" "$HTTPD_ROOT_PATH"/access.log
}

# Requires one argument: the name of a file containing the expected stripped
# access log entries.
check_access_log() {
	sort "$1" >"$1".sorted &&
	strip_access_log >access.log.stripped &&
	sort access.log.stripped >access.log.sorted &&
	if ! test_cmp "$1".sorted access.log.sorted
	then
		test_cmp "$1" access.log.stripped
	fi
}
