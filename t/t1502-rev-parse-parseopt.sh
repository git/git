#!/bin/sh

test_description='test git rev-parse --parseopt'
. ./test-lib.sh

cat > expect.err <<EOF
usage: some-command [options] <args>...

    some-command does foo and bar!

    -h, --help            show the help
    --foo                 some nifty option --foo
    --bar ...             some cool option --bar with an argument

An option group Header
    -C[...]               option C with an optional argument

Extras
    --extra1              line above used to cause a segfault but no longer does

EOF

cat > optionspec << EOF
some-command [options] <args>...

some-command does foo and bar!
--
h,help    show the help

foo       some nifty option --foo
bar=      some cool option --bar with an argument

 An option group Header
C?        option C with an optional argument

Extras
extra1    line above used to cause a segfault but no longer does
EOF

test_expect_success 'test --parseopt help output' '
	git rev-parse --parseopt -- -h 2> output.err < optionspec
	test_cmp expect.err output.err
'

cat > expect <<EOF
set -- --foo --bar 'ham' -- 'arg'
EOF

test_expect_success 'test --parseopt' '
	git rev-parse --parseopt -- --foo --bar=ham arg < optionspec > output &&
	test_cmp expect output
'

test_expect_success 'test --parseopt with mixed options and arguments' '
	git rev-parse --parseopt -- --foo arg --bar=ham < optionspec > output &&
	test_cmp expect output
'

cat > expect <<EOF
set -- --foo -- 'arg' '--bar=ham'
EOF

test_expect_success 'test --parseopt with --' '
	git rev-parse --parseopt -- --foo -- arg --bar=ham < optionspec > output &&
	test_cmp expect output
'

test_expect_success 'test --parseopt --stop-at-non-option' '
	git rev-parse --parseopt --stop-at-non-option -- --foo arg --bar=ham < optionspec > output &&
	test_cmp expect output
'

cat > expect <<EOF
set -- --foo -- '--' 'arg' '--bar=ham'
EOF

test_expect_success 'test --parseopt --keep-dashdash' '
	git rev-parse --parseopt --keep-dashdash -- --foo -- arg --bar=ham < optionspec > output &&
	test_cmp expect output
'

test_done
