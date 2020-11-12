test_expect_success "setup proc-receive hook (unknown version, $PROTOCOL/porcelain)" '
	write_script "$upstream/hooks/proc-receive" <<-EOF
	printf >&2 "# proc-receive hook\n"
	test-tool proc-receive -v --version 2
	EOF
'

# Refs of upstream : main(A)
# Refs of workbench: main(A)  tags/v123
# git push         :                       refs/for/main/topic(A)
test_expect_success "proc-receive: bad protocol (unknown version, $PROTOCOL/porcelain)" '
	test_must_fail git -C workbench push --porcelain origin \
		HEAD:refs/for/main/topic \
		>out 2>&1 &&
	make_user_friendly_and_stable_output <out >actual &&

	# Check status report for git-push
	sed -n \
		-e "/^To / { p; n; p; n; p; }" \
		<actual >actual-report &&
	cat >expect <<-EOF &&
	To <URL/of/upstream.git>
	!    HEAD:refs/for/main/topic    [remote rejected] (fail to run proc-receive hook)
	Done
	EOF
	test_cmp expect actual-report &&

	# Check error message from "receive-pack", but ignore unstable fatal error
	# message ("remote: fatal: the remote end hung up unexpectedly") which
	# is different from the remote HTTP server with different locale settings.
	grep "^remote: error:" <actual >actual-error &&
	cat >expect <<-EOF &&
	remote: error: proc-receive version "2" is not supported
	EOF
	test_cmp expect actual-error &&

	git -C "$upstream" show-ref >out &&
	make_user_friendly_and_stable_output <out >actual &&
	cat >expect <<-EOF &&
	<COMMIT-A> refs/heads/main
	EOF
	test_cmp expect actual
'

test_expect_success "setup proc-receive hook (no report, $PROTOCOL/porcelain)" '
	write_script "$upstream/hooks/proc-receive" <<-EOF
	printf >&2 "# proc-receive hook\n"
	test-tool proc-receive -v
	EOF
'

# Refs of upstream : main(A)
# Refs of workbench: main(A)  tags/v123
# git push         :                       next(A)  refs/for/main/topic(A)
test_expect_success "proc-receive: bad protocol (no report, $PROTOCOL/porcelain)" '
	test_must_fail git -C workbench push --porcelain origin \
		HEAD:refs/heads/next \
		HEAD:refs/for/main/topic >out 2>&1 &&
	make_user_friendly_and_stable_output <out >actual &&
	cat >expect <<-EOF &&
	remote: # pre-receive hook
	remote: pre-receive< <ZERO-OID> <COMMIT-A> refs/heads/next
	remote: pre-receive< <ZERO-OID> <COMMIT-A> refs/for/main/topic
	remote: # proc-receive hook
	remote: proc-receive< <ZERO-OID> <COMMIT-A> refs/for/main/topic
	remote: # post-receive hook
	remote: post-receive< <ZERO-OID> <COMMIT-A> refs/heads/next
	To <URL/of/upstream.git>
	*    HEAD:refs/heads/next    [new branch]
	!    HEAD:refs/for/main/topic    [remote rejected] (proc-receive failed to report status)
	Done
	EOF
	test_cmp expect actual &&
	git -C "$upstream" show-ref >out &&
	make_user_friendly_and_stable_output <out >actual &&
	cat >expect <<-EOF &&
	<COMMIT-A> refs/heads/main
	<COMMIT-A> refs/heads/next
	EOF
	test_cmp expect actual
'

# Refs of upstream : main(A)             next(A)
# Refs of workbench: main(A)  tags/v123
test_expect_success "cleanup ($PROTOCOL/porcelain)" '
	git -C "$upstream" update-ref -d refs/heads/next

'

test_expect_success "setup proc-receive hook (no ref, $PROTOCOL/porcelain)" '
	write_script "$upstream/hooks/proc-receive" <<-EOF
	printf >&2 "# proc-receive hook\n"
	test-tool proc-receive -v \
		-r "ok"
	EOF
'

# Refs of upstream : main(A)
# Refs of workbench: main(A)  tags/v123
# git push         :                       refs/for/main/topic
test_expect_success "proc-receive: bad protocol (no ref, $PROTOCOL/porcelain)" '
	test_must_fail git -C workbench push --porcelain origin \
		HEAD:refs/for/main/topic\
		>out 2>&1 &&
	make_user_friendly_and_stable_output <out >actual &&
	cat >expect <<-EOF &&
	remote: # pre-receive hook
	remote: pre-receive< <ZERO-OID> <COMMIT-A> refs/for/main/topic
	remote: # proc-receive hook
	remote: proc-receive< <ZERO-OID> <COMMIT-A> refs/for/main/topic
	remote: proc-receive> ok
	remote: error: proc-receive reported incomplete status line: "ok"
	To <URL/of/upstream.git>
	!    HEAD:refs/for/main/topic    [remote rejected] (proc-receive failed to report status)
	Done
	EOF
	test_cmp expect actual &&
	git -C "$upstream" show-ref >out &&
	make_user_friendly_and_stable_output <out >actual &&
	cat >expect <<-EOF &&
	<COMMIT-A> refs/heads/main
	EOF
	test_cmp expect actual
'

test_expect_success "setup proc-receive hook (unknown status, $PROTOCOL/porcelain)" '
	write_script "$upstream/hooks/proc-receive" <<-EOF
	printf >&2 "# proc-receive hook\n"
	test-tool proc-receive -v \
		-r "xx refs/for/main/topic"
	EOF
'

# Refs of upstream : main(A)
# Refs of workbench: main(A)  tags/v123
# git push         :                       refs/for/main/topic
test_expect_success "proc-receive: bad protocol (unknown status, $PROTOCOL/porcelain)" '
	test_must_fail git -C workbench push --porcelain origin \
			HEAD:refs/for/main/topic \
			>out 2>&1 &&
	make_user_friendly_and_stable_output <out >actual &&
	cat >expect <<-EOF &&
	remote: # pre-receive hook
	remote: pre-receive< <ZERO-OID> <COMMIT-A> refs/for/main/topic
	remote: # proc-receive hook
	remote: proc-receive< <ZERO-OID> <COMMIT-A> refs/for/main/topic
	remote: proc-receive> xx refs/for/main/topic
	remote: error: proc-receive reported bad status "xx" on ref "refs/for/main/topic"
	To <URL/of/upstream.git>
	!    HEAD:refs/for/main/topic    [remote rejected] (proc-receive failed to report status)
	Done
	EOF
	test_cmp expect actual &&
	git -C "$upstream" show-ref >out &&
	make_user_friendly_and_stable_output <out >actual &&
	cat >expect <<-EOF &&
	<COMMIT-A> refs/heads/main
	EOF
	test_cmp expect actual
'
