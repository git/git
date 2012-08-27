#!/bin/sh

test_description='test git rev-parse --parseopt'
. ./test-lib.sh

cat > expect <<\END_EXPECT
cat <<\EOF
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
END_EXPECT

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
	test_expect_code 129 git rev-parse --parseopt -- -h > output < optionspec &&
	test_i18ncmp expect output
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

cat >expect <<EOF
set -- --foo -- '--' 'arg' '--spam=ham'
EOF

test_expect_success 'test --parseopt --keep-dashdash --stop-at-non-option with --' '
	git rev-parse --parseopt --keep-dashdash --stop-at-non-option -- --foo -- arg --spam=ham <optionspec >output &&
	test_cmp expect output
'

cat > expect <<EOF
set -- --foo -- 'arg' '--spam=ham'
EOF

test_expect_success 'test --parseopt --keep-dashdash --stop-at-non-option without --' '
	git rev-parse --parseopt --keep-dashdash --stop-at-non-option -- --foo arg --spam=ham <optionspec >output &&
	test_cmp expect output
'

test_done
