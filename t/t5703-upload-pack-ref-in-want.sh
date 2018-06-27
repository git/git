#!/bin/sh

test_description='upload-pack ref-in-want'

. ./test-lib.sh

get_actual_refs () {
	sed -n -e '/wanted-refs/,/0001/{
		/wanted-refs/d
		/0001/d
		p
		}' <out | test-pkt-line unpack >actual_refs
}

get_actual_commits () {
	sed -n -e '/packfile/,/0000/{
		/packfile/d
		p
		}' <out | test-pkt-line unpack-sideband >o.pack &&
	git index-pack o.pack &&
	git verify-pack -v o.idx | grep commit | cut -c-40 | sort >actual_commits
}

check_output () {
	get_actual_refs &&
	test_cmp expected_refs actual_refs &&
	get_actual_commits &&
	test_cmp expected_commits actual_commits
}

# c(o/foo) d(o/bar)
#        \ /
#         b   e(baz)  f(master)
#          \__  |  __/
#             \ | /
#               a
test_expect_success 'setup repository' '
	test_commit a &&
	git checkout -b o/foo &&
	test_commit b &&
	test_commit c &&
	git checkout -b o/bar b &&
	test_commit d &&
	git checkout -b baz a &&
	test_commit e &&
	git checkout master &&
	test_commit f
'

test_expect_success 'config controls ref-in-want advertisement' '
	git serve --advertise-capabilities >out &&
	! grep -a ref-in-want out &&

	git config uploadpack.allowRefInWant false &&
	git serve --advertise-capabilities >out &&
	! grep -a ref-in-want out &&

	git config uploadpack.allowRefInWant true &&
	git serve --advertise-capabilities >out &&
	grep -a ref-in-want out
'

test_expect_success 'invalid want-ref line' '
	test-pkt-line pack >in <<-EOF &&
	command=fetch
	0001
	no-progress
	want-ref refs/heads/non-existent
	done
	0000
	EOF

	test_must_fail git serve --stateless-rpc 2>out <in &&
	grep "unknown ref" out
'

test_expect_success 'basic want-ref' '
	cat >expected_refs <<-EOF &&
	$(git rev-parse f) refs/heads/master
	EOF
	git rev-parse f | sort >expected_commits &&

	test-pkt-line pack >in <<-EOF &&
	command=fetch
	0001
	no-progress
	want-ref refs/heads/master
	have $(git rev-parse a)
	done
	0000
	EOF

	git serve --stateless-rpc >out <in &&
	check_output
'

test_expect_success 'multiple want-ref lines' '
	cat >expected_refs <<-EOF &&
	$(git rev-parse c) refs/heads/o/foo
	$(git rev-parse d) refs/heads/o/bar
	EOF
	git rev-parse c d | sort >expected_commits &&

	test-pkt-line pack >in <<-EOF &&
	command=fetch
	0001
	no-progress
	want-ref refs/heads/o/foo
	want-ref refs/heads/o/bar
	have $(git rev-parse b)
	done
	0000
	EOF

	git serve --stateless-rpc >out <in &&
	check_output
'

test_expect_success 'mix want and want-ref' '
	cat >expected_refs <<-EOF &&
	$(git rev-parse f) refs/heads/master
	EOF
	git rev-parse e f | sort >expected_commits &&

	test-pkt-line pack >in <<-EOF &&
	command=fetch
	0001
	no-progress
	want-ref refs/heads/master
	want $(git rev-parse e)
	have $(git rev-parse a)
	done
	0000
	EOF

	git serve --stateless-rpc >out <in &&
	check_output
'

test_expect_success 'want-ref with ref we already have commit for' '
	cat >expected_refs <<-EOF &&
	$(git rev-parse c) refs/heads/o/foo
	EOF
	>expected_commits &&

	test-pkt-line pack >in <<-EOF &&
	command=fetch
	0001
	no-progress
	want-ref refs/heads/o/foo
	have $(git rev-parse c)
	done
	0000
	EOF

	git serve --stateless-rpc >out <in &&
	check_output
'

test_done
