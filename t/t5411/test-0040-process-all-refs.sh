test_expect_success "config receive.procReceiveRefs = refs ($PROTOCOL)" '
	but -C "$upstream" config --unset-all receive.procReceiveRefs &&
	but -C "$upstream" config --add receive.procReceiveRefs refs
'

# Refs of upstream : main(A)
# Refs of workbench: main(A)  tags/v123
test_expect_success "setup upstream branches ($PROTOCOL)" '
	(
		cd "$upstream" &&
		but update-ref refs/heads/main $B &&
		but update-ref refs/heads/foo $A &&
		but update-ref refs/heads/bar $A &&
		but update-ref refs/heads/baz $A
	)

'

test_expect_success "setup proc-receive hook ($PROTOCOL)" '
	test_hook -C "$upstream" --clobber proc-receive <<-EOF
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
# but push -f      : main(A)             (NULL)  (B)              refs/for/main/topic(A)  refs/for/next/topic(A)
test_expect_success "proc-receive: process all refs ($PROTOCOL)" '
	but -C workbench push -f origin \
		HEAD:refs/heads/main \
		:refs/heads/foo \
		$B:refs/heads/bar \
		HEAD:refs/for/main/topic \
		HEAD:refs/for/next/topic \
		>out 2>&1 &&
	make_user_friendly_and_stable_output <out >actual &&
	format_and_save_expect <<-EOF &&
	> remote: # pre-receive hook        Z
	> remote: pre-receive< <CUMMIT-A> <CUMMIT-B> refs/heads/bar        Z
	> remote: pre-receive< <CUMMIT-A> <ZERO-OID> refs/heads/foo        Z
	> remote: pre-receive< <CUMMIT-B> <CUMMIT-A> refs/heads/main        Z
	> remote: pre-receive< <ZERO-OID> <CUMMIT-A> refs/for/main/topic        Z
	> remote: pre-receive< <ZERO-OID> <CUMMIT-A> refs/for/next/topic        Z
	> remote: # proc-receive hook        Z
	> remote: proc-receive< <CUMMIT-A> <CUMMIT-B> refs/heads/bar        Z
	> remote: proc-receive< <CUMMIT-A> <ZERO-OID> refs/heads/foo        Z
	> remote: proc-receive< <CUMMIT-B> <CUMMIT-A> refs/heads/main        Z
	> remote: proc-receive< <ZERO-OID> <CUMMIT-A> refs/for/main/topic        Z
	> remote: proc-receive< <ZERO-OID> <CUMMIT-A> refs/for/next/topic        Z
	> remote: proc-receive> ok refs/heads/main        Z
	> remote: proc-receive> option fall-through        Z
	> remote: proc-receive> ok refs/heads/foo        Z
	> remote: proc-receive> option fall-through        Z
	> remote: proc-receive> ok refs/heads/bar        Z
	> remote: proc-receive> option fall-through        Z
	> remote: proc-receive> ok refs/for/main/topic        Z
	> remote: proc-receive> option refname refs/pull/123/head        Z
	> remote: proc-receive> option old-oid <CUMMIT-A>        Z
	> remote: proc-receive> option new-oid <CUMMIT-B>        Z
	> remote: proc-receive> ok refs/for/next/topic        Z
	> remote: proc-receive> option refname refs/pull/124/head        Z
	> remote: proc-receive> option old-oid <CUMMIT-B>        Z
	> remote: proc-receive> option new-oid <CUMMIT-A>        Z
	> remote: proc-receive> option forced-update        Z
	> remote: # post-receive hook        Z
	> remote: post-receive< <CUMMIT-A> <CUMMIT-B> refs/heads/bar        Z
	> remote: post-receive< <CUMMIT-A> <ZERO-OID> refs/heads/foo        Z
	> remote: post-receive< <CUMMIT-B> <CUMMIT-A> refs/heads/main        Z
	> remote: post-receive< <CUMMIT-A> <CUMMIT-B> refs/pull/123/head        Z
	> remote: post-receive< <CUMMIT-B> <CUMMIT-A> refs/pull/124/head        Z
	> To <URL/of/upstream.but>
	>    <CUMMIT-A>..<CUMMIT-B>  <CUMMIT-B> -> bar
	>  - [deleted]         foo
	>  + <CUMMIT-B>...<CUMMIT-A> HEAD -> main (forced update)
	>    <CUMMIT-A>..<CUMMIT-B>  HEAD -> refs/pull/123/head
	>  + <CUMMIT-B>...<CUMMIT-A> HEAD -> refs/pull/124/head (forced update)
	EOF
	test_cmp expect actual &&

	test_cmp_refs -C "$upstream" <<-EOF
	<CUMMIT-B> refs/heads/bar
	<CUMMIT-A> refs/heads/baz
	<CUMMIT-A> refs/heads/main
	EOF
'

# Refs of upstream : main(A)             bar(A)  baz(B)
# Refs of workbench: main(A)  tags/v123
test_expect_success "cleanup ($PROTOCOL)" '
	(
		cd "$upstream" &&
		but update-ref -d refs/heads/bar &&
		but update-ref -d refs/heads/baz
	)
'
