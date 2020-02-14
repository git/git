# Library of functions shared by all tests scripts, included by
# test-lib.sh.
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

# The semantics of the editor variables are that of invoking
# sh -c "$EDITOR \"$@\"" files ...
#
# If our trash directory contains shell metacharacters, they will be
# interpreted if we just set $EDITOR directly, so do a little dance with
# environment variables to work around this.
#
# In particular, quoting isn't enough, as the path may contain the same quote
# that we're using.
test_set_editor () {
	FAKE_EDITOR="$1"
	export FAKE_EDITOR
	EDITOR='"$FAKE_EDITOR"'
	export EDITOR
}

test_set_index_version () {
    GIT_INDEX_VERSION="$1"
    export GIT_INDEX_VERSION
}

test_decode_color () {
	awk '
		function name(n) {
			if (n == 0) return "RESET";
			if (n == 1) return "BOLD";
			if (n == 2) return "FAINT";
			if (n == 3) return "ITALIC";
			if (n == 7) return "REVERSE";
			if (n == 30) return "BLACK";
			if (n == 31) return "RED";
			if (n == 32) return "GREEN";
			if (n == 33) return "YELLOW";
			if (n == 34) return "BLUE";
			if (n == 35) return "MAGENTA";
			if (n == 36) return "CYAN";
			if (n == 37) return "WHITE";
			if (n == 40) return "BLACK";
			if (n == 41) return "BRED";
			if (n == 42) return "BGREEN";
			if (n == 43) return "BYELLOW";
			if (n == 44) return "BBLUE";
			if (n == 45) return "BMAGENTA";
			if (n == 46) return "BCYAN";
			if (n == 47) return "BWHITE";
		}
		{
			while (match($0, /\033\[[0-9;]*m/) != 0) {
				printf "%s<", substr($0, 1, RSTART-1);
				codes = substr($0, RSTART+2, RLENGTH-3);
				if (length(codes) == 0)
					printf "%s", name(0)
				else {
					n = split(codes, ary, ";");
					sep = "";
					for (i = 1; i <= n; i++) {
						printf "%s%s", sep, name(ary[i]);
						sep = ";"
					}
				}
				printf ">";
				$0 = substr($0, RSTART + RLENGTH, length($0) - RSTART - RLENGTH + 1);
			}
			print
		}
	'
}

lf_to_nul () {
	perl -pe 'y/\012/\000/'
}

nul_to_q () {
	perl -pe 'y/\000/Q/'
}

q_to_nul () {
	perl -pe 'y/Q/\000/'
}

q_to_cr () {
	tr Q '\015'
}

q_to_tab () {
	tr Q '\011'
}

qz_to_tab_space () {
	tr QZ '\011\040'
}

append_cr () {
	sed -e 's/$/Q/' | tr Q '\015'
}

remove_cr () {
	tr '\015' Q | sed -e 's/Q$//'
}

# Generate an output of $1 bytes of all zeroes (NULs, not ASCII zeroes).
# If $1 is 'infinity', output forever or until the receiving pipe stops reading,
# whichever comes first.
generate_zero_bytes () {
	test-tool genzeros "$@"
}

# In some bourne shell implementations, the "unset" builtin returns
# nonzero status when a variable to be unset was not set in the first
# place.
#
# Use sane_unset when that should not be considered an error.

sane_unset () {
	unset "$@"
	return 0
}

test_tick () {
	if test -z "${test_tick+set}"
	then
		test_tick=1112911993
	else
		test_tick=$(($test_tick + 60))
	fi
	GIT_COMMITTER_DATE="$test_tick -0700"
	GIT_AUTHOR_DATE="$test_tick -0700"
	export GIT_COMMITTER_DATE GIT_AUTHOR_DATE
}

# Stop execution and start a shell. This is useful for debugging tests.
#
# Be sure to remove all invocations of this command before submitting.

test_pause () {
	"$SHELL_PATH" <&6 >&5 2>&7
}

# Wrap git with a debugger. Adding this to a command can make it easier
# to understand what is going on in a failing test.
#
# Examples:
#     debug git checkout master
#     debug --debugger=nemiver git $ARGS
#     debug -d "valgrind --tool=memcheck --track-origins=yes" git $ARGS
debug () {
	case "$1" in
	-d)
		GIT_DEBUGGER="$2" &&
		shift 2
		;;
	--debugger=*)
		GIT_DEBUGGER="${1#*=}" &&
		shift 1
		;;
	*)
		GIT_DEBUGGER=1
		;;
	esac &&
	GIT_DEBUGGER="${GIT_DEBUGGER}" "$@" <&6 >&5 2>&7
}

# Call test_commit with the arguments
# [-C <directory>] <message> [<file> [<contents> [<tag>]]]"
#
# This will commit a file with the given contents and the given commit
# message, and tag the resulting commit with the given tag name.
#
# <file>, <contents>, and <tag> all default to <message>.
#
# If the first argument is "-C", the second argument is used as a path for
# the git invocations.

