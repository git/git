#!/bin/sh

test_description='basic credential helper tests'
. ./test-lib.sh

# Try a set of credential helpers; the expected
# stdout and stderr should be provided on stdin,
# separated by "--".
check() {
	while read line; do
		case "$line" in
		--) break ;;
		*) echo "$line" ;;
		esac
	done >expect-stdout &&
	cat >expect-stderr &&
	test-credential "$@" >stdout 2>stderr &&
	test_cmp expect-stdout stdout &&
	test_cmp expect-stderr stderr
}

test_expect_success 'setup helper scripts' '
	cat >dump <<-\EOF &&
	whoami=$1; shift
	if test $# = 0; then
		echo >&2 "$whoami: <empty>"
	else
		for i in "$@"; do
			echo >&2 "$whoami: $i"
		done
	fi
	EOF
	chmod +x dump &&

	cat >git-credential-useless <<-\EOF &&
	#!/bin/sh
	dump useless "$@"
	exit 0
	EOF
	chmod +x git-credential-useless &&

	cat >git-credential-verbatim <<-\EOF &&
	#!/bin/sh
	user=$1; shift
	pass=$1; shift
	dump verbatim "$@"
	test -z "$user" || echo username=$user
	test -z "$pass" || echo password=$pass
	EOF
	chmod +x git-credential-verbatim &&

	cat >askpass <<-\EOF &&
	#!/bin/sh
	echo >&2 askpass: $*
	echo askpass-result
	EOF
	chmod +x askpass &&
	GIT_ASKPASS=askpass &&
	export GIT_ASKPASS &&

	PATH="$PWD:$PATH"
'

test_expect_success 'credential_fill invokes helper' '
	check "verbatim foo bar" <<-\EOF
	username=foo
	password=bar
	--
	verbatim: <empty>
	EOF
'

test_expect_success 'credential_fill invokes multiple helpers' '
	check useless "verbatim foo bar" <<-\EOF
	username=foo
	password=bar
	--
	useless: <empty>
	verbatim: <empty>
	EOF
'

test_expect_success 'credential_fill stops when we get a full response' '
	check "verbatim one two" "verbatim three four" <<-\EOF
	username=one
	password=two
	--
	verbatim: <empty>
	EOF
'

test_expect_success 'credential_fill continues through partial response' '
	check "verbatim one \"\"" "verbatim two three" <<-\EOF
	username=two
	password=three
	--
	verbatim: <empty>
	verbatim: --username=one
	EOF
'

test_expect_success 'credential_fill passes along metadata' '
	check --description=foo --unique=bar "verbatim one two" <<-\EOF
	username=one
	password=two
	--
	verbatim: --description=foo
	verbatim: --unique=bar
	EOF
'

test_expect_success 'credential_reject calls all helpers' '
	check --reject --username=foo useless "verbatim one two" <<-\EOF
	--
	useless: --reject
	useless: --username=foo
	verbatim: --reject
	verbatim: --username=foo
	EOF
'

test_expect_success 'do not bother rejecting empty credential' '
	check --reject useless <<-\EOF
	--
	EOF
'

test_expect_success 'usernames can be preserved' '
	check --username=one "verbatim \"\" three" <<-\EOF
	username=one
	password=three
	--
	verbatim: --username=one
'

test_expect_success 'usernames can be overridden' '
	check --username=one "verbatim two three" <<-\EOF
	username=two
	password=three
	--
	verbatim: --username=one
	EOF
'

test_expect_success 'do not bother completing already-full credential' '
	check --username=one --password=two "verbatim three four" <<-\EOF
	username=one
	password=two
	--
	EOF
'

# We can't test the basic terminal password prompt here because
# getpass() tries too hard to find the real terminal. But if our
# askpass helper is run, we know the internal getpass is working.
test_expect_success 'empty methods falls back to internal getpass' '
	check <<-\EOF
	username=askpass-result
	password=askpass-result
	--
	askpass: Username:
	askpass: Password:
	EOF
