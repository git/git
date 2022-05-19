#!/bin/sh

test_description='help'

. ./test-lib.sh

configure_help () {
	test_config help.format html &&

	# Unless the path has "://" in it, Git tries to make sure
	# the documentation directory locally exists. Avoid it as
	# we are only interested in seeing an attempt to correctly
	# invoke a help browser in this test.
	test_config help.htmlpath test://html &&

	# Name a custom browser
	test_config browser.test.cmd ./test-browser &&
	test_config help.browser test
}

test_expect_success "setup" '
	# Just write out which page gets requested
	write_script test-browser <<-\EOF
	echo "$*" >test-browser.log
	EOF
'

# make sure to exercise these code paths, the output is a bit tricky
# to verify
test_expect_success 'basic help commands' '
	but help >/dev/null &&
	but help -a --no-verbose >/dev/null &&
	but help -g >/dev/null &&
	but help -a >/dev/null
'

test_expect_success 'invalid usage' '
	test_expect_code 129 but help -a add &&
	test_expect_code 129 but help --all add &&

	test_expect_code 129 but help -g add &&
	test_expect_code 129 but help -a -c &&

	test_expect_code 129 but help -g add &&
	test_expect_code 129 but help -a -g &&

	test_expect_code 129 but help -g -c &&
	test_expect_code 129 but help --config-for-completion add &&
	test_expect_code 129 but help --config-sections-for-completion add
'

for opt in '-a' '-g' '-c' '--config-for-completion' '--config-sections-for-completion'
do
	test_expect_success "invalid usage of '$opt' with [-i|-m|-w]" '
		but help $opt &&
		test_expect_code 129 but help $opt -i &&
		test_expect_code 129 but help $opt -m &&
		test_expect_code 129 but help $opt -w
	'

	if test "$opt" = "-a"
	then
		continue
	fi

	test_expect_success "invalid usage of '$opt' with --no-external-commands" '
		test_expect_code 129 but help $opt --no-external-commands
	'

	test_expect_success "invalid usage of '$opt' with --no-aliases" '
		test_expect_code 129 but help $opt --no-external-commands
	'
done

test_expect_success "works for commands and guides by default" '
	configure_help &&
	but help status &&
	echo "test://html/but-status.html" >expect &&
	test_cmp expect test-browser.log &&
	but help revisions &&
	echo "test://html/butrevisions.html" >expect &&
	test_cmp expect test-browser.log
'

test_expect_success "--exclude-guides does not work for guides" '
	>test-browser.log &&
	test_must_fail but help --exclude-guides revisions &&
	test_must_be_empty test-browser.log
'

test_expect_success "--help does not work for guides" "
	cat <<-EOF >expect &&
		but: 'revisions' is not a but command. See 'but --help'.
	EOF
	test_must_fail but revisions --help 2>actual &&
	test_cmp expect actual
"

test_expect_success 'but help' '
	but help >help.output &&
	test_i18ngrep "^   clone  " help.output &&
	test_i18ngrep "^   add    " help.output &&
	test_i18ngrep "^   log    " help.output &&
	test_i18ngrep "^   cummit " help.output &&
	test_i18ngrep "^   fetch  " help.output
'
test_expect_success 'but help -g' '
	but help -g >help.output &&
	test_i18ngrep "^   attributes " help.output &&
	test_i18ngrep "^   everyday   " help.output &&
	test_i18ngrep "^   tutorial   " help.output
'

test_expect_success 'but help fails for non-existing html pages' '
	configure_help &&
	mkdir html-empty &&
	test_must_fail but -c help.htmlpath=html-empty help status &&
	test_must_be_empty test-browser.log
'

test_expect_success 'but help succeeds without but.html' '
	configure_help &&
	mkdir html-with-docs &&
	touch html-with-docs/but-status.html &&
	but -c help.htmlpath=html-with-docs help status &&
	echo "html-with-docs/but-status.html" >expect &&
	test_cmp expect test-browser.log
'

test_expect_success 'but help -c' '
	but help -c >help.output &&
	cat >expect <<-\EOF &&

	'\''but help config'\'' for more information
	EOF
	grep -v -E \
		-e "^[^.]+\.[^.]+$" \
		-e "^[^.]+\.[^.]+\.[^.]+$" \
		help.output >actual &&
	test_cmp expect actual
'

test_expect_success 'but help --config-for-completion' '
	but help -c >human &&
	grep -E \
	     -e "^[^.]+\.[^.]+$" \
	     -e "^[^.]+\.[^.]+\.[^.]+$" human |
	     sed -e "s/\*.*//" -e "s/<.*//" |
	     sort -u >human.munged &&

	but help --config-for-completion >vars &&
	test_cmp human.munged vars
'

test_expect_success 'but help --config-sections-for-completion' '
	but help -c >human &&
	grep -E \
	     -e "^[^.]+\.[^.]+$" \
	     -e "^[^.]+\.[^.]+\.[^.]+$" human |
	     sed -e "s/\..*//" |
	     sort -u >human.munged &&

	but help --config-sections-for-completion >sections &&
	test_cmp human.munged sections
'

test_section_spacing () {
	cat >expect &&
	"$@" >out &&
	grep -E "(^[^ ]|^$)" out >actual
}

test_section_spacing_trailer () {
	test_section_spacing "$@" &&
	test_expect_code 1 but >out &&
	sed -n '/list available subcommands/,$p' <out >>expect
}


for cmd in but "but help"
do
	test_expect_success "'$cmd' section spacing" '
		test_section_spacing_trailer but help <<-\EOF &&
		usage: but [--version] [--help] [-C <path>] [-c <name>=<value>]

		These are common Git commands used in various situations:

		start a working area (see also: but help tutorial)

		work on the current change (see also: but help everyday)

		examine the history and state (see also: but help revisions)

		grow, mark and tweak your common history

		collaborate (see also: but help workflows)

		EOF
		test_cmp expect actual
	'
done

test_expect_success "'but help -a' section spacing" '
	test_section_spacing \
		but help -a --no-external-commands --no-aliases <<-\EOF &&
	See '\''but help <command>'\'' to read about a specific subcommand

	Main Porcelain Commands

	Ancillary Commands / Manipulators

	Ancillary Commands / Interrogators

	Interacting with Others

	Low-level Commands / Manipulators

	Low-level Commands / Interrogators

	Low-level Commands / Syncing Repositories

	Low-level Commands / Internal Helpers
	EOF
	test_cmp expect actual
'

test_expect_success "'but help -g' section spacing" '
	test_section_spacing_trailer but help -g <<-\EOF &&
	The Git concept guides are:

	EOF
	test_cmp expect actual
'

test_expect_success 'generate builtin list' '
	mkdir -p sub &&
	but --list-cmds=builtins >builtins
'

while read builtin
do
	test_expect_success "$builtin can handle -h" '
		(
			GIT_CEILING_DIRECTORIES=$(pwd) &&
			export GIT_CEILING_DIRECTORIES &&
			test_expect_code 129 but -C sub $builtin -h >output 2>&1
		) &&
		test_i18ngrep usage output
	'
done <builtins

test_done