test_commit () {
	notick= &&
	signoff= &&
	indir= &&
	while test $# != 0
	do
		case "$1" in
		--notick)
			notick=yes
			;;
		--signoff)
			signoff="$1"
			;;
		-C)
			indir="$2"
			shift
			;;
		*)
			break
			;;
		esac
		shift
	done &&
	indir=${indir:+"$indir"/} &&
	file=${2:-"$1.t"} &&
	echo "${3-$1}" > "$indir$file" &&
	git ${indir:+ -C "$indir"} add "$file" &&
	if test -z "$notick"
	then
		test_tick
	fi &&
	git ${indir:+ -C "$indir"} commit $signoff -m "$1" &&
	git ${indir:+ -C "$indir"} tag "${4:-$1}"
}

# Call test_merge with the arguments "<message> <commit>", where <commit>
# can be a tag pointing to the commit-to-merge.

test_merge () {
	label="$1" &&
	shift &&
	test_tick &&
	git merge -m "$label" "$@" &&
	git tag "$label"
}

# Efficiently create <nr> commits, each with a unique number (from 1 to <nr>
# by default) in the commit message.
#
# Usage: test_commit_bulk [options] <nr>
#   -C <dir>:
#	Run all git commands in directory <dir>
#   --ref=<n>:
#	ref on which to create commits (default: HEAD)
#   --start=<n>:
#	number commit messages from <n> (default: 1)
#   --message=<msg>:
#	use <msg> as the commit mesasge (default: "commit %s")
#   --filename=<fn>:
#	modify <fn> in each commit (default: %s.t)
#   --contents=<string>:
#	place <string> in each file (default: "content %s")
#   --id=<string>:
#	shorthand to use <string> and %s in message, filename, and contents
#
# The message, filename, and contents strings are evaluated by printf, with the
# first "%s" replaced by the current commit number. So you can do:
#
#   test_commit_bulk --filename=file --contents="modification %s"
#
# to have every commit touch the same file, but with unique content.
#
test_commit_bulk () {
	tmpfile=.bulk-commit.input
	indir=.
	ref=HEAD
	n=1
	message='commit %s'
	filename='%s.t'
	contents='content %s'
	while test $# -gt 0
	do
		case "$1" in
		-C)
			indir=$2
			shift
			;;
		--ref=*)
			ref=${1#--*=}
			;;
		--start=*)
			n=${1#--*=}
			;;
		--message=*)
			message=${1#--*=}
			;;
		--filename=*)
			filename=${1#--*=}
			;;
		--contents=*)
			contents=${1#--*=}
			;;
		--id=*)
			message="${1#--*=} %s"
			filename="${1#--*=}-%s.t"
			contents="${1#--*=} %s"
			;;
		-*)
			BUG "invalid test_commit_bulk option: $1"
			;;
		*)
			break
			;;
		esac
		shift
	done
	total=$1

	add_from=
	if git -C "$indir" rev-parse --quiet --verify "$ref"
	then
		add_from=t
	fi

	while test "$total" -gt 0
	do
		test_tick &&
		echo "commit $ref"
		printf 'author %s <%s> %s\n' \
			"$GIT_AUTHOR_NAME" \
			"$GIT_AUTHOR_EMAIL" \
			"$GIT_AUTHOR_DATE"
		printf 'committer %s <%s> %s\n' \
			"$GIT_COMMITTER_NAME" \
			"$GIT_COMMITTER_EMAIL" \
			"$GIT_COMMITTER_DATE"
		echo "data <<EOF"
		printf "$message\n" $n
		echo "EOF"
		if test -n "$add_from"
		then
			echo "from $ref^0"
			add_from=
		fi
		printf "M 644 inline $filename\n" $n
		echo "data <<EOF"
		printf "$contents\n" $n
		echo "EOF"
		echo
		n=$((n + 1))
		total=$((total - 1))
	done >"$tmpfile"

	git -C "$indir" \
	    -c fastimport.unpacklimit=0 \
	    fast-import <"$tmpfile" || return 1

	# This will be left in place on failure, which may aid debugging.
	rm -f "$tmpfile"

	# If we updated HEAD, then be nice and update the index and working
	# tree, too.
	if test "$ref" = "HEAD"
	then
		git -C "$indir" checkout -f HEAD || return 1
	fi

}

# This function helps systems where core.filemode=false is set.
# Use it instead of plain 'chmod +x' to set or unset the executable bit
# of a file in the working directory and add it to the index.

test_chmod () {
	chmod "$@" &&
	git update-index --add "--chmod=$@"
}

# Get the modebits from a file.
test_modebits () {
	ls -l "$1" | sed -e 's|^\(..........\).*|\1|'
}

# Unset a configuration variable, but don't fail if it doesn't exist.
test_unconfig () {
	config_dir=
	if test "$1" = -C
	then
		shift
		config_dir=$1
		shift
	fi
	git ${config_dir:+-C "$config_dir"} config --unset-all "$@"
	config_status=$?
	case "$config_status" in
	5) # ok, nothing to unset
		config_status=0
		;;
	esac
	return $config_status
}

# Set git config, automatically unsetting it after the test is over.
test_config () {
	config_dir=
	if test "$1" = -C
	then
		shift
		config_dir=$1
		shift
	fi
	test_when_finished "test_unconfig ${config_dir:+-C '$config_dir'} '$1'" &&
	git ${config_dir:+-C "$config_dir"} config "$@"
}

test_config_global () {
	test_when_finished "test_unconfig --global '$1'" &&
	git config --global "$@"
}

write_script () {
	{
		echo "#!${2-"$SHELL_PATH"}" &&
		cat
	} >"$1" &&
	chmod +x "$1"
}

