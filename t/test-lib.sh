#!/bin/sh
#
# Copyright (c) 2005 Junio C Hamano
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see http://www.gnu.org/licenses/ .

# Keep the original TERM for say_color
ORIGINAL_TERM=$TERM

# Test the binaries we have just built.  The tests are kept in
# t/ subdirectory and are run in 'trash directory' subdirectory.
if test -z "$TEST_DIRECTORY"
then
	# We allow tests to override this, in case they want to run tests
	# outside of t/, e.g. for running tests on the test library
	# itself.
	TEST_DIRECTORY=$(pwd)
fi
if test -z "$TEST_OUTPUT_DIRECTORY"
then
	# Similarly, override this to store the test-results subdir
	# elsewhere
	TEST_OUTPUT_DIRECTORY=$TEST_DIRECTORY
fi
GIT_BUILD_DIR="$TEST_DIRECTORY"/..

################################################################
# It appears that people try to run tests without building...
"$GIT_BUILD_DIR/git" >/dev/null
if test $? != 1
then
	echo >&2 'error: you do not seem to have built git yet.'
	exit 1
fi

. "$GIT_BUILD_DIR"/GIT-BUILD-OPTIONS
export PERL_PATH SHELL_PATH

# if --tee was passed, write the output not only to the terminal, but
# additionally to the file test-results/$BASENAME.out, too.
case "$GIT_TEST_TEE_STARTED, $* " in
done,*)
	# do not redirect again
	;;
*' --tee '*|*' --va'*)
	mkdir -p test-results
	BASE=test-results/$(basename "$0" .sh)
	(GIT_TEST_TEE_STARTED=done ${SHELL_PATH} "$0" "$@" 2>&1;
	 echo $? > $BASE.exit) | tee $BASE.out
	test "$(cat $BASE.exit)" = 0
	exit
	;;
esac

# For repeatability, reset the environment to known value.
LANG=C
LC_ALL=C
PAGER=cat
TZ=UTC
TERM=dumb
export LANG LC_ALL PAGER TERM TZ
EDITOR=:
# A call to "unset" with no arguments causes at least Solaris 10
# /usr/xpg4/bin/sh and /bin/ksh to bail out.  So keep the unsets
# deriving from the command substitution clustered with the other
# ones.
unset VISUAL EMAIL LANGUAGE COLUMNS $("$PERL_PATH" -e '
	my @env = keys %ENV;
	my $ok = join("|", qw(
		TRACE
		DEBUG
		USE_LOOKUP
		TEST
		.*_TEST
		PROVE
		VALGRIND
		UNZIP
		PERF_AGGREGATING_LATER
	));
	my @vars = grep(/^GIT_/ && !/^GIT_($ok)/o, @env);
	print join("\n", @vars);
')
unset XDG_CONFIG_HOME
GIT_AUTHOR_EMAIL=author@example.com
GIT_AUTHOR_NAME='A U Thor'
GIT_COMMITTER_EMAIL=committer@example.com
GIT_COMMITTER_NAME='C O Mitter'
GIT_MERGE_VERBOSITY=5
GIT_MERGE_AUTOEDIT=no
export GIT_MERGE_VERBOSITY GIT_MERGE_AUTOEDIT
export GIT_AUTHOR_EMAIL GIT_AUTHOR_NAME
export GIT_COMMITTER_EMAIL GIT_COMMITTER_NAME
export EDITOR

# Add libc MALLOC and MALLOC_PERTURB test
# only if we are not executing the test with valgrind
if expr " $GIT_TEST_OPTS " : ".* --valgrind " >/dev/null ||
   test -n "$TEST_NO_MALLOC_CHECK"
then
	setup_malloc_check () {
		: nothing
	}
	teardown_malloc_check () {
		: nothing
	}
else
	setup_malloc_check () {
		MALLOC_CHECK_=3	MALLOC_PERTURB_=165
		export MALLOC_CHECK_ MALLOC_PERTURB_
	}
	teardown_malloc_check () {
		unset MALLOC_CHECK_ MALLOC_PERTURB_
	}
fi

# Protect ourselves from common misconfiguration to export
# CDPATH into the environment
unset CDPATH

unset GREP_OPTIONS
unset UNZIP

case $(echo $GIT_TRACE |tr "[A-Z]" "[a-z]") in
1|2|true)
	echo "* warning: Some tests will not work if GIT_TRACE" \
		"is set as to trace on STDERR ! *"
	echo "* warning: Please set GIT_TRACE to something" \
		"other than 1, 2 or true ! *"
	;;
