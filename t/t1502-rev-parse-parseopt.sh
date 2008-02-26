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
    -C [...]              option C with an optional argument

Extras
    --extra1              line above used to cause a segfault but no longer does

EOF

test_expect_success 'test --parseopt help output' '
	git rev-parse --parseopt -- -h 2> output.err <<EOF
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
	git diff expect.err output.err
'

test_done