# Use test_set_prereq to tell that a particular prerequisite is available.
# The prerequisite can later be checked for in two ways:
#
# - Explicitly using test_have_prereq.
#
# - Implicitly by specifying the prerequisite tag in the calls to
#   test_expect_{success,failure,code}.
#
# The single parameter is the prerequisite tag (a simple word, in all
# capital letters by convention).

test_unset_prereq () {
	! test_have_prereq "$1" ||
	satisfied_prereq="${satisfied_prereq% $1 *} ${satisfied_prereq#* $1 }"
}

test_set_prereq () {
	if test -n "$GIT_TEST_FAIL_PREREQS_INTERNAL"
	then
		case "$1" in
		# The "!" case is handled below with
		# test_unset_prereq()
		!*)
			;;
		# (Temporary?) whitelist of things we can't easily
		# pretend not to support
		SYMLINKS)
			;;
		# Inspecting whether GIT_TEST_FAIL_PREREQS is on
		# should be unaffected.
		FAIL_PREREQS)
			;;
		*)
			return
		esac
	fi

	case "$1" in
	!*)
		test_unset_prereq "${1#!}"
		;;
	*)
		satisfied_prereq="$satisfied_prereq$1 "
		;;
	esac
}
satisfied_prereq=" "
lazily_testable_prereq= lazily_tested_prereq=

# Usage: test_lazy_prereq PREREQ 'script'
test_lazy_prereq () {
	lazily_testable_prereq="$lazily_testable_prereq$1 "
	eval test_prereq_lazily_$1=\$2
}

test_run_lazy_prereq_ () {
	script='
mkdir -p "$TRASH_DIRECTORY/prereq-test-dir" &&
(
	cd "$TRASH_DIRECTORY/prereq-test-dir" &&'"$2"'
)'
	say >&3 "checking prerequisite: $1"
	say >&3 "$script"
	test_eval_ "$script"
	eval_ret=$?
	rm -rf "$TRASH_DIRECTORY/prereq-test-dir"
	if test "$eval_ret" = 0; then
		say >&3 "prerequisite $1 ok"
	else
		say >&3 "prerequisite $1 not satisfied"
	fi
	return $eval_ret
}

test_have_prereq () {
	# prerequisites can be concatenated with ','
	save_IFS=$IFS
	IFS=,
	set -- $*
	IFS=$save_IFS

	total_prereq=0
	ok_prereq=0
	missing_prereq=

	for prerequisite
	do
		case "$prerequisite" in
		!*)
			negative_prereq=t
			prerequisite=${prerequisite#!}
			;;
		*)
			negative_prereq=
		esac

		case " $lazily_tested_prereq " in
		*" $prerequisite "*)
			;;
		*)
			case " $lazily_testable_prereq " in
			*" $prerequisite "*)
				eval "script=\$test_prereq_lazily_$prerequisite" &&
				if test_run_lazy_prereq_ "$prerequisite" "$script"
				then
					test_set_prereq $prerequisite
				fi
				lazily_tested_prereq="$lazily_tested_prereq$prerequisite "
			esac
			;;
		esac

		total_prereq=$(($total_prereq + 1))
		case "$satisfied_prereq" in
		*" $prerequisite "*)
			satisfied_this_prereq=t
			;;
		*)
			satisfied_this_prereq=
		esac

		case "$satisfied_this_prereq,$negative_prereq" in
		t,|,t)
			ok_prereq=$(($ok_prereq + 1))
			;;
		*)
			# Keep a list of missing prerequisites; restore
			# the negative marker if necessary.
			prerequisite=${negative_prereq:+!}$prerequisite
			if test -z "$missing_prereq"
			then
				missing_prereq=$prerequisite
			else
				missing_prereq="$prerequisite,$missing_prereq"
			fi
		esac
	done

	test $total_prereq = $ok_prereq
}

test_declared_prereq () {
	case ",$test_prereq," in
	*,$1,*)
		return 0
		;;
	esac
	return 1
}

test_verify_prereq () {
	test -z "$test_prereq" ||
	expr >/dev/null "$test_prereq" : '[A-Z0-9_,!]*$' ||
	BUG "'$test_prereq' does not look like a prereq"
}

test_expect_failure () {
	test_start_
	test "$#" = 3 && { test_prereq=$1; shift; } || test_prereq=
	test "$#" = 2 ||
	BUG "not 2 or 3 parameters to test-expect-failure"
	test_verify_prereq
	export test_prereq
	if ! test_skip "$@"
	then
		say >&3 "checking known breakage of $TEST_NUMBER.$test_count '$1': $2"
		if test_run_ "$2" expecting_failure
		then
			test_known_broken_ok_ "$1"
		else
			test_known_broken_failure_ "$1"
		fi
	fi
	test_finish_
}

test_expect_success () {
	test_start_
	test "$#" = 3 && { test_prereq=$1; shift; } || test_prereq=
	test "$#" = 2 ||
	BUG "not 2 or 3 parameters to test-expect-success"
	test_verify_prereq
	export test_prereq
	if ! test_skip "$@"
	then
		say >&3 "expecting success of $TEST_NUMBER.$test_count '$1': $2"
		if test_run_ "$2"
		then
			test_ok_ "$1"
		else
			test_failure_ "$@"
		fi
	fi
	test_finish_
}

