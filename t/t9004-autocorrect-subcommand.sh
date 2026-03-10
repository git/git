#!/bin/sh

test_description='subcommand auto-correction test

Test autocorrection for subcommands with different
help.autocorrect mode.'

. ./test-lib.sh

test_expect_success 'setup' "
	echo '^error: unknown subcommand: ' >grep_unknown
"

test_expect_success 'default is not to autocorrect' '
	test_must_fail git worktree lsit 2>actual &&
	head -n1 actual >first && test_grep -f grep_unknown first
'

for mode in false no off 0 show never
do
	test_expect_success "'$mode' disables autocorrection" "
		test_config help.autocorrect $mode &&

		test_must_fail git worktree lsit 2>actual &&
		head -n1 actual >first && test_grep -f grep_unknown first
	"
done

for mode in -39 immediate 1
do
	test_expect_success "autocorrect immediately with '$mode'" - <<-EOT
		test_config help.autocorrect $mode &&

		git worktree lsit 2>actual &&
		test_grep "you meant 'list'\.$" actual
	EOT
done

test_expect_success 'delay path is executed' - <<-\EOT
	test_config help.autocorrect 2 &&

	git worktree lsit 2>actual &&
	test_grep '^Continuing in 0.2 seconds, ' actual
EOT

test_expect_success 'deny if too dissimilar' - <<-\EOT
	test_must_fail git remote rensnr 2>actual &&
	head -n1 actual >first && test_grep -f grep_unknown first
EOT

test_done
