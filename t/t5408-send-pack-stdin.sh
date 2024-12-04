#!/bin/sh

test_description='send-pack --stdin tests'

. ./test-lib.sh

create_ref () {
	tree=$(git write-tree) &&
	test_tick &&
	commit=$(echo "$1" | git commit-tree $tree) &&
	git update-ref "$1" $commit
}

clear_remote () {
	rm -rf remote.git &&
	git init --bare remote.git
}

verify_push () {
	git rev-parse "$1" >expect &&
	git --git-dir=remote.git rev-parse "${2:-$1}" >actual &&
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
	git send-pack remote.git $(cat refs) &&
	for i in $(cat refs); do
		verify_push $i || return 1
	done
'

test_expect_success 'refs over stdin' '
	clear_remote &&
	git send-pack remote.git --stdin <refs &&
	for i in $(cat refs); do
		verify_push $i || return 1
	done
'

test_expect_success 'stdin lines are full refspecs' '
	clear_remote &&
	echo "A:other" >input &&
	git send-pack remote.git --stdin <input &&
	verify_push refs/heads/A refs/heads/other
'

test_expect_success 'stdin mixed with cmdline' '
	clear_remote &&
	echo A >input &&
	git send-pack remote.git --stdin B <input &&
	verify_push A &&
	verify_push B
'

test_expect_success 'cmdline refs written in order' '
	clear_remote &&
	test_must_fail git send-pack remote.git A:foo B:foo &&
	verify_push A foo
'

test_expect_success '--stdin refs come after cmdline' '
	clear_remote &&
	echo A:foo >input &&
	test_must_fail git send-pack remote.git --stdin B:foo <input &&
	verify_push B foo
'

test_expect_success 'refspecs and --mirror do not mix (cmdline)' '
	clear_remote &&
	test_must_fail git send-pack remote.git --mirror $(cat refs)
'

test_expect_success 'refspecs and --mirror do not mix (stdin)' '
	clear_remote &&
	test_must_fail git send-pack remote.git --mirror --stdin <refs
'

test_done