# test_external runs external test scripts that provide continuous
# test output about their progress, and succeeds/fails on
# zero/non-zero exit code.  It outputs the test output on stdout even
# in non-verbose mode, and announces the external script with "# run
# <n>: ..." before running it.  When providing relative paths, keep in
# mind that all scripts run in "trash directory".
# Usage: test_external description command arguments...
# Example: test_external 'Perl API' perl ../path/to/test.pl
test_external () {
	test "$#" = 4 && { test_prereq=$1; shift; } || test_prereq=
	test "$#" = 3 ||
	BUG "not 3 or 4 parameters to test_external"
	descr="$1"
	shift
	test_verify_prereq
	export test_prereq
	if ! test_skip "$descr" "$@"
	then
		# Announce the script to reduce confusion about the
		# test output that follows.
		say_color "" "# run $test_count: $descr ($*)"
		# Export TEST_DIRECTORY, TRASH_DIRECTORY and GIT_TEST_LONG
		# to be able to use them in script
		export TEST_DIRECTORY TRASH_DIRECTORY GIT_TEST_LONG
		# Run command; redirect its stderr to &4 as in
		# test_run_, but keep its stdout on our stdout even in
		# non-verbose mode.
		"$@" 2>&4
		if test "$?" = 0
		then
			if test $test_external_has_tap -eq 0; then
				test_ok_ "$descr"
			else
				say_color "" "# test_external test $descr was ok"
				test_success=$(($test_success + 1))
			fi
		else
			if test $test_external_has_tap -eq 0; then
				test_failure_ "$descr" "$@"
			else
				say_color error "# test_external test $descr failed: $@"
				test_failure=$(($test_failure + 1))
			fi
		fi
	fi
}

# Like test_external, but in addition tests that the command generated
# no output on stderr.
test_external_without_stderr () {
	# The temporary file has no (and must have no) security
	# implications.
	tmp=${TMPDIR:-/tmp}
	stderr="$tmp/git-external-stderr.$$.tmp"
	test_external "$@" 4> "$stderr"
	test -f "$stderr" || error "Internal error: $stderr disappeared."
	descr="no stderr: $1"
	shift
	say >&3 "# expecting no stderr from previous command"
	if test ! -s "$stderr"
	then
		rm "$stderr"

		if test $test_external_has_tap -eq 0; then
			test_ok_ "$descr"
		else
			say_color "" "# test_external_without_stderr test $descr was ok"
			test_success=$(($test_success + 1))
		fi
	else
		if test "$verbose" = t
		then
			output=$(echo; echo "# Stderr is:"; cat "$stderr")
		else
			output=
		fi
		# rm first in case test_failure exits.
		rm "$stderr"
		if test $test_external_has_tap -eq 0; then
			test_failure_ "$descr" "$@" "$output"
		else
			say_color error "# test_external_without_stderr test $descr failed: $@: $output"
			test_failure=$(($test_failure + 1))
		fi
	fi
}

# debugging-friendly alternatives to "test [-f|-d|-e]"
# The commands test the existence or non-existence of $1. $2 can be
# given to provide a more precise diagnosis.
test_path_is_file () {
	if ! test -f "$1"
	then
		echo "File $1 doesn't exist. $2"
		false
	fi
}

test_path_is_dir () {
	if ! test -d "$1"
	then
		echo "Directory $1 doesn't exist. $2"
		false
	fi
}

test_path_exists () {
	if ! test -e "$1"
	then
		echo "Path $1 doesn't exist. $2"
		false
	fi
}

# Check if the directory exists and is empty as expected, barf otherwise.
test_dir_is_empty () {
	test_path_is_dir "$1" &&
	if test -n "$(ls -a1 "$1" | egrep -v '^\.\.?$')"
	then
		echo "Directory '$1' is not empty, it contains:"
		ls -la "$1"
		return 1
	fi
}

# Check if the file exists and has a size greater than zero
test_file_not_empty () {
	if ! test -s "$1"
	then
		echo "'$1' is not a non-empty file."
		false
	fi
}

test_path_is_missing () {
	if test -e "$1"
	then
		echo "Path exists:"
		ls -ld "$1"
		if test $# -ge 1
		then
			echo "$*"
		fi
		false
	fi
}

# test_line_count checks that a file has the number of lines it
# ought to. For example:
#
#	test_expect_success 'produce exactly one line of output' '
#		do something >output &&
#		test_line_count = 1 output
#	'
#
# is like "test $(wc -l <output) = 1" except that it passes the
# output through when the number of lines is wrong.

test_line_count () {
	if test $# != 3
	then
		BUG "not 3 parameters to test_line_count"
	elif ! test $(wc -l <"$3") "$1" "$2"
	then
		echo "test_line_count: line count for $3 !$1 $2"
		cat "$3"
		return 1
	fi
}

# Returns success if a comma separated string of keywords ($1) contains a
# given keyword ($2).
# Examples:
# `list_contains "foo,bar" bar` returns 0
# `list_contains "foo" bar` returns 1

list_contains () {
	case ",$1," in
	*,$2,*)
		return 0
		;;
	esac
	return 1
}

