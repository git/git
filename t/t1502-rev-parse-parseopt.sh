#!/bin/sh

test_description='test git rev-parse --parseopt'
. ./test-lib.sh

test_expect_success 'setup optionspec' '
	sed -e "s/^|//" >optionspec <<\EOF
|some-command [options] <args>...
|
|some-command does foo and bar!
|--
|h,help    show the help
|
|foo       some nifty option --foo
|bar=      some cool option --bar with an argument
|b,baz     a short and long option
|
| An option group Header
|C?        option C with an optional argument
|d,data?   short and long option with an optional argument
|
| Argument hints
|B=arg     short option required argument
|bar2=arg  long option required argument
|e,fuz=with-space  short and long option required argument
|s?some    short option optional argument
|long?data long option optional argument
|g,fluf?path     short and long option optional argument
|longest=very-long-argument-hint  a very long argument hint
|pair=key=value  with an equals sign in the hint
|short-hint=a    with a one symbol hint
|
|Extras
|extra1    line above used to cause a segfault but no longer does
EOF
'

test_expect_success 'test --parseopt help output' '
	sed -e "s/^|//" >expect <<\END_EXPECT &&
|cat <<\EOF
|usage: some-command [options] <args>...
|
|    some-command does foo and bar!
|
|    -h, --help            show the help
|    --foo                 some nifty option --foo
|    --bar ...             some cool option --bar with an argument
|    -b, --baz             a short and long option
|
|An option group Header
|    -C[...]               option C with an optional argument
|    -d, --data[=...]      short and long option with an optional argument
|
|Argument hints
|    -B <arg>              short option required argument
|    --bar2 <arg>          long option required argument
|    -e, --fuz <with-space>
|                          short and long option required argument
|    -s[<some>]            short option optional argument
|    --long[=<data>]       long option optional argument
|    -g, --fluf[=<path>]   short and long option optional argument
|    --longest <very-long-argument-hint>
|                          a very long argument hint
|    --pair <key=value>    with an equals sign in the hint
|    --short-hint <a>      with a one symbol hint
|
|Extras
|    --extra1              line above used to cause a segfault but no longer does
|
|EOF
END_EXPECT
	test_expect_code 129 git rev-parse --parseopt -- -h > output < optionspec &&
	test_i18ncmp expect output
'

test_expect_success 'setup expect.1' "
	cat > expect <<EOF
set -- --foo --bar 'ham' -b -- 'arg'
EOF
"

test_expect_success 'test --parseopt' '
	git rev-parse --parseopt -- --foo --bar=ham --baz arg < optionspec > output &&
	test_cmp expect output
'

test_expect_success 'test --parseopt with mixed options and arguments' '
	git rev-parse --parseopt -- --foo arg --bar=ham --baz < optionspec > output &&
	test_cmp expect output
'

test_expect_success 'setup expect.2' "
	cat > expect <<EOF
set -- --foo -- 'arg' '--bar=ham'
EOF
"

test_expect_success 'test --parseopt with --' '
	git rev-parse --parseopt -- --foo -- arg --bar=ham < optionspec > output &&
	test_cmp expect output
'

test_expect_success 'test --parseopt --stop-at-non-option' '
	git rev-parse --parseopt --stop-at-non-option -- --foo arg --bar=ham < optionspec > output &&
	test_cmp expect output
'

test_expect_success 'setup expect.3' "
	cat > expect <<EOF
set -- --foo -- '--' 'arg' '--bar=ham'
EOF
"

test_expect_success 'test --parseopt --keep-dashdash' '
	git rev-parse --parseopt --keep-dashdash -- --foo -- arg --bar=ham < optionspec > output &&
	test_cmp expect output
'

test_expect_success 'setup expect.4' "
	cat >expect <<EOF
set -- --foo -- '--' 'arg' '--spam=ham'
EOF
"

test_expect_success 'test --parseopt --keep-dashdash --stop-at-non-option with --' '
	git rev-parse --parseopt --keep-dashdash --stop-at-non-option -- --foo -- arg --spam=ham <optionspec >output &&
	test_cmp expect output
'

test_expect_success 'setup expect.5' "
	cat > expect <<EOF
set -- --foo -- 'arg' '--spam=ham'
EOF
"

test_expect_success 'test --parseopt --keep-dashdash --stop-at-non-option without --' '
	git rev-parse --parseopt --keep-dashdash --stop-at-non-option -- --foo arg --spam=ham <optionspec >output &&
	test_cmp expect output
'

test_expect_success 'setup expect.6' "
	cat > expect <<EOF
set -- --foo --bar='z' --baz -C'Z' --data='A' -- 'arg'
EOF
"

test_expect_success 'test --parseopt --stuck-long' '
	git rev-parse --parseopt --stuck-long -- --foo --bar=z -b arg -CZ -dA <optionspec >output &&
	test_cmp expect output
'

test_expect_success 'setup expect.7' "
	cat > expect <<EOF
set -- --data='' -C --baz -- 'arg'
EOF
"

test_expect_success 'test --parseopt --stuck-long and empty optional argument' '
	git rev-parse --parseopt --stuck-long -- --data= arg -C -b <optionspec >output &&
	test_cmp expect output
'

test_expect_success 'setup expect.8' "
	cat > expect <<EOF
set -- --data --baz -- 'arg'
EOF
"

test_expect_success 'test --parseopt --stuck-long and long option with unset optional argument' '
	git rev-parse --parseopt --stuck-long -- --data arg -b <optionspec >output &&
	test_cmp expect output
'

test_expect_success 'test --parseopt --stuck-long and short option with unset optional argument' '
	git rev-parse --parseopt --stuck-long -- -d arg -b <optionspec >output &&
	test_cmp expect output
'

test_done