esac

# Convenience
#
# A regexp to match 5 and 40 hexdigits
_x05='[0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f]'
_x40="$_x05$_x05$_x05$_x05$_x05$_x05$_x05$_x05"

# Zero SHA-1
_z40=0000000000000000000000000000000000000000

# Line feed
LF='
'

export _x05 _x40 _z40 LF

# Each test should start with something like this, after copyright notices:
#
# test_description='Description of this test...
# This test checks if command xyzzy does the right thing...
# '
# . ./test-lib.sh
[ "x$ORIGINAL_TERM" != "xdumb" ] && (
		TERM=$ORIGINAL_TERM &&
		export TERM &&
		[ -t 1 ] &&
		tput bold >/dev/null 2>&1 &&
		tput setaf 1 >/dev/null 2>&1 &&
		tput sgr0 >/dev/null 2>&1
	) &&
	color=t

while test "$#" -ne 0
do
	case "$1" in
	-d|--d|--de|--deb|--debu|--debug)
		debug=t; shift ;;
	-i|--i|--im|--imm|--imme|--immed|--immedi|--immedia|--immediat|--immediate)
		immediate=t; shift ;;
	-l|--l|--lo|--lon|--long|--long-|--long-t|--long-te|--long-tes|--long-test|--long-tests)
		GIT_TEST_LONG=t; export GIT_TEST_LONG; shift ;;
	-h|--h|--he|--hel|--help)
		help=t; shift ;;
	-v|--v|--ve|--ver|--verb|--verbo|--verbos|--verbose)
		verbose=t; shift ;;
	-q|--q|--qu|--qui|--quie|--quiet)
		# Ignore --quiet under a TAP::Harness. Saying how many tests
		# passed without the ok/not ok details is always an error.
		test -z "$HARNESS_ACTIVE" && quiet=t; shift ;;
	--with-dashes)
		with_dashes=t; shift ;;
	--no-color)
		color=; shift ;;
	--va|--val|--valg|--valgr|--valgri|--valgrin|--valgrind)
		valgrind=t; verbose=t; shift ;;
	--tee)
		shift ;; # was handled already
	--root=*)
		root=$(expr "z$1" : 'z[^=]*=\(.*\)')
		shift ;;
	*)
		echo "error: unknown test option '$1'" >&2; exit 1 ;;
	esac
done

if test -n "$color"
then
	say_color () {
		(
		TERM=$ORIGINAL_TERM
		export TERM
		case "$1" in
		error)
			tput bold; tput setaf 1;; # bold red
		skip)
			tput bold; tput setaf 2;; # bold green
		pass)
			tput setaf 2;;            # green
		info)
			tput setaf 3;;            # brown
		*)
			test -n "$quiet" && return;;
		esac
		shift
		printf "%s" "$*"
		tput sgr0
		echo
		)
	}
else
	say_color() {
		test -z "$1" && test -n "$quiet" && return
		shift
		printf "%s\n" "$*"
	}
fi

error () {
	say_color error "error: $*"
	GIT_EXIT_OK=t
	exit 1
}

say () {
	say_color info "$*"
}

test "${test_description}" != "" ||
error "Test script did not set test_description."

if test "$help" = "t"
then
	echo "$test_description"
	exit 0
fi

exec 5>&1
exec 6<&0
if test "$verbose" = "t"
then
	exec 4>&2 3>&1
else
	exec 4>/dev/null 3>/dev/null
fi

test_failure=0
test_count=0
test_fixed=0
test_broken=0
test_success=0

test_external_has_tap=0

die () {
	code=$?
	if test -n "$GIT_EXIT_OK"
	then
		exit $code
	else
		echo >&5 "FATAL: Unexpected exit with code $code"
		exit 1
	fi
}

GIT_EXIT_OK=
trap 'die' EXIT

# The user-facing functions are loaded from a separate file so that
# test_perf subshells can have them too
. "$TEST_DIRECTORY/test-lib-functions.sh"

# You are not expected to call test_ok_ and test_failure_ directly, use
# the text_expect_* functions instead.

test_ok_ () {
	test_success=$(($test_success + 1))
	say_color "" "ok $test_count - $@"
}

test_failure_ () {
	test_failure=$(($test_failure + 1))
	say_color error "not ok - $test_count $1"
	shift
	echo "$@" | sed -e 's/^/#	/'
	test "$immediate" = "" || { GIT_EXIT_OK=t; exit 1; }
}