# This is not among top-level (test_expect_success | test_expect_failure)
# but is a prefix that can be used in the test script, like:
#
#	test_expect_success 'complain and die' '
#           do something &&
#           do something else &&
#	    test_must_fail git checkout ../outerspace
#	'
#
# Writing this as "! git checkout ../outerspace" is wrong, because
# the failure could be due to a segv.  We want a controlled failure.
#
# Accepts the following options:
#
#   ok=<signal-name>[,<...>]:
#     Don't treat an exit caused by the given signal as error.
#     Multiple signals can be specified as a comma separated list.
#     Currently recognized signal names are: sigpipe, success.
#     (Don't use 'success', use 'test_might_fail' instead.)

test_must_fail () {
	case "$1" in
	ok=*)
		_test_ok=${1#ok=}
		shift
		;;
	*)
		_test_ok=
		;;
	esac
	"$@" 2>&7
	exit_code=$?
	if test $exit_code -eq 0 && ! list_contains "$_test_ok" success
	then
		echo >&4 "test_must_fail: command succeeded: $*"
		return 1
	elif test_match_signal 13 $exit_code && list_contains "$_test_ok" sigpipe
	then
		return 0
	elif test $exit_code -gt 129 && test $exit_code -le 192
	then
		echo >&4 "test_must_fail: died by signal $(($exit_code - 128)): $*"
		return 1
	elif test $exit_code -eq 127
	then
		echo >&4 "test_must_fail: command not found: $*"
		return 1
	elif test $exit_code -eq 126
	then
		echo >&4 "test_must_fail: valgrind error: $*"
		return 1
	fi
	return 0
} 7>&2 2>&4

# Similar to test_must_fail, but tolerates success, too.  This is
# meant to be used in contexts like:
#
#	test_expect_success 'some command works without configuration' '
#		test_might_fail git config --unset all.configuration &&
#		do something
#	'
#
# Writing "git config --unset all.configuration || :" would be wrong,
# because we want to notice if it fails due to segv.
#
# Accepts the same options as test_must_fail.

test_might_fail () {
	test_must_fail ok=success "$@" 2>&7
} 7>&2 2>&4

# Similar to test_must_fail and test_might_fail, but check that a
# given command exited with a given exit code. Meant to be used as:
#
#	test_expect_success 'Merge with d/f conflicts' '
#		test_expect_code 1 git merge "merge msg" B master
#	'

test_expect_code () {
	want_code=$1
	shift
	"$@" 2>&7
	exit_code=$?
	if test $exit_code = $want_code
	then
		return 0
	fi

	echo >&4 "test_expect_code: command exited with $exit_code, we wanted $want_code $*"
	return 1
} 7>&2 2>&4

# test_cmp is a helper function to compare actual and expected output.
# You can use it like:
#
#	test_expect_success 'foo works' '
#		echo expected >expected &&
#		foo >actual &&
#		test_cmp expected actual
#	'
#
# This could be written as either "cmp" or "diff -u", but:
# - cmp's output is not nearly as easy to read as diff -u
# - not all diff versions understand "-u"

test_cmp() {
	$GIT_TEST_CMP "$@"
}

# Check that the given config key has the expected value.
#
#    test_cmp_config [-C <dir>] <expected-value>
#                    [<git-config-options>...] <config-key>
#
# for example to check that the value of core.bar is foo
#
#    test_cmp_config foo core.bar
#
test_cmp_config() {
	local GD &&
	if test "$1" = "-C"
	then
		shift &&
		GD="-C $1" &&
		shift
	fi &&
	printf "%s\n" "$1" >expect.config &&
	shift &&
	git $GD config "$@" >actual.config &&
	test_cmp expect.config actual.config
}

# test_cmp_bin - helper to compare binary files

test_cmp_bin() {
	cmp "$@"
}

# Use this instead of test_cmp to compare files that contain expected and
# actual output from git commands that can be translated.  When running
# under GIT_TEST_GETTEXT_POISON this pretends that the command produced expected
# results.
test_i18ncmp () {
	! test_have_prereq C_LOCALE_OUTPUT || test_cmp "$@"
}

