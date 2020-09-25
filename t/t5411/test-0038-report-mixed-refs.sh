test_expect_success "setup proc-receive hook ($PROTOCOL)" '
	write_script "$upstream/hooks/proc-receive" <<-EOF
	printf >&2 "# proc-receive hook\n"
	test-tool proc-receive -v \
		-r "ok refs/for/next/topic2" \
		-r "ng refs/for/next/topic1 fail to call Web API" \
		-r "ok refs/for/master/topic" \
		-r "option refname refs/for/master/topic" \
		-r "option old-oid $A" \
		-r "option new-oid $B"
	EOF
'

# Refs of upstream : master(A)
# Refs of workbench: master(A)  tags/v123
# git push         : (B)                   bar(A)  baz(A)  refs/for/next/topic(A)  foo(A)  refs/for/master/topic(A)
test_expect_success "proc-receive: report update of mixed refs ($PROTOCOL)" '
	test_must_fail git -C workbench push origin \
		$B:refs/heads/master \
		HEAD:refs/heads/bar \
		HEAD:refs/heads/baz \
		HEAD:refs/for/next/topic2 \
		HEAD:refs/for/next/topic1 \
		HEAD:refs/heads/foo \
		HEAD:refs/for/master/topic \
		HEAD:refs/for/next/topic3 \
		>out 2>&1 &&
	make_user_friendly_and_stable_output <out >actual &&
	cat >expect <<-EOF &&
	remote: # pre-receive hook
	remote: pre-receive< <COMMIT-A> <COMMIT-B> refs/heads/master
	remote: pre-receive< <ZERO-OID> <COMMIT-A> refs/heads/bar
	remote: pre-receive< <ZERO-OID> <COMMIT-A> refs/heads/baz
	remote: pre-receive< <ZERO-OID> <COMMIT-A> refs/for/next/topic2
	remote: pre-receive< <ZERO-OID> <COMMIT-A> refs/for/next/topic1
	remote: pre-receive< <ZERO-OID> <COMMIT-A> refs/heads/foo
	remote: pre-receive< <ZERO-OID> <COMMIT-A> refs/for/master/topic
	remote: pre-receive< <ZERO-OID> <COMMIT-A> refs/for/next/topic3
	remote: # proc-receive hook
	remote: proc-receive< <ZERO-OID> <COMMIT-A> refs/for/next/topic2
	remote: proc-receive< <ZERO-OID> <COMMIT-A> refs/for/next/topic1
	remote: proc-receive< <ZERO-OID> <COMMIT-A> refs/for/master/topic
	remote: proc-receive< <ZERO-OID> <COMMIT-A> refs/for/next/topic3
	remote: proc-receive> ok refs/for/next/topic2
	remote: proc-receive> ng refs/for/next/topic1 fail to call Web API
	remote: proc-receive> ok refs/for/master/topic
	remote: proc-receive> option refname refs/for/master/topic
	remote: proc-receive> option old-oid <COMMIT-A>
	remote: proc-receive> option new-oid <COMMIT-B>
	remote: # post-receive hook
	remote: post-receive< <COMMIT-A> <COMMIT-B> refs/heads/master
	remote: post-receive< <ZERO-OID> <COMMIT-A> refs/heads/bar
	remote: post-receive< <ZERO-OID> <COMMIT-A> refs/heads/baz
	remote: post-receive< <ZERO-OID> <COMMIT-A> refs/for/next/topic2
	remote: post-receive< <ZERO-OID> <COMMIT-A> refs/heads/foo
	remote: post-receive< <COMMIT-A> <COMMIT-B> refs/for/master/topic
	To <URL/of/upstream.git>
	 <OID-A>..<OID-B> <COMMIT-B> -> master
	 * [new branch] HEAD -> bar
	 * [new branch] HEAD -> baz
	 * [new reference] HEAD -> refs/for/next/topic2
	 * [new branch] HEAD -> foo
	 <OID-A>..<OID-B> HEAD -> refs/for/master/topic
	 ! [remote rejected] HEAD -> refs/for/next/topic1 (fail to call Web API)
	 ! [remote rejected] HEAD -> refs/for/next/topic3 (proc-receive failed to report status)
	EOF
	test_cmp expect actual &&
	git -C "$upstream" show-ref >out &&
	make_user_friendly_and_stable_output <out >actual &&
	cat >expect <<-EOF &&
	<COMMIT-A> refs/heads/bar
	<COMMIT-A> refs/heads/baz
	<COMMIT-A> refs/heads/foo
	<COMMIT-B> refs/heads/master
	EOF
	test_cmp expect actual
'

# Refs of upstream : master(B)             foo(A)  bar(A))  baz(A)
# Refs of workbench: master(A)  tags/v123
test_expect_success "cleanup ($PROTOCOL)" '
	(
		cd "$upstream" &&
		git update-ref refs/heads/master $A &&
		git update-ref -d refs/heads/foo &&
		git update-ref -d refs/heads/bar &&
		git update-ref -d refs/heads/baz
	)
'
