#!/bin/sh

test_description='check that the most basic functions work


Verify wrappers and compatibility functions.
'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

test_expect_success 'mktemp to nonexistent directory prints filename' '
	test_must_fail test-tool mktemp doesnotexist/testXXXXXX 2>err &&
	grep "doesnotexist/test" err
'

test_expect_success POSIXPERM,SANITY 'mktemp to unwritable directory prints filename' '
	mkdir cannotwrite &&
	test_when_finished "chmod +w cannotwrite" &&
	chmod -w cannotwrite &&
	test_must_fail test-tool mktemp cannotwrite/testXXXXXX 2>err &&
	grep "cannotwrite/test" err
'

test_expect_success 'git_mkstemps_mode does not fail if fd 0 is not open' '
	git commit --allow-empty -m message <&-
'

test_expect_success 'check for a bug in the regex routines' '
	# if this test fails, re-build git with NO_REGEX=1
	test-tool regex --bug
'

test_expect_success 'incomplete sideband messages are reassembled' '
	test-tool pkt-line send-split-sideband >split-sideband &&
	test-tool pkt-line receive-sideband <split-sideband 2>err &&
	grep "Hello, world" err
'

test_expect_success 'eof on sideband message is reported' '
	printf 1234 >input &&
	test-tool pkt-line receive-sideband <input 2>err &&
	test_grep "unexpected disconnect" err
'

test_expect_success 'missing sideband designator is reported' '
	printf 0004 >input &&
	test-tool pkt-line receive-sideband <input 2>err &&
	test_grep "missing sideband" err
'

test_expect_success 'unpack-sideband: --no-chomp-newline' '
	test_when_finished "rm -f expect-out expect-err" &&
	test-tool pkt-line send-split-sideband >split-sideband &&
	test-tool pkt-line unpack-sideband \
		--no-chomp-newline <split-sideband >out 2>err &&
	cat >expect-out <<-EOF &&
		primary: regular output
	EOF
	cat >expect-err <<-EOF &&
		Foo.
		Bar.
		Hello, world!
	EOF
	test_cmp expect-out out &&
	test_cmp expect-err err
'

test_expect_success 'unpack-sideband: --chomp-newline (default)' '
	test_when_finished "rm -f expect-out expect-err" &&
	test-tool pkt-line send-split-sideband >split-sideband &&
	test-tool pkt-line unpack-sideband \
		--chomp-newline <split-sideband >out 2>err &&
	printf "primary: regular output" >expect-out &&
	printf "Foo.Bar.Hello, world!" >expect-err &&
	test_cmp expect-out out &&
	test_cmp expect-err err
'

test_expect_success 'unpack-sideband: packet_reader_read() consumes sideband, no chomp payload' '
	test_when_finished "rm -f expect-out expect-err" &&
	test-tool pkt-line send-split-sideband >split-sideband &&
	test-tool pkt-line unpack-sideband \
		--reader-use-sideband \
		--no-chomp-newline <split-sideband >out 2>err &&
	cat >expect-out <<-EOF &&
		primary: regular output
	EOF
	printf "remote: Foo.        \n"           >expect-err &&
	printf "remote: Bar.        \n"          >>expect-err &&
	printf "remote: Hello, world!        \n" >>expect-err &&
	test_cmp expect-out out &&
	test_cmp expect-err err
'

test_expect_success 'unpack-sideband: packet_reader_read() consumes sideband, chomp payload' '
	test_when_finished "rm -f expect-out expect-err" &&
	test-tool pkt-line send-split-sideband >split-sideband &&
	test-tool pkt-line unpack-sideband \
		--reader-use-sideband \
		--chomp-newline <split-sideband >out 2>err &&
	printf "primary: regular output" >expect-out &&
	printf "remote: Foo.        \n"           >expect-err &&
	printf "remote: Bar.        \n"          >>expect-err &&
	printf "remote: Hello, world!        \n" >>expect-err &&
	test_cmp expect-out out &&
	test_cmp expect-err err
'

test_done