# Use this instead of "grep expected-string actual" to see if the
# output from a git command that can be translated either contains an
# expected string, or does not contain an unwanted one.  When running
# under GIT_TEST_GETTEXT_POISON this pretends that the command produced expected
# results.
test_i18ngrep () {
	eval "last_arg=\${$#}"

	test -f "$last_arg" ||
	BUG "test_i18ngrep requires a file to read as the last parameter"

	if test $# -lt 2 ||
	   { test "x!" = "x$1" && test $# -lt 3 ; }
	then
		BUG "too few parameters to test_i18ngrep"
	fi

	if test_have_prereq !C_LOCALE_OUTPUT
	then
		# pretend success
		return 0
	fi

	if test "x!" = "x$1"
	then
		shift
		! grep "$@" && return 0

		echo >&4 "error: '! grep $@' did find a match in:"
	else
		grep "$@" && return 0

		echo >&4 "error: 'grep $@' didn't find a match in:"
	fi

	if test -s "$last_arg"
	then
		cat >&4 "$last_arg"
	else
		echo >&4 "<File '$last_arg' is empty>"
	fi

	return 1
}

# Call any command "$@" but be more verbose about its
# failure. This is handy for commands like "test" which do
# not output anything when they fail.
verbose () {
	"$@" && return 0
	echo >&4 "command failed: $(git rev-parse --sq-quote "$@")"
	return 1
}

# Check if the file expected to be empty is indeed empty, and barfs
# otherwise.

test_must_be_empty () {
	test_path_is_file "$1" &&
	if test -s "$1"
	then
		echo "'$1' is not empty, it contains:"
		cat "$1"
		return 1
	fi
}

# Tests that its two parameters refer to the same revision, or if '!' is
# provided first, that its other two parameters refer to different
# revisions.
test_cmp_rev () {
	local op='=' wrong_result=different

	if test $# -ge 1 && test "x$1" = 'x!'
	then
	    op='!='
	    wrong_result='the same'
	    shift
	fi
	if test $# != 2
	then
		error "bug in the test script: test_cmp_rev requires two revisions, but got $#"
	else
		local r1 r2
		r1=$(git rev-parse --verify "$1") &&
		r2=$(git rev-parse --verify "$2") || return 1

		if ! test "$r1" "$op" "$r2"
		then
			cat >&4 <<-EOF
			error: two revisions point to $wrong_result objects:
			  '$1': $r1
			  '$2': $r2
			EOF
			return 1
		fi
	fi
}

# Compare paths respecting core.ignoreCase
test_cmp_fspath () {
	if test "x$1" = "x$2"
	then
		return 0
	fi

	if test true != "$(git config --get --type=bool core.ignorecase)"
	then
		return 1
	fi

	test "x$(echo "$1" | tr A-Z a-z)" =  "x$(echo "$2" | tr A-Z a-z)"
}

# Print a sequence of integers in increasing order, either with
# two arguments (start and end):
#
#     test_seq 1 5 -- outputs 1 2 3 4 5 one line at a time
#
# or with one argument (end), in which case it starts counting
# from 1.

test_seq () {
	case $# in
	1)	set 1 "$@" ;;
	2)	;;
	*)	BUG "not 1 or 2 parameters to test_seq" ;;
	esac
	test_seq_counter__=$1
	while test "$test_seq_counter__" -le "$2"
	do
		echo "$test_seq_counter__"
		test_seq_counter__=$(( $test_seq_counter__ + 1 ))
	done
}

# This function can be used to schedule some commands to be run
# unconditionally at the end of the test to restore sanity:
#
#	test_expect_success 'test core.capslock' '
#		git config core.capslock true &&
#		test_when_finished "git config --unset core.capslock" &&
#		hello world
#	'
#
# That would be roughly equivalent to
#
#	test_expect_success 'test core.capslock' '
#		git config core.capslock true &&
#		hello world
#		git config --unset core.capslock
#	'
#
# except that the greeting and config --unset must both succeed for
# the test to pass.
#
# Note that under --immediate mode, no clean-up is done to help diagnose
# what went wrong.

test_when_finished () {
	# We cannot detect when we are in a subshell in general, but by
	# doing so on Bash is better than nothing (the test will
	# silently pass on other shells).
	test "${BASH_SUBSHELL-0}" = 0 ||
	BUG "test_when_finished does nothing in a subshell"
	test_cleanup="{ $*
		} && (exit \"\$eval_ret\"); eval_ret=\$?; $test_cleanup"
}

# This function can be used to schedule some commands to be run
# unconditionally at the end of the test script, e.g. to stop a daemon:
#
#	test_expect_success 'test git daemon' '
#		git daemon &
#		daemon_pid=$! &&
#		test_atexit 'kill $daemon_pid' &&
#		hello world
#	'
#
# The commands will be executed before the trash directory is removed,
# i.e. the atexit commands will still be able to access any pidfiles or
# socket files.
#
# Note that these commands will be run even when a test script run
# with '--immediate' fails.  Be careful with your atexit commands to
# minimize any changes to the failed state.

test_atexit () {
	# We cannot detect when we are in a subshell in general, but by
	# doing so on Bash is better than nothing (the test will
	# silently pass on other shells).
	test "${BASH_SUBSHELL-0}" = 0 ||
	error "bug in test script: test_atexit does nothing in a subshell"
	test_atexit_cleanup="{ $*
		} && (exit \"\$eval_ret\"); eval_ret=\$?; $test_atexit_cleanup"
}

# Most tests can use the created repository, but some may need to create more.
# Usage: test_create_repo <directory>
test_create_repo () {
	test "$#" = 1 ||
	BUG "not 1 parameter to test-create-repo"
	repo="$1"
	mkdir -p "$repo"
	(
		cd "$repo" || error "Cannot setup test environment"
		"${GIT_TEST_INSTALLED:-$GIT_EXEC_PATH}/git$X" init \
			"--template=$GIT_BUILD_DIR/templates/blt/" >&3 2>&4 ||
		error "cannot run git init -- have you built things yet?"
		mv .git/hooks .git/hooks-disabled
	) || exit
}

# This function helps on symlink challenged file systems when it is not
# important that the file system entry is a symbolic link.
# Use test_ln_s_add instead of "ln -s x y && git add y" to add a
# symbolic link entry y to the index.

test_ln_s_add () {
	if test_have_prereq SYMLINKS
	then
		ln -s "$1" "$2" &&
		git update-index --add "$2"
	else
		printf '%s' "$1" >"$2" &&
		ln_s_obj=$(git hash-object -w "$2") &&
		git update-index --add --cacheinfo 120000 $ln_s_obj "$2" &&
		# pick up stat info from the file
		git update-index "$2"
	fi
}

