test_expect_success "add two receive.procReceiveRefs settings" '
	(
		cd "$upstream" &&
		git config --add receive.procReceiveRefs refs/for &&
		git config --add receive.procReceiveRefs refs/review/
	)
'