test_known_broken_ok_ () {
	test_fixed=$(($test_fixed+1))
	say_color "" "ok $test_count - $@ # TODO known breakage"
}

test_known_broken_failure_ () {
	test_broken=$(($test_broken+1))
	say_color skip "not ok $test_count - $@ # TODO known breakage"
}

test_debug () {
	test "$debug" = "" || eval "$1"
}

test_eval_ () {
	# This is a separate function because some tests use
	# "return" to end a test_expect_success block early.
	eval </dev/null >&3 2>&4 "$*"
}

test_run_ () {
	test_cleanup=:
	expecting_failure=$2
	test_eval_ "$1"
	eval_ret=$?

	if test -z "$immediate" || test $eval_ret = 0 || test -n "$expecting_failure"
	then
		setup_malloc_check
		test_eval_ "$test_cleanup"
		teardown_malloc_check
	fi
	if test "$verbose" = "t" && test -n "$HARNESS_ACTIVE"
	then
		echo ""
	fi
	return "$eval_ret"
}

test_skip () {
	test_count=$(($test_count+1))
	to_skip=
	for skp in $GIT_SKIP_TESTS
	do
		case $this_test.$test_count in
		$skp)
			to_skip=t
			break
		esac
	done
	if test -z "$to_skip" && test -n "$test_prereq" &&
	   ! test_have_prereq "$test_prereq"
	then
		to_skip=t
	fi
	case "$to_skip" in
	t)
		of_prereq=
		if test "$missing_prereq" != "$test_prereq"
		then
			of_prereq=" of $test_prereq"
		fi

		say_color skip >&3 "skipping test: $@"
		say_color skip "ok $test_count # skip $1 (missing $missing_prereq${of_prereq})"
		: true
		;;
	*)
		false
		;;
	esac
}

# stub; perf-lib overrides it
test_at_end_hook_ () {
	:
}

test_done () {
	GIT_EXIT_OK=t

	if test -z "$HARNESS_ACTIVE"
	then
		test_results_dir="$TEST_OUTPUT_DIRECTORY/test-results"
		mkdir -p "$test_results_dir"
		base=${0##*/}
		test_results_path="$test_results_dir/${base%.sh}-$$.counts"

		cat >>"$test_results_path" <<-EOF
		total $test_count
		success $test_success
		fixed $test_fixed
		broken $test_broken
		failed $test_failure

		EOF
	fi

	if test "$test_fixed" != 0
	then
		say_color pass "# fixed $test_fixed known breakage(s)"
	fi
	if test "$test_broken" != 0
	then
		say_color error "# still have $test_broken known breakage(s)"
		msg="remaining $(($test_count-$test_broken)) test(s)"
	else
		msg="$test_count test(s)"
	fi
	case "$test_failure" in
	0)
		# Maybe print SKIP message
		if test -n "$skip_all" && test $test_count -gt 0
		then
			error "Can't use skip_all after running some tests"
		fi
		[ -z "$skip_all" ] || skip_all=" # SKIP $skip_all"

		if test $test_external_has_tap -eq 0
		then
			if test $test_count -gt 0
			then
				say_color pass "# passed all $msg"
			fi
			say "1..$test_count$skip_all"
		fi

		test -d "$remove_trash" &&
		cd "$(dirname "$remove_trash")" &&
		rm -rf "$(basename "$remove_trash")"

		test_at_end_hook_

		exit 0 ;;

	*)
		if test $test_external_has_tap -eq 0
		then
			say_color error "# failed $test_failure among $msg"
			say "1..$test_count"
		fi

		exit 1 ;;

	esac
}

