test_expect_success "setup proc-receive hook (unexpected ref, $PROTOCOL/porcelain)" '
	test_hook -C "$upstream" --clobber proc-receive <<-\EOF
	printf >&2 "# proc-receive hook\n"
	test-tool proc-receive -v \
		-r "ok refs/heads/main"
	EOF
'

# Refs of upstream : main(A)
# Refs of workbench: main(A)  tags/v123
# but push         : (B)                   refs/for/main/topic
test_expect_success "proc-receive: report unexpected ref ($PROTOCOL/porcelain)" '
	test_must_fail but -C workbench push --porcelain origin \
		$B:refs/heads/main \
		HEAD:refs/for/main/topic \
		>out-$test_count 2>&1 &&
	make_user_friendly_and_stable_output <out-$test_count >actual &&
	format_and_save_expect <<-EOF &&
	> remote: # pre-receive hook        Z
	> remote: pre-receive< <CUMMIT-A> <CUMMIT-B> refs/heads/main        Z
	> remote: pre-receive< <ZERO-OID> <CUMMIT-A> refs/for/main/topic        Z
	> remote: # proc-receive hook        Z
	> remote: proc-receive< <ZERO-OID> <CUMMIT-A> refs/for/main/topic        Z
	> remote: proc-receive> ok refs/heads/main        Z
	> remote: error: proc-receive reported status on unexpected ref: refs/heads/main        Z
	> remote: # post-receive hook        Z
	> remote: post-receive< <CUMMIT-A> <CUMMIT-B> refs/heads/main        Z
	> To <URL/of/upstream.but>
	>  	<CUMMIT-B>:refs/heads/main	<CUMMIT-A>..<CUMMIT-B>
	> !	HEAD:refs/for/main/topic	[remote rejected] (proc-receive failed to report status)
	> Done
	EOF
	test_cmp expect actual &&

	test_cmp_refs -C "$upstream" <<-EOF
	<CUMMIT-B> refs/heads/main
	EOF
'

# Refs of upstream : main(B)
# Refs of workbench: main(A)  tags/v123
test_expect_success "cleanup ($PROTOCOL/porcelain)" '
	but -C "$upstream" update-ref refs/heads/main $A
'
