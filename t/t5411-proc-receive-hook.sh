#!/bin/sh
#
# Copyright (c) 2020 Jiang Xin
#

test_description='Test proc-receive hook'

BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

. "$TEST_DIRECTORY"/t5411/common-functions.sh

setup_upstream_and_workbench () {
	# Refs of upstream : main(A)
	# Refs of workbench: main(A)  tags/v123
	test_expect_success "setup upstream and workbench" '
		rm -rf upstream.but &&
		rm -rf workbench &&
		but init --bare upstream.but &&
		but init workbench &&
		create_cummits_in workbench A B &&
		(
			cd workbench &&
			# Try to make a stable fixed width for abbreviated cummit ID,
			# this fixed-width oid will be replaced with "<OID>".
			but config core.abbrev 7 &&
			but tag -m "v123" v123 $A &&
			but remote add origin ../upstream.but &&
			but push origin main &&
			but update-ref refs/heads/main $A $B &&
			but -C ../upstream.but update-ref \
				refs/heads/main $A $B
		) &&
		TAG=$(but -C workbench rev-parse v123) &&

		# setup pre-receive hook
		test_hook --setup -C upstream.but pre-receive <<-\EOF &&
		exec >&2
		echo "# pre-receive hook"
		while read old new ref
		do
			echo "pre-receive< $old $new $ref"
		done
		EOF

		# setup post-receive hook
		test_hook --setup -C upstream.but post-receive <<-\EOF &&
		exec >&2
		echo "# post-receive hook"
		while read old new ref
		do
			echo "post-receive< $old $new $ref"
		done
		EOF

		upstream=upstream.but
	'
}

run_proc_receive_hook_test() {
	case $1 in
	http)
		PROTOCOL="HTTP protocol"
		URL_PREFIX="http://.*"
		;;
	local)
		PROTOCOL="builtin protocol"
		URL_PREFIX="\.\."
		;;
	esac

	# Include test cases for both file and HTTP protocol
	for t in  "$TEST_DIRECTORY"/t5411/test-*.sh
	do
		. "$t"
	done
}

# Initialize the upstream repository and local workbench.
setup_upstream_and_workbench

# Load test cases that only need to be executed once.
for t in  "$TEST_DIRECTORY"/t5411/once-*.sh
do
	. "$t"
done

# Initialize the upstream repository and local workbench.
setup_upstream_and_workbench

# Run test cases for 'proc-receive' hook on local file protocol.
run_proc_receive_hook_test local

ROOT_PATH="$PWD"
. "$TEST_DIRECTORY"/lib-gpg.sh
. "$TEST_DIRECTORY"/lib-httpd.sh
. "$TEST_DIRECTORY"/lib-terminal.sh
start_httpd

# Re-initialize the upstream repository and local workbench.
setup_upstream_and_workbench

# Refs of upstream : main(A)
# Refs of workbench: main(A)  tags/v123
test_expect_success "setup for HTTP protocol" '
	but -C upstream.but config http.receivepack true &&
	upstream="$HTTPD_DOCUMENT_ROOT_PATH/upstream.but" &&
	mv upstream.but "$upstream" &&
	but -C workbench remote set-url origin "$HTTPD_URL/auth-push/smart/upstream.but" &&
	set_askpass user@host pass@host
'

setup_askpass_helper

# Run test cases for 'proc-receive' hook on HTTP protocol.
run_proc_receive_hook_test http

test_done
