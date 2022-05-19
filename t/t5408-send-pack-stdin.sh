#!/bin/sh

test_description='send-pack --stdin tests'
. ./test-lib.sh

create_ref () {
	tree=$(but write-tree) &&
	test_tick &&
	cummit=$(echo "$1" | but cummit-tree $tree) &&
	but update-ref "$1" $cummit
}

clear_remote () {
	rm -rf remote.but &&
	but init --bare remote.but
}

verify_push () {
	but rev-parse "$1" >expect &&
	but --but-dir=remote.but rev-parse "${2:-$1}" >actual &&
	test_cmp expect actual
}

test_expect_success 'setup refs' '
	cat >refs <<-\EOF &&
	refs/heads/A
	refs/heads/C
	refs/tags/D
	refs/heads/B
	refs/tags/E
	EOF
	for i in $(cat refs); do
		create_ref $i || return 1
	done
'

# sanity check our setup
test_expect_success 'refs on cmdline' '
	clear_remote &&
	but send-pack remote.but $(cat refs) &&
	for i in $(cat refs); do
		verify_push $i || return 1
	done
'

test_expect_success 'refs over stdin' '
	clear_remote &&
	but send-pack remote.but --stdin <refs &&
	for i in $(cat refs); do
		verify_push $i || return 1
	done
'

test_expect_success 'stdin lines are full refspecs' '
	clear_remote &&
	echo "A:other" >input &&
	but send-pack remote.but --stdin <input &&
	verify_push refs/heads/A refs/heads/other
'

test_expect_success 'stdin mixed with cmdline' '
	clear_remote &&
	echo A >input &&
	but send-pack remote.but --stdin B <input &&
	verify_push A &&
	verify_push B
'

test_expect_success 'cmdline refs written in order' '
	clear_remote &&
	test_must_fail but send-pack remote.but A:foo B:foo &&
	verify_push A foo
'

test_expect_success '--stdin refs come after cmdline' '
	clear_remote &&
	echo A:foo >input &&
	test_must_fail but send-pack remote.but --stdin B:foo <input &&
	verify_push B foo
'

test_expect_success 'refspecs and --mirror do not mix (cmdline)' '
	clear_remote &&
	test_must_fail but send-pack remote.but --mirror $(cat refs)
'

test_expect_success 'refspecs and --mirror do not mix (stdin)' '
	clear_remote &&
	test_must_fail but send-pack remote.but --mirror --stdin <refs
'

test_done