if test -n "$valgrind"
then
	make_symlink () {
		test -h "$2" &&
		test "$1" = "$(readlink "$2")" || {
			# be super paranoid
			if mkdir "$2".lock
			then
				rm -f "$2" &&
				ln -s "$1" "$2" &&
				rm -r "$2".lock
			else
				while test -d "$2".lock
				do
					say "Waiting for lock on $2."
					sleep 1
				done
			fi
		}
	}

	make_valgrind_symlink () {
		# handle only executables, unless they are shell libraries that
		# need to be in the exec-path.  We will just use "#!" as a
		# guess for a shell-script, since we have no idea what the user
		# may have configured as the shell path.
		test -x "$1" ||
		test "#!" = "$(head -c 2 <"$1")" ||
		return;

		base=$(basename "$1")
		symlink_target=$GIT_BUILD_DIR/$base
		# do not override scripts
		if test -x "$symlink_target" &&
		    test ! -d "$symlink_target" &&
		    test "#!" != "$(head -c 2 < "$symlink_target")"
		then
			symlink_target=../valgrind.sh
		fi
		case "$base" in
		*.sh|*.perl)
			symlink_target=../unprocessed-script
		esac
		# create the link, or replace it if it is out of date
		make_symlink "$symlink_target" "$GIT_VALGRIND/bin/$base" || exit
	}

	# override all git executables in TEST_DIRECTORY/..
	GIT_VALGRIND=$TEST_DIRECTORY/valgrind
	mkdir -p "$GIT_VALGRIND"/bin
	for file in $GIT_BUILD_DIR/git* $GIT_BUILD_DIR/test-*
	do
		make_valgrind_symlink $file
	done
	# special-case the mergetools loadables
	make_symlink "$GIT_BUILD_DIR"/mergetools "$GIT_VALGRIND/bin/mergetools"
	OLDIFS=$IFS
	IFS=:
	for path in $PATH
	do
		ls "$path"/git-* 2> /dev/null |
		while read file
		do
			make_valgrind_symlink "$file"
		done
	done
	IFS=$OLDIFS
	PATH=$GIT_VALGRIND/bin:$PATH
	GIT_EXEC_PATH=$GIT_VALGRIND/bin
	export GIT_VALGRIND
elif test -n "$GIT_TEST_INSTALLED"
then
	GIT_EXEC_PATH=$($GIT_TEST_INSTALLED/git --exec-path)  ||
	error "Cannot run git from $GIT_TEST_INSTALLED."
	PATH=$GIT_TEST_INSTALLED:$GIT_BUILD_DIR:$PATH
	GIT_EXEC_PATH=${GIT_TEST_EXEC_PATH:-$GIT_EXEC_PATH}
else # normal case, use ../bin-wrappers only unless $with_dashes:
	git_bin_dir="$GIT_BUILD_DIR/bin-wrappers"
	if ! test -x "$git_bin_dir/git"
	then
		if test -z "$with_dashes"
		then
			say "$git_bin_dir/git is not executable; using GIT_EXEC_PATH"
		fi
		with_dashes=t
	fi
	PATH="$git_bin_dir:$PATH"
	GIT_EXEC_PATH=$GIT_BUILD_DIR
	if test -n "$with_dashes"
	then
		PATH="$GIT_BUILD_DIR:$PATH"
	fi
fi
GIT_TEMPLATE_DIR="$GIT_BUILD_DIR"/templates/blt
unset GIT_CONFIG
GIT_CONFIG_NOSYSTEM=1
GIT_ATTR_NOSYSTEM=1
export PATH GIT_EXEC_PATH GIT_TEMPLATE_DIR GIT_CONFIG_NOSYSTEM GIT_ATTR_NOSYSTEM

if test -z "$GIT_TEST_CMP"
then
	if test -n "$GIT_TEST_CMP_USE_COPIED_CONTEXT"
	then
		GIT_TEST_CMP="$DIFF -c"
	else
		GIT_TEST_CMP="$DIFF -u"
	fi
fi

GITPERLLIB="$GIT_BUILD_DIR"/perl/blib/lib:"$GIT_BUILD_DIR"/perl/blib/arch/auto/Git
export GITPERLLIB
test -d "$GIT_BUILD_DIR"/templates/blt || {
	error "You haven't built things yet, have you?"
}

if test -z "$GIT_TEST_INSTALLED" && test -z "$NO_PYTHON"
then
	GITPYTHONLIB="$GIT_BUILD_DIR/git_remote_helpers/build/lib"
	export GITPYTHONLIB
	test -d "$GIT_BUILD_DIR"/git_remote_helpers/build || {
		error "You haven't built git_remote_helpers yet, have you?"
	}
fi

if ! test -x "$GIT_BUILD_DIR"/test-chmtime
then
	echo >&2 'You need to build test-chmtime:'
	echo >&2 'Run "make test-chmtime" in the source (toplevel) directory'
	exit 1
fi

