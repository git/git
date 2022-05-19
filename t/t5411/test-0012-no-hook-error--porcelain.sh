# Refs of upstream : main(A)
# Refs of workbench: main(A)  tags/v123
# but push         :                       next(A)  refs/for/main/topic(A)
test_expect_success "proc-receive: no hook, fail to push special ref ($PROTOCOL/porcelain)" '
	test_must_fail but -C workbench push --porcelain origin \
		HEAD:next \
		HEAD:refs/for/main/topic \
		>out-$test_count 2>&1 &&
	make_user_friendly_and_stable_output <out-$test_count >actual &&
	format_and_save_expect <<-EOF &&
	> remote: # pre-receive hook        Z
	> remote: pre-receive< <ZERO-OID> <CUMMIT-A> refs/heads/next        Z
	> remote: pre-receive< <ZERO-OID> <CUMMIT-A> refs/for/main/topic        Z
	> remote: error: cannot find hook "proc-receive"        Z
	> remote: # post-receive hook        Z
	> remote: post-receive< <ZERO-OID> <CUMMIT-A> refs/heads/next        Z
	> To <URL/of/upstream.but>
	> *	HEAD:refs/heads/next	[new branch]
	> !	HEAD:refs/for/main/topic	[remote rejected] (fail to run proc-receive hook)
	Done
	EOF
	test_cmp expect actual &&

	test_cmp_refs -C "$upstream" <<-EOF
	<CUMMIT-A> refs/heads/main
	<CUMMIT-A> refs/heads/next
	EOF
'

# Refs of upstream : main(A)             next(A)
# Refs of workbench: main(A)  tags/v123
test_expect_success "cleanup ($PROTOCOL/porcelain)" '
	but -C "$upstream" update-ref -d refs/heads/next
'

# Refs of upstream : main(A)
# Refs of workbench: main(A)  tags/v123
# but push --atomic: (B)                   next(A)  refs/for/main/topic(A)
test_expect_success "proc-receive: no hook, all failed for atomic push ($PROTOCOL/porcelain)" '
	test_must_fail but -C workbench push --porcelain --atomic origin \
		$B:main \
		HEAD:next \
		HEAD:refs/for/main/topic >out-$test_count 2>&1 &&
	make_user_friendly_and_stable_output <out-$test_count >actual &&
	format_and_save_expect <<-EOF &&
	> remote: # pre-receive hook        Z
	> remote: pre-receive< <CUMMIT-A> <CUMMIT-B> refs/heads/main        Z
	> remote: pre-receive< <ZERO-OID> <CUMMIT-A> refs/heads/next        Z
	> remote: pre-receive< <ZERO-OID> <CUMMIT-A> refs/for/main/topic        Z
	> remote: error: cannot find hook "proc-receive"        Z
	> To <URL/of/upstream.but>
	> !	<CUMMIT-B>:refs/heads/main	[remote rejected] (fail to run proc-receive hook)
	> !	HEAD:refs/heads/next	[remote rejected] (fail to run proc-receive hook)
	> !	HEAD:refs/for/main/topic	[remote rejected] (fail to run proc-receive hook)
	> Done
	EOF
	test_cmp expect actual &&

	test_cmp_refs -C "$upstream" <<-EOF
	<CUMMIT-A> refs/heads/main
	EOF
'
