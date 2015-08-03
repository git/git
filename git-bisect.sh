#!/bin/sh

USAGE='[help|start|bad|good|skip|next|reset|visualize|replay|log|run]'
LONG_USAGE='git bisect help
	print this long help message.
git bisect start [--no-checkout] [<bad> [<good>...]] [--] [<pathspec>...]
	reset bisect state and start bisection.
git bisect bad [<rev>]
	mark <rev> a known-bad revision.
git bisect good [<rev>...]
	mark <rev>... known-good revisions.
git bisect skip [(<rev>|<range>)...]
	mark <rev>... untestable revisions.
git bisect next
	find next bisection to test and check it out.
git bisect reset [<commit>]
	finish bisection search and go back to commit.
git bisect visualize
	show bisect status in gitk.
git bisect replay <logfile>
	replay bisection log.
git bisect log
	show bisect log.
git bisect run <cmd>...
	use <cmd>... to automatically bisect.

Please use "git help bisect" to get the full man page.'

OPTIONS_SPEC=
. git-sh-setup
. git-sh-i18n

_x40='[0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f]'
_x40="$_x40$_x40$_x40$_x40$_x40$_x40$_x40$_x40"
TERM_BAD=bad
TERM_GOOD=good

bisect_head()
{
	if test -f "$GIT_DIR/BISECT_HEAD"
	then
		echo BISECT_HEAD
	else
		echo HEAD
	fi
}

bisect_autostart() {
	test -s "$GIT_DIR/BISECT_START" || {
		gettextln "You need to start by \"git bisect start\"" >&2
		if test -t 0
		then
			# TRANSLATORS: Make sure to include [Y] and [n] in your
			# translation. The program will only accept English input
			# at this point.
			gettext "Do you want me to do it for you [Y/n]? " >&2
			read yesno
			case "$yesno" in
			[Nn]*)
				exit ;;
			esac
			bisect_start
		else
			exit 1
		fi
	}
}