# Test repository
test="trash directory.$(basename "$0" .sh)"
test -n "$root" && test="$root/$test"
case "$test" in
/*) TRASH_DIRECTORY="$test" ;;
 *) TRASH_DIRECTORY="$TEST_OUTPUT_DIRECTORY/$test" ;;
esac
test ! -z "$debug" || remove_trash=$TRASH_DIRECTORY
rm -fr "$test" || {
	GIT_EXIT_OK=t
	echo >&5 "FATAL: Cannot prepare test area"
	exit 1
}

HOME="$TRASH_DIRECTORY"
export HOME

if test -z "$TEST_NO_CREATE_REPO"
then
	test_create_repo "$test"
else
	mkdir -p "$test"
fi
# Use -P to resolve symlinks in our working directory so that the cwd
# in subprocesses like git equals our $PWD (for pathname comparisons).
cd -P "$test" || exit 1

this_test=${0##*/}
this_test=${this_test%%-*}
for skp in $GIT_SKIP_TESTS
do
	case "$this_test" in
	$skp)
		say_color skip >&3 "skipping test $this_test altogether"
		skip_all="skip all tests in $this_test"
		test_done
	esac
done

# Provide an implementation of the 'yes' utility
yes () {
	if test $# = 0
	then
		y=y
	else
		y="$*"
	fi

	while echo "$y"
	do
		:
	done
}

# Fix some commands on Windows
case $(uname -s) in
*MINGW*)
	# Windows has its own (incompatible) sort and find
	sort () {
		/usr/bin/sort "$@"
	}
	find () {
		/usr/bin/find "$@"
	}
	sum () {
		md5sum "$@"
	}
	# git sees Windows-style pwd
	pwd () {
		builtin pwd -W
	}
	# no POSIX permissions
	# backslashes in pathspec are converted to '/'
	# exec does not inherit the PID
	test_set_prereq MINGW
	test_set_prereq SED_STRIPS_CR
	;;
*CYGWIN*)
	test_set_prereq POSIXPERM
	test_set_prereq EXECKEEPSPID
	test_set_prereq NOT_MINGW
	test_set_prereq SED_STRIPS_CR
	;;
*)
	test_set_prereq POSIXPERM
	test_set_prereq BSLASHPSPEC
	test_set_prereq EXECKEEPSPID
	test_set_prereq NOT_MINGW
	;;
esac

( COLUMNS=1 && test $COLUMNS = 1 ) && test_set_prereq COLUMNS_CAN_BE_1
test -z "$NO_PERL" && test_set_prereq PERL
test -z "$NO_PYTHON" && test_set_prereq PYTHON
test -n "$USE_LIBPCRE" && test_set_prereq LIBPCRE
test -z "$NO_GETTEXT" && test_set_prereq GETTEXT

# Can we rely on git's output in the C locale?
if test -n "$GETTEXT_POISON"
then
	GIT_GETTEXT_POISON=YesPlease
	export GIT_GETTEXT_POISON
	test_set_prereq GETTEXT_POISON
else
	test_set_prereq C_LOCALE_OUTPUT
fi

# Use this instead of test_cmp to compare files that contain expected and
# actual output from git commands that can be translated.  When running
# under GETTEXT_POISON this pretends that the command produced expected
# results.
test_i18ncmp () {
	test -n "$GETTEXT_POISON" || test_cmp "$@"
}

# Use this instead of "grep expected-string actual" to see if the
# output from a git command that can be translated either contains an
# expected string, or does not contain an unwanted one.  When running
# under GETTEXT_POISON this pretends that the command produced expected
# results.
test_i18ngrep () {
	if test -n "$GETTEXT_POISON"
	then
	    : # pretend success
	elif test "x!" = "x$1"
	then
		shift
		! grep "$@"
	else
		grep "$@"
	fi
}

test_lazy_prereq SYMLINKS '
	# test whether the filesystem supports symbolic links
	ln -s x y && test -h y
'

test_lazy_prereq CASE_INSENSITIVE_FS '
	echo good >CamelCase &&
	echo bad >camelcase &&
	test "$(cat CamelCase)" != good
'

test_lazy_prereq UTF8_NFD_TO_NFC '
	# check whether FS converts nfd unicode to nfc
	auml=$(printf "\303\244")
	aumlcdiar=$(printf "\141\314\210")
	>"$auml" &&
	case "$(echo *)" in
	"$aumlcdiar")
		true ;;
	*)
		false ;;
	esac
'

test_lazy_prereq AUTOIDENT '
	sane_unset GIT_AUTHOR_NAME &&
	sane_unset GIT_AUTHOR_EMAIL &&
	git var GIT_AUTHOR_IDENT
'

# When the tests are run as root, permission tests will report that
# things are writable when they shouldn't be.
test -w / || test_set_prereq SANITY
