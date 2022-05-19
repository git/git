# Refs of upstream : main(A)  
# Refs of workbench: main(A)  tags/v123
# but-push         : main(B)             next(A)
test_expect_success "but-push ($PROTOCOL)" '
	but -C workbench push origin \
		$B:refs/heads/main \
		HEAD:refs/heads/next \
		>out 2>&1 &&
	make_user_friendly_and_stable_output <out >actual &&
	format_and_save_expect <<-EOF &&
	> remote: # pre-receive hook        Z
	> remote: pre-receive< <CUMMIT-A> <CUMMIT-B> refs/heads/main        Z
	> remote: pre-receive< <ZERO-OID> <CUMMIT-A> refs/heads/next        Z
	> remote: # post-receive hook        Z
	> remote: post-receive< <CUMMIT-A> <CUMMIT-B> refs/heads/main        Z
	> remote: post-receive< <ZERO-OID> <CUMMIT-A> refs/heads/next        Z
	> To <URL/of/upstream.but>
	>    <CUMMIT-A>..<CUMMIT-B>  <CUMMIT-B> -> main
	>  * [new branch]      HEAD -> next
	EOF
	test_cmp expect actual &&

	test_cmp_refs -C "$upstream" <<-EOF
	<CUMMIT-B> refs/heads/main
	<CUMMIT-A> refs/heads/next
	EOF
'

# Refs of upstream : main(B)  next(A)
# Refs of workbench: main(A)           tags/v123
# but-push --atomic: main(A)  next(B)
test_expect_success "but-push --atomic ($PROTOCOL)" '
	test_must_fail but -C workbench push --atomic origin \
		main \
		$B:refs/heads/next \
		>out-$test_count 2>&1 &&
	filter_out_user_friendly_and_stable_output \
		-e "/^To / { p; }" \
		-e "/^ ! / { p; }" \
		<out-$test_count >actual &&
	format_and_save_expect <<-EOF &&
	> To <URL/of/upstream.but>
	>  ! [rejected]        main -> main (non-fast-forward)
	>  ! [rejected]        <CUMMIT-B> -> next (atomic push failed)
	EOF
	test_cmp expect actual &&

	test_cmp_refs -C "$upstream" <<-EOF
	<CUMMIT-B> refs/heads/main
	<CUMMIT-A> refs/heads/next
	EOF
'

# Refs of upstream : main(B)  next(A)
# Refs of workbench: main(A)           tags/v123
# but-push         : main(A)  next(B)
test_expect_success "non-fast-forward but-push ($PROTOCOL)" '
	test_must_fail but \
		-C workbench \
		-c advice.pushUpdateRejected=false \
		push origin \
		main \
		$B:refs/heads/next \
		>out-$test_count 2>&1 &&
	make_user_friendly_and_stable_output <out-$test_count >actual &&
	format_and_save_expect <<-EOF &&
	> remote: # pre-receive hook        Z
	> remote: pre-receive< <CUMMIT-A> <CUMMIT-B> refs/heads/next        Z
	> remote: # post-receive hook        Z
	> remote: post-receive< <CUMMIT-A> <CUMMIT-B> refs/heads/next        Z
	> To <URL/of/upstream.but>
	>    <CUMMIT-A>..<CUMMIT-B>  <CUMMIT-B> -> next
	>  ! [rejected]        main -> main (non-fast-forward)
	EOF
	test_cmp expect actual &&

	test_cmp_refs -C "$upstream" <<-EOF
	<CUMMIT-B> refs/heads/main
	<CUMMIT-B> refs/heads/next
	EOF
'

# Refs of upstream : main(B)  next(B)
# Refs of workbench: main(A)           tags/v123
# but-push -f      : main(A)  NULL     tags/v123  refs/review/main/topic(A)  a/b/c(A)
test_expect_success "but-push -f ($PROTOCOL)" '
	but -C workbench push -f origin \
		refs/tags/v123 \
		:refs/heads/next \
		main \
		main:refs/review/main/topic \
		HEAD:refs/heads/a/b/c \
		>out 2>&1 &&
	make_user_friendly_and_stable_output <out >actual &&
	format_and_save_expect <<-EOF &&
	> remote: # pre-receive hook        Z
	> remote: pre-receive< <CUMMIT-B> <CUMMIT-A> refs/heads/main        Z
	> remote: pre-receive< <CUMMIT-B> <ZERO-OID> refs/heads/next        Z
	> remote: pre-receive< <ZERO-OID> <TAG-v123> refs/tags/v123        Z
	> remote: pre-receive< <ZERO-OID> <CUMMIT-A> refs/review/main/topic        Z
	> remote: pre-receive< <ZERO-OID> <CUMMIT-A> refs/heads/a/b/c        Z
	> remote: # post-receive hook        Z
	> remote: post-receive< <CUMMIT-B> <CUMMIT-A> refs/heads/main        Z
	> remote: post-receive< <CUMMIT-B> <ZERO-OID> refs/heads/next        Z
	> remote: post-receive< <ZERO-OID> <TAG-v123> refs/tags/v123        Z
	> remote: post-receive< <ZERO-OID> <CUMMIT-A> refs/review/main/topic        Z
	> remote: post-receive< <ZERO-OID> <CUMMIT-A> refs/heads/a/b/c        Z
	> To <URL/of/upstream.but>
	>  + <CUMMIT-B>...<CUMMIT-A> main -> main (forced update)
	>  - [deleted]         next
	>  * [new tag]         v123 -> v123
	>  * [new reference]   main -> refs/review/main/topic
	>  * [new branch]      HEAD -> a/b/c
	EOF
	test_cmp expect actual &&

	test_cmp_refs -C "$upstream" <<-EOF
	<CUMMIT-A> refs/heads/a/b/c
	<CUMMIT-A> refs/heads/main
	<CUMMIT-A> refs/review/main/topic
	<TAG-v123> refs/tags/v123
	EOF
'

# Refs of upstream : main(A)  tags/v123  refs/review/main/topic(A)  a/b/c(A)
# Refs of workbench: main(A)  tags/v123
test_expect_success "cleanup ($PROTOCOL)" '
	(
		cd "$upstream" &&
		but update-ref -d refs/review/main/topic &&
		but update-ref -d refs/tags/v123 &&
		but update-ref -d refs/heads/a/b/c
	)
'