'

test_expect_success 'internal getpass does not ask for known username' '
	check --username=foo <<-\EOF
	username=foo
	password=askpass-result
	--
	askpass: Password:
	EOF
'

test_expect_success 'internal getpass can pull from config' '
	git config credential.foo.username configured-username
	check --unique=foo <<-\EOF
	username=configured-username
	password=askpass-result
	--
	askpass: Password:
	EOF
'

test_expect_success 'credential-cache caches password' '
	test_when_finished "git credential-cache --exit" &&
	check --unique=host cache <<-\EOF &&
	username=askpass-result
	password=askpass-result
	--
	askpass: Username:
	askpass: Password:
	EOF
	check --unique=host cache <<-\EOF
	username=askpass-result
	password=askpass-result
	--
	EOF
'

test_expect_success 'credential-cache requires matching unique token' '
	test_when_finished "git credential-cache --exit" &&
	check --unique=host cache <<-\EOF &&
	username=askpass-result
	password=askpass-result
	--
	askpass: Username:
	askpass: Password:
	EOF
	check --unique=host2 cache <<-\EOF
	username=askpass-result
	password=askpass-result
	--
	askpass: Username:
	askpass: Password:
	EOF
'

test_expect_success 'credential-cache requires matching usernames' '
	test_when_finished "git credential-cache --exit" &&
	check --unique=host cache <<-\EOF &&
	username=askpass-result
	password=askpass-result
	--
	askpass: Username:
	askpass: Password:
	EOF
	check --unique=host --username=other cache <<-\EOF
	username=other
	password=askpass-result
	--
	askpass: Password:
	EOF
'

test_expect_success 'credential-cache times out' '
	test_when_finished "git credential-cache --exit || true" &&
	check --unique=host "cache --timeout=1" <<-\EOF &&
	username=askpass-result
	password=askpass-result
	--
	askpass: Username:
	askpass: Password:
	EOF
	sleep 2 &&
	check --unique=host cache <<-\EOF
	username=askpass-result
	password=askpass-result
	--
	askpass: Username:
	askpass: Password:
	EOF
'

test_expect_success 'credential-cache removes rejected credentials' '
	test_when_finished "git credential-cache --exit || true" &&
	check --unique=host cache <<-\EOF &&
	username=askpass-result
	password=askpass-result
	--
	askpass: Username:
	askpass: Password:
	EOF
	check --reject --unique=host --username=askpass-result cache <<-\EOF &&
	--
	EOF
	check --unique=host cache <<-\EOF
	username=askpass-result
	password=askpass-result
	--
	askpass: Username:
	askpass: Password:
	EOF
'

test_expect_success 'credential-store stores password' '
	test_when_finished "rm -f .git-credentials" &&
	check --unique=host store <<-\EOF &&
	username=askpass-result
	password=askpass-result
	--
	askpass: Username:
	askpass: Password:
	EOF
	check --unique=host store <<-\EOF
	username=askpass-result
	password=askpass-result
	--
	EOF
'

test_expect_success 'credential-store requires matching unique token' '
	test_when_finished "rm -f .git-credentials" &&
	check --unique=host store <<-\EOF &&
	username=askpass-result
	password=askpass-result
	--
	askpass: Username:
	askpass: Password:
	EOF
	check --unique=host2 store <<-\EOF
	username=askpass-result
	password=askpass-result
	--
	askpass: Username:
	askpass: Password:
	EOF
'

test_expect_success 'credential-store removes rejected credentials' '
	test_when_finished "rm -f .git-credentials" &&
	check --unique=host store <<-\EOF &&
	username=askpass-result
	password=askpass-result
	--
	askpass: Username:
	askpass: Password:
	EOF
	check --reject --unique=host --username=askpass-result store <<-\EOF &&
	--
	EOF
	check --unique=host store <<-\EOF
	username=askpass-result
	password=askpass-result
	--
	askpass: Username:
	askpass: Password:
	EOF
'

test_done