bisect_start() {
	#
	# Check for one bad and then some good revisions.
	#
	has_double_dash=0
	for arg; do
		case "$arg" in --) has_double_dash=1; break ;; esac
	done
	orig_args=$(git rev-parse --sq-quote "$@")
	bad_seen=0
	eval=''
	must_write_terms=0
	revs=''
	if test "z$(git rev-parse --is-bare-repository)" != zfalse
	then
		mode=--no-checkout
	else
		mode=''
	fi
	while [ $# -gt 0 ]; do
		arg="$1"
		case "$arg" in
		--)
			shift
			break
		;;
		--no-checkout)
			mode=--no-checkout
			shift ;;
		--*)
			die "$(eval_gettext "unrecognised option: '\$arg'")" ;;
		*)
			rev=$(git rev-parse -q --verify "$arg^{commit}") || {
				test $has_double_dash -eq 1 &&
				die "$(eval_gettext "'\$arg' does not appear to be a valid revision")"
				break
			}
			revs="$revs $rev"
			shift
			;;
		esac
	done

	for rev in $revs
	do
		# The user ran "git bisect start <sha1>
		# <sha1>", hence did not explicitly specify
		# the terms, but we are already starting to
		# set references named with the default terms,
		# and won't be able to change afterwards.
		must_write_terms=1

		case $bad_seen in
		0) state=$TERM_BAD ; bad_seen=1 ;;
		*) state=$TERM_GOOD ;;
		esac
		eval="$eval bisect_write '$state' '$rev' 'nolog' &&"
	done
	#
	# Verify HEAD.
	#
	head=$(GIT_DIR="$GIT_DIR" git symbolic-ref -q HEAD) ||
	head=$(GIT_DIR="$GIT_DIR" git rev-parse --verify HEAD) ||
	die "$(gettext "Bad HEAD - I need a HEAD")"

	#
	# Check if we are bisecting.
	#
	start_head=''
	if test -s "$GIT_DIR/BISECT_START"
	then
		# Reset to the rev from where we started.
		start_head=$(cat "$GIT_DIR/BISECT_START")
		if test "z$mode" != "z--no-checkout"
		then
			git checkout "$start_head" -- ||
			die "$(eval_gettext "Checking out '\$start_head' failed. Try 'git bisect reset <valid-branch>'.")"
		fi
	else
		# Get rev from where we start.
		case "$head" in
		refs/heads/*|$_x40)
			# This error message should only be triggered by
			# cogito usage, and cogito users should understand
			# it relates to cg-seek.
			[ -s "$GIT_DIR/head-name" ] &&
				die "$(gettext "won't bisect on cg-seek'ed tree")"
			start_head="${head#refs/heads/}"
			;;
		*)
			die "$(gettext "Bad HEAD - strange symbolic ref")"
			;;
		esac
	fi

	#
	# Get rid of any old bisect state.
	#
	bisect_clean_state || exit

	#
	# Change state.
	# In case of mistaken revs or checkout error, or signals received,
	# "bisect_auto_next" below may exit or misbehave.
	# We have to trap this to be able to clean up using
	# "bisect_clean_state".
	#
	trap 'bisect_clean_state' 0
	trap 'exit 255' 1 2 3 15

	#
	# Write new start state.
	#
	echo "$start_head" >"$GIT_DIR/BISECT_START" && {
		test "z$mode" != "z--no-checkout" ||
		git update-ref --no-deref BISECT_HEAD "$start_head"
	} &&
	git rev-parse --sq-quote "$@" >"$GIT_DIR/BISECT_NAMES" &&
	eval "$eval true" &&
	if test $must_write_terms -eq 1
	then
		write_terms "$TERM_BAD" "$TERM_GOOD"
	fi &&
	echo "git bisect start$orig_args" >>"$GIT_DIR/BISECT_LOG" || exit
	#
	# Check if we can proceed to the next bisect state.
	#
	bisect_auto_next

	trap '-' 0
}

bisect_write() {
	state="$1"
	rev="$2"
	nolog="$3"
	case "$state" in
		"$TERM_BAD")
			tag="$state" ;;
		"$TERM_GOOD"|skip)
			tag="$state"-"$rev" ;;
		*)
			die "$(eval_gettext "Bad bisect_write argument: \$state")" ;;
	esac
	git update-ref "refs/bisect/$tag" "$rev" || exit
	echo "# $state: $(git show-branch $rev)" >>"$GIT_DIR/BISECT_LOG"
	test -n "$nolog" || echo "git bisect $state $rev" >>"$GIT_DIR/BISECT_LOG"
}

is_expected_rev() {
	test -f "$GIT_DIR/BISECT_EXPECTED_REV" &&
	test "$1" = $(cat "$GIT_DIR/BISECT_EXPECTED_REV")
}

check_expected_revs() {
	for _rev in "$@"; do
		if ! is_expected_rev "$_rev"
		then
			rm -f "$GIT_DIR/BISECT_ANCESTORS_OK"
			rm -f "$GIT_DIR/BISECT_EXPECTED_REV"
			return
		fi
	done
}

bisect_skip() {
	all=''
	for arg in "$@"
	do
		case "$arg" in
		*..*)
			revs=$(git rev-list "$arg") || die "$(eval_gettext "Bad rev input: \$arg")" ;;
		*)
			revs=$(git rev-parse --sq-quote "$arg") ;;
		esac
		all="$all $revs"
	done
	eval bisect_state 'skip' $all
}

bisect_state() {
	bisect_autostart
	state=$1
	check_and_set_terms $state
	case "$#,$state" in
	0,*)
		die "$(gettext "Please call 'bisect_state' with at least one argument.")" ;;
	1,"$TERM_BAD"|1,"$TERM_GOOD"|1,skip)
		rev=$(git rev-parse --verify $(bisect_head)) ||
			die "$(gettext "Bad rev input: $(bisect_head)")"
		bisect_write "$state" "$rev"
		check_expected_revs "$rev" ;;
	2,"$TERM_BAD"|*,"$TERM_GOOD"|*,skip)
		shift
		hash_list=''
		for rev in "$@"
		do
			sha=$(git rev-parse --verify "$rev^{commit}") ||
				die "$(eval_gettext "Bad rev input: \$rev")"
			hash_list="$hash_list $sha"
		done
		for rev in $hash_list
		do
			bisect_write "$state" "$rev"
		done
		check_expected_revs $hash_list ;;
	*,"$TERM_BAD")
		die "$(eval_gettext "'git bisect \$TERM_BAD' can take only one argument.")" ;;
	*)
		usage ;;
	esac
	bisect_auto_next
}

bisect_next_check() {
	missing_good= missing_bad=
	git show-ref -q --verify refs/bisect/$TERM_BAD || missing_bad=t
	test -n "$(git for-each-ref "refs/bisect/$TERM_GOOD-*")" || missing_good=t

	case "$missing_good,$missing_bad,$1" in
	,,*)
		: have both $TERM_GOOD and $TERM_BAD - ok
		;;
	*,)
		# do not have both but not asked to fail - just report.
		false
		;;
	t,,"$TERM_GOOD")
		# have bad but not good.  we could bisect although
		# this is less optimum.
		eval_gettextln "Warning: bisecting only with a \$TERM_BAD commit." >&2
		if test -t 0
		then
			# TRANSLATORS: Make sure to include [Y] and [n] in your
			# translation. The program will only accept English input
			# at this point.
			gettext "Are you sure [Y/n]? " >&2
			read yesno
			case "$yesno" in [Nn]*) exit 1 ;; esac
		fi
		: bisect without $TERM_GOOD...
		;;
	*)
		bad_syn=$(bisect_voc bad)
		good_syn=$(bisect_voc good)
		if test -s "$GIT_DIR/BISECT_START"
		then

			eval_gettextln "You need to give me at least one \$bad_syn and one \$good_syn revision.
(You can use \"git bisect \$bad_syn\" and \"git bisect \$good_syn\" for that.)" >&2
		else
			eval_gettextln "You need to start by \"git bisect start\".
You then need to give me at least one \$good_syn and one \$bad_syn revision.
(You can use \"git bisect \$bad_syn\" and \"git bisect \$good_syn\" for that.)" >&2
		fi
		exit 1 ;;
	esac
}

bisect_auto_next() {
	bisect_next_check && bisect_next || :
}

bisect_next() {
	case "$#" in 0) ;; *) usage ;; esac
	bisect_autostart
	bisect_next_check $TERM_GOOD

	# Perform all bisection computation, display and checkout
	git bisect--helper --next-all $(test -f "$GIT_DIR/BISECT_HEAD" && echo --no-checkout)
	res=$?

	# Check if we should exit because bisection is finished
	if test $res -eq 10
	then
		bad_rev=$(git show-ref --hash --verify refs/bisect/$TERM_BAD)
		bad_commit=$(git show-branch $bad_rev)
		echo "# first $TERM_BAD commit: $bad_commit" >>"$GIT_DIR/BISECT_LOG"
		exit 0
	elif test $res -eq 2
	then
		echo "# only skipped commits left to test" >>"$GIT_DIR/BISECT_LOG"
		good_revs=$(git for-each-ref --format="%(objectname)" "refs/bisect/$TERM_GOOD-*")
		for skipped in $(git rev-list refs/bisect/$TERM_BAD --not $good_revs)
		do
			skipped_commit=$(git show-branch $skipped)
			echo "# possible first $TERM_BAD commit: $skipped_commit" >>"$GIT_DIR/BISECT_LOG"
		done
		exit $res
	fi

	# Check for an error in the bisection process
	test $res -ne 0 && exit $res

	return 0
}

bisect_visualize() {
	bisect_next_check fail

	if test $# = 0
	then
		if test -n "${DISPLAY+set}${SESSIONNAME+set}${MSYSTEM+set}${SECURITYSESSIONID+set}" &&
			type gitk >/dev/null 2>&1
		then
			set gitk
		else
			set git log
		fi
	else
		case "$1" in
		git*|tig) ;;
		-*)	set git log "$@" ;;
		*)	set git "$@" ;;
		esac
	fi

	eval '"$@"' --bisect -- $(cat "$GIT_DIR/BISECT_NAMES")
}

bisect_reset() {
	test -s "$GIT_DIR/BISECT_START" || {
		gettextln "We are not bisecting."
		return
	}
	case "$#" in
	0) branch=$(cat "$GIT_DIR/BISECT_START") ;;
	1) git rev-parse --quiet --verify "$1^{commit}" >/dev/null || {
			invalid="$1"
			die "$(eval_gettext "'\$invalid' is not a valid commit")"
		}
		branch="$1" ;;
	*)
		usage ;;
	esac

	if ! test -f "$GIT_DIR/BISECT_HEAD" && ! git checkout "$branch" --
	then
		die "$(eval_gettext "Could not check out original HEAD '\$branch'.
Try 'git bisect reset <commit>'.")"
	fi
	bisect_clean_state
}

bisect_clean_state() {
	# There may be some refs packed during bisection.
	git for-each-ref --format='%(refname) %(objectname)' refs/bisect/\* |
	while read ref hash
	do
		git update-ref -d $ref $hash || exit
	done
	rm -f "$GIT_DIR/BISECT_EXPECTED_REV" &&
	rm -f "$GIT_DIR/BISECT_ANCESTORS_OK" &&
	rm -f "$GIT_DIR/BISECT_LOG" &&
	rm -f "$GIT_DIR/BISECT_NAMES" &&
	rm -f "$GIT_DIR/BISECT_RUN" &&
	rm -f "$GIT_DIR/BISECT_TERMS" &&
	# Cleanup head-name if it got left by an old version of git-bisect
	rm -f "$GIT_DIR/head-name" &&
	git update-ref -d --no-deref BISECT_HEAD &&
	# clean up BISECT_START last
	rm -f "$GIT_DIR/BISECT_START"
}

bisect_replay () {
	file="$1"
	test "$#" -eq 1 || die "$(gettext "No logfile given")"
	test -r "$file" || die "$(eval_gettext "cannot read \$file for replaying")"
	bisect_reset
	while read git bisect command rev
	do
		test "$git $bisect" = "git bisect" || test "$git" = "git-bisect" || continue
		if test "$git" = "git-bisect"
		then
			rev="$command"
			command="$bisect"
		fi
		get_terms
		check_and_set_terms "$command"
		case "$command" in
		start)
			cmd="bisect_start $rev"
			eval "$cmd" ;;
		"$TERM_GOOD"|"$TERM_BAD"|skip)
			bisect_write "$command" "$rev" ;;
		*)
			die "$(gettext "?? what are you talking about?")" ;;
		esac
	done <"$file"
	bisect_auto_next
}

bisect_run () {
	bisect_next_check fail

	while true
	do
		command="$@"
		eval_gettextln "running \$command"
		"$@"
		res=$?

		# Check for really bad run error.
		if [ $res -lt 0 -o $res -ge 128 ]
		then
			eval_gettextln "bisect run failed:
exit code \$res from '\$command' is < 0 or >= 128" >&2
			exit $res
		fi

		# Find current state depending on run success or failure.
		# A special exit code of 125 means cannot test.
		if [ $res -eq 125 ]
		then
			state='skip'
		elif [ $res -gt 0 ]
		then
			state="$TERM_BAD"
		else
			state="$TERM_GOOD"
		fi

		# We have to use a subshell because "bisect_state" can exit.
		( bisect_state $state >"$GIT_DIR/BISECT_RUN" )
		res=$?

		cat "$GIT_DIR/BISECT_RUN"

		if sane_grep "first $TERM_BAD commit could be any of" "$GIT_DIR/BISECT_RUN" \
			>/dev/null
		then
			gettextln "bisect run cannot continue any more" >&2
			exit $res
		fi

		if [ $res -ne 0 ]
		then
			eval_gettextln "bisect run failed:
'bisect_state \$state' exited with error code \$res" >&2
			exit $res
		fi

		if sane_grep "is the first $TERM_BAD commit" "$GIT_DIR/BISECT_RUN" >/dev/null
		then
			gettextln "bisect run success"
			exit 0;
		fi

	done
}

bisect_log () {
	test -s "$GIT_DIR/BISECT_LOG" || die "$(gettext "We are not bisecting.")"
	cat "$GIT_DIR/BISECT_LOG"
}

get_terms () {
	if test -s "$GIT_DIR/BISECT_TERMS"
	then
		{
		read TERM_BAD
		read TERM_GOOD
		} <"$GIT_DIR/BISECT_TERMS"
	fi
}

write_terms () {
	TERM_BAD=$1
	TERM_GOOD=$2
	printf '%s\n%s\n' "$TERM_BAD" "$TERM_GOOD" >"$GIT_DIR/BISECT_TERMS"
}

check_and_set_terms () {
	cmd="$1"
	case "$cmd" in
	skip|start|terms) ;;
	*)
		if test -s "$GIT_DIR/BISECT_TERMS" && test "$cmd" != "$TERM_BAD" && test "$cmd" != "$TERM_GOOD"
		then
			die "$(eval_gettext "Invalid command: you're currently in a \$TERM_BAD/\$TERM_GOOD bisect.")"
		fi
		case "$cmd" in
		bad|good)
			if ! test -s "$GIT_DIR/BISECT_TERMS"
			then
				write_terms bad good
			fi
			;;
		esac ;;
	esac
}

bisect_voc () {
	case "$1" in
	bad) echo "bad" ;;
	good) echo "good" ;;
	esac
}

case "$#" in
0)
	usage ;;
*)
	cmd="$1"
	get_terms
	shift
	case "$cmd" in
	help)
		git bisect -h ;;
	start)
		bisect_start "$@" ;;
	bad|good|"$TERM_BAD"|"$TERM_GOOD")
		bisect_state "$cmd" "$@" ;;
	skip)
		bisect_skip "$@" ;;
	next)
		# Not sure we want "next" at the UI level anymore.
		bisect_next "$@" ;;
	visualize|view)
		bisect_visualize "$@" ;;
	reset)
		bisect_reset "$@" ;;
	replay)
		bisect_replay "$@" ;;
	log)
		bisect_log ;;
	run)
		bisect_run "$@" ;;
	*)
		usage ;;
	esac
esac
