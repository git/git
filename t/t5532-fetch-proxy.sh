#!/bin/sh

test_description='fetching via but:// using core.butproxy'
. ./test-lib.sh

test_expect_success 'setup remote repo' '
	but init remote &&
	(cd remote &&
	 echo content >file &&
	 but add file &&
	 but cummit -m one
	)
'

test_expect_success 'setup proxy script' '
	write_script proxy-get-cmd "$PERL_PATH" <<-\EOF &&
	read(STDIN, $buf, 4);
	my $n = hex($buf) - 4;
	read(STDIN, $buf, $n);
	my ($cmd, $other) = split /\0/, $buf;
	# drop absolute-path on repo name
	$cmd =~ s{ /}{ };
	print $cmd;
	EOF

	write_script proxy <<-\EOF
	echo >&2 "proxying for $*"
	cmd=$(./proxy-get-cmd)
	echo >&2 "Running $cmd"
	exec $cmd
	EOF
'

test_expect_success 'setup local repo' '
	but remote add fake but://example.com/remote &&
	but config core.butproxy ./proxy
'

test_expect_success 'fetch through proxy works' '
	but fetch fake &&
	echo one >expect &&
	but log -1 --format=%s FETCH_HEAD >actual &&
	test_cmp expect actual
'

test_expect_success 'funny hostnames are rejected before running proxy' '
	test_must_fail but fetch but://-remote/repo.but 2>stderr &&
	! grep "proxying for" stderr
'

test_done
