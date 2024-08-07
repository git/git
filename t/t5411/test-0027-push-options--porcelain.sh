test_expect_success "setup proc-receive hook and disable push-options ($PROTOCOL/porcelain)" '
	git -C "$upstream" config receive.advertisePushOptions false &&
	test_hook -C "$upstream" --clobber proc-receive <<-\EOF
	printf >&2 "# proc-receive hook\n"
	test-tool proc-receive -v \
		-r "ok refs/for/main/topic"
	EOF
'

# Refs of upstream : main(A)
# Refs of workbench: main(A)  tags/v123
# git push -o ...  :                       refs/for/main/topic
test_expect_success "proc-receive: not support push options ($PROTOCOL/porcelain)" '
	test_must_fail git -C workbench push \
		--porcelain \
		-o issue=123 \
		-o reviewer=user1 \
		origin \
		HEAD:refs/for/main/topic \
		>out-$test_count 2>&1 &&
	make_user_friendly_and_stable_output <out-$test_count >actual &&
	test_grep "fatal: the receiving end does not support push options" \
		actual &&

	test_cmp_refs -C "$upstream" <<-EOF
	<COMMIT-A> refs/heads/main
	EOF
'

test_expect_success "enable push options ($PROTOCOL/porcelain)" '
	git -C "$upstream" config receive.advertisePushOptions true
'

test_expect_success "setup version=0 for proc-receive hook ($PROTOCOL/porcelain)" '
	test_hook -C "$upstream" --clobber proc-receive <<-\EOF
	printf >&2 "# proc-receive hook\n"
	test-tool proc-receive -v \
		--version 0 \
		-r "ok refs/for/main/topic"
	EOF
'

# Refs of upstream : main(A)
# Refs of workbench: main(A)  tags/v123
# git push -o ...  :                       next(A)  refs/for/main/topic
test_expect_success "proc-receive: ignore push-options for version 0 ($PROTOCOL/porcelain)" '
	git -C workbench push \
		--porcelain \
		--atomic \
		-o issue=123 \
		-o reviewer=user1 \
		origin \
		HEAD:refs/heads/next \
		HEAD:refs/for/main/topic \
		>out 2>&1 &&
	make_user_friendly_and_stable_output <out >actual &&
	format_and_save_expect <<-EOF &&
	> remote: # pre-receive hook        Z
	> remote: pre-receive< <ZERO-OID> <COMMIT-A> refs/heads/next        Z
	> remote: pre-receive< <ZERO-OID> <COMMIT-A> refs/for/main/topic        Z
	> remote: # proc-receive hook        Z
	> remote: proc-receive< <ZERO-OID> <COMMIT-A> refs/for/main/topic        Z
	> remote: proc-receive> ok refs/for/main/topic        Z
	> remote: # post-receive hook        Z
	> remote: post-receive< <ZERO-OID> <COMMIT-A> refs/heads/next        Z
	> remote: post-receive< <ZERO-OID> <COMMIT-A> refs/for/main/topic        Z
	> To <URL/of/upstream.git>
	> *	HEAD:refs/heads/next	[new branch]
	> *	HEAD:refs/for/main/topic	[new reference]
	> Done
	EOF
	test_cmp expect actual &&

	test_cmp_refs -C "$upstream" <<-EOF
	<COMMIT-A> refs/heads/main
	<COMMIT-A> refs/heads/next
	EOF
'

test_expect_success "restore proc-receive hook ($PROTOCOL/porcelain)" '
	test_hook -C "$upstream" --clobber proc-receive <<-\EOF
	printf >&2 "# proc-receive hook\n"
	test-tool proc-receive -v \
		-r "ok refs/for/main/topic"
	EOF
'

# Refs of upstream : main(A)             next(A)
# Refs of workbench: main(A)  tags/v123
test_expect_success "cleanup ($PROTOCOL/porcelain)" '
	git -C "$upstream" update-ref -d refs/heads/next
'

# Refs of upstream : main(A)
# Refs of workbench: main(A)  tags/v123
# git push -o ...  :                       next(A)  refs/for/main/topic
test_expect_success "proc-receive: push with options ($PROTOCOL/porcelain)" '
	git -C workbench push \
		--porcelain \
		--atomic \
		-o issue=123 \
		-o reviewer=user1 \
		origin \
		HEAD:refs/heads/next \
		HEAD:refs/for/main/topic \
		>out 2>&1 &&
	make_user_friendly_and_stable_output <out >actual &&
	format_and_save_expect <<-EOF &&
	> remote: # pre-receive hook        Z
	> remote: pre-receive< <ZERO-OID> <COMMIT-A> refs/heads/next        Z
	> remote: pre-receive< <ZERO-OID> <COMMIT-A> refs/for/main/topic        Z
	> remote: # proc-receive hook        Z
	> remote: proc-receive: atomic push_options        Z
	> remote: proc-receive< <ZERO-OID> <COMMIT-A> refs/for/main/topic        Z
	> remote: proc-receive< issue=123        Z
	> remote: proc-receive< reviewer=user1        Z
	> remote: proc-receive> ok refs/for/main/topic        Z
	> remote: # post-receive hook        Z
	> remote: post-receive< <ZERO-OID> <COMMIT-A> refs/heads/next        Z
	> remote: post-receive< <ZERO-OID> <COMMIT-A> refs/for/main/topic        Z
	> To <URL/of/upstream.git>
	> *	HEAD:refs/heads/next	[new branch]
	> *	HEAD:refs/for/main/topic	[new reference]
	> Done
	EOF
	test_cmp expect actual &&

	test_cmp_refs -C "$upstream" <<-EOF
	<COMMIT-A> refs/heads/main
	<COMMIT-A> refs/heads/next
	EOF
'

# Refs of upstream : main(A)             next(A)
# Refs of workbench: main(A)  tags/v123
test_expect_success "cleanup ($PROTOCOL/porcelain)" '
	git -C "$upstream" update-ref -d refs/heads/next
'