# This function writes out its parameters, one per line
test_write_lines () {
	printf "%s\n" "$@"
}

perl () {
	command "$PERL_PATH" "$@" 2>&7
} 7>&2 2>&4

# Given the name of an environment variable with a bool value, normalize
# its value to a 0 (true) or 1 (false or empty string) return code.
#
#   test_bool_env GIT_TEST_HTTPD <default-value>
#
# Return with code corresponding to the given default value if the variable
# is unset.
# Abort the test script if either the value of the variable or the default
# are not valid bool values.

test_bool_env () {
	if test $# != 2
	then
		BUG "test_bool_env requires two parameters (variable name and default value)"
	fi

	git env--helper --type=bool --default="$2" --exit-code "$1"
	ret=$?
	case $ret in
	0|1)	# unset or valid bool value
		;;
	*)	# invalid bool value or something unexpected
		error >&7 "test_bool_env requires bool values both for \$$1 and for the default fallback"
		;;
	esac
	return $ret
}

# Exit the test suite, either by skipping all remaining tests or by
# exiting with an error. If our prerequisite variable $1 falls back
# on a default assume we were opportunistically trying to set up some
# tests and we skip. If it is explicitly "true", then we report a failure.
#
# The error/skip message should be given by $2.
#
test_skip_or_die () {
	if ! test_bool_env "$1" false
	then
		skip_all=$2
		test_done
	fi
	error "$2"
}

# The following mingw_* functions obey POSIX shell syntax, but are actually
# bash scripts, and are meant to be used only with bash on Windows.

# A test_cmp function that treats LF and CRLF equal and avoids to fork
# diff when possible.
mingw_test_cmp () {
	# Read text into shell variables and compare them. If the results
	# are different, use regular diff to report the difference.
	local test_cmp_a= test_cmp_b=

	# When text came from stdin (one argument is '-') we must feed it
	# to diff.
	local stdin_for_diff=

	# Since it is difficult to detect the difference between an
	# empty input file and a failure to read the files, we go straight
	# to diff if one of the inputs is empty.
	if test -s "$1" && test -s "$2"
	then
		# regular case: both files non-empty
		mingw_read_file_strip_cr_ test_cmp_a <"$1"
		mingw_read_file_strip_cr_ test_cmp_b <"$2"
	elif test -s "$1" && test "$2" = -
	then
		# read 2nd file from stdin
		mingw_read_file_strip_cr_ test_cmp_a <"$1"
		mingw_read_file_strip_cr_ test_cmp_b
		stdin_for_diff='<<<"$test_cmp_b"'
	elif test "$1" = - && test -s "$2"
	then
		# read 1st file from stdin
		mingw_read_file_strip_cr_ test_cmp_a
		mingw_read_file_strip_cr_ test_cmp_b <"$2"
		stdin_for_diff='<<<"$test_cmp_a"'
	fi
	test -n "$test_cmp_a" &&
	test -n "$test_cmp_b" &&
	test "$test_cmp_a" = "$test_cmp_b" ||
	eval "diff -u \"\$@\" $stdin_for_diff"
}

# $1 is the name of the shell variable to fill in
mingw_read_file_strip_cr_ () {
	# Read line-wise using LF as the line separator
	# and use IFS to strip CR.
	local line
	while :
	do
		if IFS=$'\r' read -r -d $'\n' line
		then
			# good
			line=$line$'\n'
		else
			# we get here at EOF, but also if the last line
			# was not terminated by LF; in the latter case,
			# some text was read
			if test -z "$line"
			then
				# EOF, really
				break
			fi
		fi
		eval "$1=\$$1\$line"
	done
}

# Like "env FOO=BAR some-program", but run inside a subshell, which means
# it also works for shell functions (though those functions cannot impact
# the environment outside of the test_env invocation).
test_env () {
	(
		while test $# -gt 0
		do
			case "$1" in
			*=*)
				eval "${1%%=*}=\${1#*=}"
				eval "export ${1%%=*}"
				shift
				;;
			*)
				"$@" 2>&7
				exit
				;;
			esac
		done
	)
} 7>&2 2>&4

# Returns true if the numeric exit code in "$2" represents the expected signal
# in "$1". Signals should be given numerically.
test_match_signal () {
	if test "$2" = "$((128 + $1))"
	then
		# POSIX
		return 0
	elif test "$2" = "$((256 + $1))"
	then
		# ksh
		return 0
	fi
	return 1
}

# Read up to "$1" bytes (or to EOF) from stdin and write them to stdout.
test_copy_bytes () {
	perl -e '
		my $len = $ARGV[1];
		while ($len > 0) {
			my $s;
			my $nread = sysread(STDIN, $s, $len);
			die "cannot read: $!" unless defined($nread);
			last unless $nread;
			print $s;
			$len -= $nread;
		}
	' - "$1"
}

# run "$@" inside a non-git directory
nongit () {
	test -d non-repo ||
	mkdir non-repo ||
	return 1

	(
		GIT_CEILING_DIRECTORIES=$(pwd) &&
		export GIT_CEILING_DIRECTORIES &&
		cd non-repo &&
		"$@" 2>&7
	)
} 7>&2 2>&4

