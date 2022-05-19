# Refs of upstream : main(A)  
# Refs of workbench: main(A)  tags/v123
# but-push         : main(B)             next(A)
test_expect_success "but-push ($PROTOCOL/porcelain)" '
	but -C workbench push --porcelain origin \
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
	>  	<CUMMIT-B>:refs/heads/main	<CUMMIT-A>..<CUMMIT-B>
	> *	HEAD:refs/heads/next	[new branch]
	> Done
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
test_expect_success "but-push --atomic ($PROTOCOL/porcelain)" '
	test_must_fail but -C workbench push --atomic --porcelain origin \
		main \
		$B:refs/heads/next \
		>out-$test_count 2>&1 &&
	filter_out_user_friendly_and_stable_output \
		-e "s/^# GETTEXT POISON #//" \
		-e "/^To / { p; }" \
		-e "/^!/ { p; }" \
		<out-$test_count >actual &&
	format_and_save_expect <<-EOF &&
	> To <URL/of/upstream.but>
	> !	refs/heads/main:refs/heads/main	[rejected] (non-fast-forward)
	> !	<CUMMIT-B>:refs/heads/next	[rejected] (atomic push failed)
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
test_expect_success "non-fast-forward but-push ($PROTOCOL/porcelain)" '
	test_must_fail but \
		-C workbench \
		-c advice.pushUpdateRejected=false \
		push --porcelain origin \
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
	>  	<CUMMIT-B>:refs/heads/next	<CUMMIT-A>..<CUMMIT-B>
	> !	refs/heads/main:refs/heads/main	[rejected] (non-fast-forward)
	> Done
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
test_expect_success "but-push -f ($PROTOCOL/porcelain)" '
	but -C workbench push --porcelain -f origin \
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
	> +	refs/heads/main:refs/heads/main	<CUMMIT-B>...<CUMMIT-A> (forced update)
	> -	:refs/heads/next	[deleted]
	> *	refs/tags/v123:refs/tags/v123	[new tag]
	> *	refs/heads/main:refs/review/main/topic	[new reference]
	> *	HEAD:refs/heads/a/b/c	[new branch]
	> Done
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
test_expect_success "cleanup ($PROTOCOL/porcelain)" '
	(
		cd "$upstream" &&
		but update-ref -d refs/review/main/topic &&
		but update-ref -d refs/tags/v123 &&
		but update-ref -d refs/heads/a/b/c
	)
'
