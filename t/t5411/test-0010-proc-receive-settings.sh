test_expect_success "add two receive.procReceiveRefs settings" '
	(
		cd "$upstream" &&
		but config --add receive.procReceiveRefs refs/for &&
		but config --add receive.procReceiveRefs refs/review/
	)
'
