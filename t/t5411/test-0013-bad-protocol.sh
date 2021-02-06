test_expect_success "setup proc-receive hook (unknown version, $PROTOCOL)" '
	write_script "$upstream/hooks/proc-receive" <<-EOF
	printf >&2 "# proc-receive hook\n"
	test-tool proc-receive -v --version 2
	EOF
'

# Refs of upstream : main(A)
# Refs of workbench: main(A)  tags/v123
# git push         :                       refs/for/main/topic(A)
test_expect_success "proc-receive: bad protocol (unknown version, $PROTOCOL)" '
	test_must_fail git -C workbench push origin \
		HEAD:refs/for/main/topic \
		>out 2>&1 &&
	make_user_friendly_and_stable_output <out >actual &&

	# Check status report for git-push
	sed -n \
		-e "/^To / { p; }" \
		-e "/^ ! / { p; }" \
		<actual >actual-report &&
	cat >expect <<-EOF &&
	To <URL/of/upstream.git>
	 ! [remote rejected] HEAD -> refs/for/main/topic (fail to run proc-receive hook)
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

test_expect_success "setup proc-receive hook (hook --die-read-version, $PROTOCOL)" '
	write_script "$upstream/hooks/proc-receive" <<-EOF
	printf >&2 "# proc-receive hook\n"
	test-tool proc-receive -v --die-read-version
	EOF
'

# Refs of upstream : main(A)
# Refs of workbench: main(A)  tags/v123
# git push         :                       refs/for/main/topic(A)
test_expect_success "proc-receive: bad protocol (hook --die-read-version, $PROTOCOL)" '
	test_must_fail git -C workbench push origin \
		HEAD:refs/for/main/topic \
		>out 2>&1 &&
	filter_out_user_friendly_and_stable_output \
		-e "/^To / { p; }" \
		-e "/^ ! / { p; }" \
		<out >actual &&
	cat >expect <<-EOF &&
	To <URL/of/upstream.git>
	 ! [remote rejected] HEAD -> refs/for/main/topic (fail to run proc-receive hook)
	EOF
	test_cmp expect actual &&
	grep "remote: fatal: die with the --die-read-version option" out &&
	grep "remote: error: fail to negotiate version with proc-receive hook" out &&

	git -C "$upstream" show-ref >out &&
	make_user_friendly_and_stable_output <out >actual &&
	cat >expect <<-EOF &&
	<COMMIT-A> refs/heads/main
	EOF
	test_cmp expect actual
'

test_expect_success "setup proc-receive hook (hook --die-write-version, $PROTOCOL)" '
	write_script "$upstream/hooks/proc-receive" <<-EOF
	printf >&2 "# proc-receive hook\n"
	test-tool proc-receive -v --die-write-version
	EOF
'

# Refs of upstream : main(A)
# Refs of workbench: main(A)  tags/v123
# git push         :                       refs/for/main/topic(A)
test_expect_success "proc-receive: bad protocol (hook --die-write-version, $PROTOCOL)" '
	test_must_fail git -C workbench push origin \
		HEAD:refs/for/main/topic \
		>out 2>&1 &&
	filter_out_user_friendly_and_stable_output \
		-e "/^To / { p; }" \
		-e "/^ ! / { p; }" \
		<out >actual &&
	cat >expect <<-EOF &&
	To <URL/of/upstream.git>
	 ! [remote rejected] HEAD -> refs/for/main/topic (fail to run proc-receive hook)
	EOF
	test_cmp expect actual &&
	grep "remote: fatal: die with the --die-write-version option" out &&
	grep "remote: error: fail to negotiate version with proc-receive hook" out &&

	git -C "$upstream" show-ref >out &&
	make_user_friendly_and_stable_output <out >actual &&
	cat >expect <<-EOF &&
	<COMMIT-A> refs/heads/main
	EOF
	test_cmp expect actual
'

test_expect_success "setup proc-receive hook (hook --die-read-commands, $PROTOCOL)" '
	write_script "$upstream/hooks/proc-receive" <<-EOF
	printf >&2 "# proc-receive hook\n"
	test-tool proc-receive -v --die-read-commands
	EOF
'

# Refs of upstream : main(A)
# Refs of workbench: main(A)  tags/v123
# git push         :                       refs/for/main/topic(A)
test_expect_success "proc-receive: bad protocol (hook --die-read-commands, $PROTOCOL)" '
	test_must_fail git -C workbench push origin \
		HEAD:refs/for/main/topic \
		>out 2>&1 &&
	filter_out_user_friendly_and_stable_output \
		-e "/^To / { p; }" \
		-e "/^ ! / { p; }" \
		<out >actual &&
	cat >expect <<-EOF &&
	To <URL/of/upstream.git>
	 ! [remote rejected] HEAD -> refs/for/main/topic (fail to run proc-receive hook)
	EOF
	test_cmp expect actual &&
	grep "remote: fatal: die with the --die-read-commands option" out &&

	git -C "$upstream" show-ref >out &&
	make_user_friendly_and_stable_output <out >actual &&
	cat >expect <<-EOF &&
	<COMMIT-A> refs/heads/main
	EOF
	test_cmp expect actual
'

