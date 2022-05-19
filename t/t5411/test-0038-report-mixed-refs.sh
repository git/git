test_expect_success "setup proc-receive hook ($PROTOCOL)" '
	test_hook -C "$upstream" --clobber proc-receive <<-EOF
	printf >&2 "# proc-receive hook\n"
	test-tool proc-receive -v \
		-r "ok refs/for/next/topic2" \
		-r "ng refs/for/next/topic1 fail to call Web API" \
		-r "ok refs/for/main/topic" \
		-r "option refname refs/for/main/topic" \
		-r "option old-oid $A" \
		-r "option new-oid $B"
	EOF
'

# Refs of upstream : main(A)
# Refs of workbench: main(A)  tags/v123
# git push         : (B)                   bar(A)  baz(A)  refs/for/next/topic(A)  foo(A)  refs/for/main/topic(A)
test_expect_success "proc-receive: report update of mixed refs ($PROTOCOL)" '
	test_must_fail git -C workbench push origin \
		$B:refs/heads/main \
		HEAD:refs/heads/bar \
		HEAD:refs/heads/baz \
		HEAD:refs/for/next/topic2 \
		HEAD:refs/for/next/topic1 \
		HEAD:refs/heads/foo \
		HEAD:refs/for/main/topic \
		HEAD:refs/for/next/topic3 \
		>out-$test_count 2>&1 &&
	make_user_friendly_and_stable_output <out-$test_count >actual &&
	format_and_save_expect <<-EOF &&
	> remote: # pre-receive hook        Z
	> remote: pre-receive< <CUMMIT-A> <CUMMIT-B> refs/heads/main        Z
	> remote: pre-receive< <ZERO-OID> <CUMMIT-A> refs/heads/bar        Z
	> remote: pre-receive< <ZERO-OID> <CUMMIT-A> refs/heads/baz        Z
	> remote: pre-receive< <ZERO-OID> <CUMMIT-A> refs/for/next/topic2        Z
	> remote: pre-receive< <ZERO-OID> <CUMMIT-A> refs/for/next/topic1        Z
	> remote: pre-receive< <ZERO-OID> <CUMMIT-A> refs/heads/foo        Z
	> remote: pre-receive< <ZERO-OID> <CUMMIT-A> refs/for/main/topic        Z
	> remote: pre-receive< <ZERO-OID> <CUMMIT-A> refs/for/next/topic3        Z
	> remote: # proc-receive hook        Z
	> remote: proc-receive< <ZERO-OID> <CUMMIT-A> refs/for/next/topic2        Z
	> remote: proc-receive< <ZERO-OID> <CUMMIT-A> refs/for/next/topic1        Z
	> remote: proc-receive< <ZERO-OID> <CUMMIT-A> refs/for/main/topic        Z
	> remote: proc-receive< <ZERO-OID> <CUMMIT-A> refs/for/next/topic3        Z
	> remote: proc-receive> ok refs/for/next/topic2        Z
	> remote: proc-receive> ng refs/for/next/topic1 fail to call Web API        Z
	> remote: proc-receive> ok refs/for/main/topic        Z
	> remote: proc-receive> option refname refs/for/main/topic        Z
	> remote: proc-receive> option old-oid <CUMMIT-A>        Z
	> remote: proc-receive> option new-oid <CUMMIT-B>        Z
	> remote: # post-receive hook        Z
	> remote: post-receive< <CUMMIT-A> <CUMMIT-B> refs/heads/main        Z
	> remote: post-receive< <ZERO-OID> <CUMMIT-A> refs/heads/bar        Z
	> remote: post-receive< <ZERO-OID> <CUMMIT-A> refs/heads/baz        Z
	> remote: post-receive< <ZERO-OID> <CUMMIT-A> refs/for/next/topic2        Z
	> remote: post-receive< <ZERO-OID> <CUMMIT-A> refs/heads/foo        Z
	> remote: post-receive< <CUMMIT-A> <CUMMIT-B> refs/for/main/topic        Z
	> To <URL/of/upstream.git>
	>    <CUMMIT-A>..<CUMMIT-B>  <CUMMIT-B> -> main
	>  * [new branch]      HEAD -> bar
	>  * [new branch]      HEAD -> baz
	>  * [new reference]   HEAD -> refs/for/next/topic2
	>  * [new branch]      HEAD -> foo
	>    <CUMMIT-A>..<CUMMIT-B>  HEAD -> refs/for/main/topic
	>  ! [remote rejected] HEAD -> refs/for/next/topic1 (fail to call Web API)
	>  ! [remote rejected] HEAD -> refs/for/next/topic3 (proc-receive failed to report status)
	EOF
	test_cmp expect actual &&

	test_cmp_refs -C "$upstream" <<-EOF
	<CUMMIT-A> refs/heads/bar
	<CUMMIT-A> refs/heads/baz
	<CUMMIT-A> refs/heads/foo
	<CUMMIT-B> refs/heads/main
	EOF
'

# Refs of upstream : main(B)             foo(A)  bar(A))  baz(A)
# Refs of workbench: main(A)  tags/v123
test_expect_success "cleanup ($PROTOCOL)" '
	(
		cd "$upstream" &&
		git update-ref refs/heads/main $A &&
		git update-ref -d refs/heads/foo &&
		git update-ref -d refs/heads/bar &&
		git update-ref -d refs/heads/baz
	)
'
