test_expect_success "config receive.procReceiveRefs = refs ($PROTOCOL)" '
	git -C "$upstream" config --unset-all receive.procReceiveRefs &&
	git -C "$upstream" config --add receive.procReceiveRefs refs
'

# Refs of upstream : main(A)
# Refs of workbench: main(A)  tags/v123
test_expect_success "setup upstream branches ($PROTOCOL)" '
	(
		cd "$upstream" &&
		git update-ref refs/heads/main $B &&
		git update-ref refs/heads/foo $A &&
		git update-ref refs/heads/bar $A &&
		git update-ref refs/heads/baz $A
	)

'

test_expect_success "setup proc-receive hook ($PROTOCOL)" '
	write_script "$upstream/hooks/proc-receive" <<-EOF
	printf >&2 "# proc-receive hook\n"
	test-tool proc-receive -v \
		-r "ok refs/heads/main" \
		-r "option fall-through" \
		-r "ok refs/heads/foo" \
		-r "option fall-through" \
		-r "ok refs/heads/bar" \
		-r "option fall-through" \
		-r "ok refs/for/main/topic" \
		-r "option refname refs/pull/123/head" \
		-r "option old-oid $A" \
		-r "option new-oid $B" \
		-r "ok refs/for/next/topic" \
		-r "option refname refs/pull/124/head" \
		-r "option old-oid $B" \
		-r "option new-oid $A" \
		-r "option forced-update"
	EOF
'

# Refs of upstream : main(B)             foo(A)  bar(A))  baz(A)
# Refs of workbench: main(A)  tags/v123
# git push -f      : main(A)             (NULL)  (B)              refs/for/main/topic(A)  refs/for/next/topic(A)
test_expect_success "proc-receive: process all refs ($PROTOCOL)" '
	git -C workbench push -f origin \
		HEAD:refs/heads/main \
		:refs/heads/foo \
		$B:refs/heads/bar \
		HEAD:refs/for/main/topic \
		HEAD:refs/for/next/topic \
		>out 2>&1 &&
	make_user_friendly_and_stable_output <out >actual &&
	cat >expect <<-EOF &&
	remote: # pre-receive hook
	remote: pre-receive< <COMMIT-A> <COMMIT-B> refs/heads/bar
	remote: pre-receive< <COMMIT-A> <ZERO-OID> refs/heads/foo
	remote: pre-receive< <COMMIT-B> <COMMIT-A> refs/heads/main
	remote: pre-receive< <ZERO-OID> <COMMIT-A> refs/for/main/topic
	remote: pre-receive< <ZERO-OID> <COMMIT-A> refs/for/next/topic
	remote: # proc-receive hook
	remote: proc-receive< <COMMIT-A> <COMMIT-B> refs/heads/bar
	remote: proc-receive< <COMMIT-A> <ZERO-OID> refs/heads/foo
	remote: proc-receive< <COMMIT-B> <COMMIT-A> refs/heads/main
	remote: proc-receive< <ZERO-OID> <COMMIT-A> refs/for/main/topic
	remote: proc-receive< <ZERO-OID> <COMMIT-A> refs/for/next/topic
	remote: proc-receive> ok refs/heads/main
	remote: proc-receive> option fall-through
	remote: proc-receive> ok refs/heads/foo
	remote: proc-receive> option fall-through
	remote: proc-receive> ok refs/heads/bar
	remote: proc-receive> option fall-through
	remote: proc-receive> ok refs/for/main/topic
	remote: proc-receive> option refname refs/pull/123/head
	remote: proc-receive> option old-oid <COMMIT-A>
	remote: proc-receive> option new-oid <COMMIT-B>
	remote: proc-receive> ok refs/for/next/topic
	remote: proc-receive> option refname refs/pull/124/head
	remote: proc-receive> option old-oid <COMMIT-B>
	remote: proc-receive> option new-oid <COMMIT-A>
	remote: proc-receive> option forced-update
	remote: # post-receive hook
	remote: post-receive< <COMMIT-A> <COMMIT-B> refs/heads/bar
	remote: post-receive< <COMMIT-A> <ZERO-OID> refs/heads/foo
	remote: post-receive< <COMMIT-B> <COMMIT-A> refs/heads/main
	remote: post-receive< <COMMIT-A> <COMMIT-B> refs/pull/123/head
	remote: post-receive< <COMMIT-B> <COMMIT-A> refs/pull/124/head
	To <URL/of/upstream.git>
	 <OID-A>..<OID-B> <COMMIT-B> -> bar
	 - [deleted] foo
	 + <OID-B>...<OID-A> HEAD -> main (forced update)
	 <OID-A>..<OID-B> HEAD -> refs/pull/123/head
	 + <OID-B>...<OID-A> HEAD -> refs/pull/124/head (forced update)
	EOF
	test_cmp expect actual &&
	git -C "$upstream" show-ref >out &&
	make_user_friendly_and_stable_output <out >actual &&
	cat >expect <<-EOF &&
	<COMMIT-B> refs/heads/bar
	<COMMIT-A> refs/heads/baz
	<COMMIT-A> refs/heads/main
	EOF
	test_cmp expect actual
'

# Refs of upstream : main(A)             bar(A)  baz(B)
# Refs of workbench: main(A)  tags/v123
test_expect_success "cleanup ($PROTOCOL)" '
	(
		cd "$upstream" &&
		git update-ref -d refs/heads/bar &&
		git update-ref -d refs/heads/baz
	)
'