# convert stdin to pktline representation; note that empty input becomes an
# empty packet, not a flush packet (for that you can just print 0000 yourself).
packetize() {
	cat >packetize.tmp &&
	len=$(wc -c <packetize.tmp) &&
	printf '%04x%s' "$(($len + 4))" &&
	cat packetize.tmp &&
	rm -f packetize.tmp
}

# Parse the input as a series of pktlines, writing the result to stdout.
# Sideband markers are removed automatically, and the output is routed to
# stderr if appropriate.
#
# NUL bytes are converted to "\\0" for ease of parsing with text tools.
depacketize () {
	perl -e '
		while (read(STDIN, $len, 4) == 4) {
			if ($len eq "0000") {
				print "FLUSH\n";
			} else {
				read(STDIN, $buf, hex($len) - 4);
				$buf =~ s/\0/\\0/g;
				if ($buf =~ s/^[\x2\x3]//) {
					print STDERR $buf;
				} else {
					$buf =~ s/^\x1//;
					print $buf;
				}
			}
		}
	'
}

# Converts base-16 data into base-8. The output is given as a sequence of
# escaped octals, suitable for consumption by 'printf'.
hex2oct () {
	perl -ne 'printf "\\%03o", hex for /../g'
}

# Set the hash algorithm in use to $1.  Only useful when testing the testsuite.
test_set_hash () {
	test_hash_algo="$1"
}

# Detect the hash algorithm in use.
test_detect_hash () {
	# Currently we only support SHA-1, but in the future this function will
	# actually detect the algorithm in use.
	test_hash_algo='sha1'
}

# Load common hash metadata and common placeholder object IDs for use with
# test_oid.
test_oid_init () {
	test -n "$test_hash_algo" || test_detect_hash &&
	test_oid_cache <"$TEST_DIRECTORY/oid-info/hash-info" &&
	test_oid_cache <"$TEST_DIRECTORY/oid-info/oid"
}

# Load key-value pairs from stdin suitable for use with test_oid.  Blank lines
# and lines starting with "#" are ignored.  Keys must be shell identifier
# characters.
#
# Examples:
# rawsz sha1:20
# rawsz sha256:32
test_oid_cache () {
	local tag rest k v &&

	{ test -n "$test_hash_algo" || test_detect_hash; } &&
	while read tag rest
	do
		case $tag in
		\#*)
			continue;;
		?*)
			# non-empty
			;;
		*)
			# blank line
			continue;;
		esac &&

		k="${rest%:*}" &&
		v="${rest#*:}" &&

		if ! expr "$k" : '[a-z0-9][a-z0-9]*$' >/dev/null
		then
			BUG 'bad hash algorithm'
		fi &&
		eval "test_oid_${k}_$tag=\"\$v\""
	done
}

# Look up a per-hash value based on a key ($1).  The value must have been loaded
# by test_oid_init or test_oid_cache.
test_oid () {
	local var="test_oid_${test_hash_algo}_$1" &&

	# If the variable is unset, we must be missing an entry for this
	# key-hash pair, so exit with an error.
	if eval "test -z \"\${$var+set}\""
	then
		BUG "undefined key '$1'"
	fi &&
	eval "printf '%s' \"\${$var}\""
}

# Insert a slash into an object ID so it can be used to reference a location
# under ".git/objects".  For example, "deadbeef..." becomes "de/adbeef..".
test_oid_to_path () {
	local basename=${1#??}
	echo "${1%$basename}/$basename"
}

# Choose a port number based on the test script's number and store it in
# the given variable name, unless that variable already contains a number.
test_set_port () {
	local var=$1 port

	if test $# -ne 1 || test -z "$var"
	then
		BUG "test_set_port requires a variable name"
	fi

	eval port=\$$var
	case "$port" in
	"")
		# No port is set in the given env var, use the test
		# number as port number instead.
		# Remove not only the leading 't', but all leading zeros
		# as well, so the arithmetic below won't (mis)interpret
		# a test number like '0123' as an octal value.
		port=${this_test#${this_test%%[1-9]*}}
		if test "${port:-0}" -lt 1024
		then
			# root-only port, use a larger one instead.
			port=$(($port + 10000))
		fi
		;;
	*[!0-9]*|0*)
		error >&7 "invalid port number: $port"
		;;
	*)
		# The user has specified the port.
		;;
	esac

	# Make sure that parallel '--stress' test jobs get different
	# ports.
	port=$(($port + ${GIT_TEST_STRESS_JOB_NR:-0}))
	eval $var=$port
}

# Compare a file containing rev-list bitmap traversal output to its non-bitmap
# counterpart. You can't just use test_cmp for this, because the two produce
# subtly different output:
#
#   - regular output is in traversal order, whereas bitmap is split by type,
#     with non-packed objects at the end
#
#   - regular output has a space and the pathname appended to non-commit
#     objects; bitmap output omits this
#
# This function normalizes and compares the two. The second file should
# always be the bitmap output.
test_bitmap_traversal () {
	if test "$1" = "--no-confirm-bitmaps"
	then
		shift
	elif cmp "$1" "$2"
	then
		echo >&2 "identical raw outputs; are you sure bitmaps were used?"
		return 1
	fi &&
	cut -d' ' -f1 "$1" | sort >"$1.normalized" &&
	sort "$2" >"$2.normalized" &&
	test_cmp "$1.normalized" "$2.normalized" &&
	rm -f "$1.normalized" "$2.normalized"
}
