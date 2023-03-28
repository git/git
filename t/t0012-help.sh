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
	git help >/dev/null &&
	git help -a --no-verbose >/dev/null &&
	git help -g >/dev/null &&
	git help -a >/dev/null
'

test_expect_success 'invalid usage' '
	test_expect_code 129 git help -a add &&
	test_expect_code 129 git help --all add &&

	test_expect_code 129 git help -g add &&
	test_expect_code 129 git help -a -c &&

	test_expect_code 129 git help -g add &&
	test_expect_code 129 git help -a -g &&

	test_expect_code 129 git help --user-interfaces add &&

	test_expect_code 129 git help -g -c &&
	test_expect_code 129 git help --config-for-completion add &&
	test_expect_code 129 git help --config-sections-for-completion add
'

for opt in '-a' '-g' '-c' '--config-for-completion' '--config-sections-for-completion'
do
	test_expect_success "invalid usage of '$opt' with [-i|-m|-w]" '
		git help $opt &&
		test_expect_code 129 git help $opt -i &&
		test_expect_code 129 git help $opt -m &&
		test_expect_code 129 git help $opt -w
	'

	if test "$opt" = "-a"
	then
		continue
	fi

	test_expect_success "invalid usage of '$opt' with --no-external-commands" '
		test_expect_code 129 git help $opt --no-external-commands
	'

	test_expect_success "invalid usage of '$opt' with --no-aliases" '
		test_expect_code 129 git help $opt --no-external-commands
	'
done

test_expect_success "works for commands and guides by default" '
	configure_help &&
	git help status &&
	echo "test://html/git-status.html" >expect &&
	test_cmp expect test-browser.log &&
	git help revisions &&
	echo "test://html/gitrevisions.html" >expect &&
	test_cmp expect test-browser.log
'

test_expect_success "--exclude-guides does not work for guides" '
	>test-browser.log &&
	test_must_fail git help --exclude-guides revisions &&
	test_must_be_empty test-browser.log
'

test_expect_success "--help does not work for guides" "
	cat <<-EOF >expect &&
		git: 'revisions' is not a git command. See 'git --help'.
	EOF
	test_must_fail git revisions --help 2>actual &&
	test_cmp expect actual
"

test_expect_success 'git help' '
	git help >help.output &&
	test_i18ngrep "^   clone  " help.output &&
	test_i18ngrep "^   add    " help.output &&
	test_i18ngrep "^   log    " help.output &&
	test_i18ngrep "^   commit " help.output &&
	test_i18ngrep "^   fetch  " help.output
'

test_expect_success 'git help -g' '
	git help -g >help.output &&
	test_i18ngrep "^   everyday   " help.output &&
	test_i18ngrep "^   tutorial   " help.output
'

test_expect_success 'git help fails for non-existing html pages' '
	configure_help &&
	mkdir html-empty &&
	test_must_fail git -c help.htmlpath=html-empty help status &&
	test_must_be_empty test-browser.log
'

test_expect_success 'git help succeeds without git.html' '
	configure_help &&
	mkdir html-with-docs &&
	touch html-with-docs/git-status.html &&
	git -c help.htmlpath=html-with-docs help status &&
	echo "html-with-docs/git-status.html" >expect &&
	test_cmp expect test-browser.log
'

test_expect_success 'git help --user-interfaces' '
	git help --user-interfaces >help.output &&
	grep "^   attributes   " help.output &&
	grep "^   mailmap   " help.output
'

test_expect_success 'git help -c' '
	git help -c >help.output &&
	cat >expect <<-\EOF &&

	'\''git help config'\'' for more information
	EOF
	grep -v -E \
		-e "^[^.]+\.[^.]+$" \
		-e "^[^.]+\.[^.]+\.[^.]+$" \
		help.output >actual &&
	test_cmp expect actual
'

test_expect_success 'git help --config-for-completion' '
	git help -c >human &&
	grep -E \
	     -e "^[^.]+\.[^.]+$" \
	     -e "^[^.]+\.[^.]+\.[^.]+$" human |
	     sed -e "s/\*.*//" -e "s/<.*//" |
	     sort -u >human.munged &&

	git help --config-for-completion >vars &&
	test_cmp human.munged vars
'

test_expect_success 'git help --config-sections-for-completion' '
	git help -c >human &&
	grep -E \
	     -e "^[^.]+\.[^.]+$" \
	     -e "^[^.]+\.[^.]+\.[^.]+$" human |
	     sed -e "s/\..*//" |
	     sort -u >human.munged &&

	git help --config-sections-for-completion >sections &&
	test_cmp human.munged sections
'

test_section_spacing () {
	cat >expect &&
	"$@" >out &&
	grep -E "(^[^ ]|^$)" out >actual
}

test_section_spacing_trailer () {
	test_section_spacing "$@" &&
	test_expect_code 1 git >out &&
	sed -n '/list available subcommands/,$p' <out >>expect
}


for cmd in git "git help"
do
	test_expect_success "'$cmd' section spacing" '
		test_section_spacing_trailer git help <<-\EOF &&
		usage: git [-v | --version] [-h | --help] [-C <path>] [-c <name>=<value>]

		These are common Git commands used in various situations:

		start a working area (see also: git help tutorial)

		work on the current change (see also: git help everyday)

		examine the history and state (see also: git help revisions)

		grow, mark and tweak your common history

		collaborate (see also: git help workflows)

		EOF
		test_cmp expect actual
	'
done

test_expect_success "'git help -a' section spacing" '
	test_section_spacing \
		git help -a --no-external-commands --no-aliases <<-\EOF &&
	See '\''git help <command>'\'' to read about a specific subcommand

	Main Porcelain Commands

	Ancillary Commands / Manipulators

	Ancillary Commands / Interrogators

	Interacting with Others

	Low-level Commands / Manipulators

	Low-level Commands / Interrogators

	Low-level Commands / Syncing Repositories

	Low-level Commands / Internal Helpers

	User-facing repository, command and file interfaces

	Developer-facing file formats, protocols and other interfaces
	EOF
	test_cmp expect actual
'

test_expect_success "'git help -g' section spacing" '
	test_section_spacing_trailer git help -g <<-\EOF &&
	The Git concept guides are:

	EOF
	test_cmp expect actual
'

test_expect_success 'generate builtin list' '
	mkdir -p sub &&
	git --list-cmds=builtins >builtins
'

while read builtin
do
	test_expect_success "$builtin can handle -h" '
		(
			GIT_CEILING_DIRECTORIES=$(pwd) &&
			export GIT_CEILING_DIRECTORIES &&
			test_expect_code 129 git -C sub $builtin -h >output 2>&1
		) &&
		test_i18ngrep usage output
	'
done <builtins

test_done