test_expect_success "setup proc-receive hook (hook --die-read-push-options, $PROTOCOL)" '
	write_script "$upstream/hooks/proc-receive" <<-EOF
	printf >&2 "# proc-receive hook\n"
	test-tool proc-receive -v --die-read-push-options
	EOF
'

# Refs of upstream : main(A)
# Refs of workbench: main(A)  tags/v123
# git push         :                       refs/for/main/topic(A)
test_expect_success "proc-receive: bad protocol (hook --die-read-push-options, $PROTOCOL)" '
	git -C "$upstream" config receive.advertisePushOptions true &&
	test_must_fail git -C workbench push origin \
		-o reviewers=user1,user2 \
		HEAD:refs/for/main/topic \
		>out 2>&1 &&
	filter_out_user_friendly_and_stable_output \
		-e "/^To / { p; }" \
		-e "/^ ! / { p; }" \
		<out >actual &&
	cat >expect <<-EOF &&
	To <URL/of/upstream.git>
	 ! [remote rejected] HEAD -> refs/for/main/topic (fail to run proc-receive hook)
	EOF
	test_cmp expect actual &&
	grep "remote: fatal: die with the --die-read-push-options option" out &&

	git -C "$upstream" show-ref >out &&
	make_user_friendly_and_stable_output <out >actual &&
	cat >expect <<-EOF &&
	<COMMIT-A> refs/heads/main
	EOF
	test_cmp expect actual
'

test_expect_success "setup proc-receive hook (hook --die-write-report, $PROTOCOL)" '
	write_script "$upstream/hooks/proc-receive" <<-EOF
	printf >&2 "# proc-receive hook\n"
	test-tool proc-receive -v --die-write-report
	EOF
'

# Refs of upstream : main(A)
# Refs of workbench: main(A)  tags/v123
# git push         :                       refs/for/main/topic(A)
test_expect_success "proc-receive: bad protocol (hook --die-write-report, $PROTOCOL)" '
	test_must_fail git -C workbench push origin \
		HEAD:refs/for/main/topic \
		>out 2>&1 &&
	filter_out_user_friendly_and_stable_output \
		-e "/^To / { p; }" \
		-e "/^ ! / { p; }" \
		<out >actual &&
	cat >expect <<-EOF &&
	To <URL/of/upstream.git>
	 ! [remote rejected] HEAD -> refs/for/main/topic (fail to run proc-receive hook)
	EOF
	test_cmp expect actual &&
	grep "remote: fatal: die with the --die-write-report option" out &&

	git -C "$upstream" show-ref >out &&
	make_user_friendly_and_stable_output <out >actual &&
	cat >expect <<-EOF &&
	<COMMIT-A> refs/heads/main
	EOF
	test_cmp expect actual
'

test_expect_success "setup proc-receive hook (no report, $PROTOCOL)" '
	write_script "$upstream/hooks/proc-receive" <<-EOF
	printf >&2 "# proc-receive hook\n"
	test-tool proc-receive -v
	EOF
'

# Refs of upstream : main(A)
# Refs of workbench: main(A)  tags/v123
# git push         :                       next(A)  refs/for/main/topic(A)
test_expect_success "proc-receive: bad protocol (no report, $PROTOCOL)" '
	test_must_fail git -C workbench push origin \
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
	 * [new branch] HEAD -> next
	 ! [remote rejected] HEAD -> refs/for/main/topic (proc-receive failed to report status)
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
test_expect_success "cleanup ($PROTOCOL)" '
	git -C "$upstream" update-ref -d refs/heads/next

'

test_expect_success "setup proc-receive hook (no ref, $PROTOCOL)" '
	write_script "$upstream/hooks/proc-receive" <<-EOF
	printf >&2 "# proc-receive hook\n"
	test-tool proc-receive -v \
		-r "ok"
	EOF
'

# Refs of upstream : main(A)
# Refs of workbench: main(A)  tags/v123
# git push         :                       refs/for/main/topic
test_expect_success "proc-receive: bad protocol (no ref, $PROTOCOL)" '
	test_must_fail git -C workbench push origin \
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
	 ! [remote rejected] HEAD -> refs/for/main/topic (proc-receive failed to report status)
	EOF
	test_cmp expect actual &&

	git -C "$upstream" show-ref >out &&
	make_user_friendly_and_stable_output <out >actual &&
	cat >expect <<-EOF &&
	<COMMIT-A> refs/heads/main
	EOF
	test_cmp expect actual
'

test_expect_success "setup proc-receive hook (unknown status, $PROTOCOL)" '
	write_script "$upstream/hooks/proc-receive" <<-EOF
	printf >&2 "# proc-receive hook\n"
	test-tool proc-receive -v \
		-r "xx refs/for/main/topic"
	EOF
'

# Refs of upstream : main(A)
# Refs of workbench: main(A)  tags/v123
# git push         :                       refs/for/main/topic
test_expect_success "proc-receive: bad protocol (unknown status, $PROTOCOL)" '
	test_must_fail git -C workbench push origin \
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
	 ! [remote rejected] HEAD -> refs/for/main/topic (proc-receive failed to report status)
	EOF
	test_cmp expect actual &&

	git -C "$upstream" show-ref >out &&
	make_user_friendly_and_stable_output <out >actual &&
	cat >expect <<-EOF &&
	<COMMIT-A> refs/heads/main
	EOF
	test_cmp expect actual
'
