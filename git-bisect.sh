#!/bin/sh

USAGE='[help|start|bad|good|new|old|terms|skip|next|reset|visualize|view|replay|log|run]'
LONG_USAGE='git bisect help
	print this long help message.
git bisect start [--term-{old,good}=<term> --term-{new,bad}=<term>]
		 [--no-checkout] [<bad> [<good>...]] [--] [<pathspec>...]
	reset bisect state and start bisection.
git bisect (bad|new) [<rev>]
	mark <rev> a known-bad revision/
		a revision after change in a given property.
git bisect (good|old) [<rev>...]
	mark <rev>... known-good revisions/
		revisions before change in a given property.
git bisect terms [--term-good | --term-bad]
	show the terms used for old and new commits (default: bad, good)
git bisect skip [(<rev>|<range>)...]
	mark <rev>... untestable revisions.
git bisect next
	find next bisection to test and check it out.
git bisect reset [<commit>]
	finish bisection search and go back to commit.
git bisect (visualize|view)
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
	git bisect--helper --bisect-start $@ || exit

	#
	# Change state.
	# In case of mistaken revs or checkout error, or signals received,
	# "bisect_auto_next" below may exit or misbehave.
	# We have to trap this to be able to clean up using
	# "bisect_clean_state".
	#
	trap 'git bisect--helper --bisect-clean-state' 0
	trap 'exit 255' 1 2 3 15

	#
	# Check if we can proceed to the next bisect state.
	#
	get_terms
	bisect_auto_next

	trap '-' 0
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
	git bisect--helper --check-and-set-terms $state $TERM_GOOD $TERM_BAD || exit
	get_terms
	case "$#,$state" in
	0,*)
		die "Please call 'bisect_state' with at least one argument." ;;
	1,"$TERM_BAD"|1,"$TERM_GOOD"|1,skip)
		bisected_head=$(bisect_head)
		rev=$(git rev-parse --verify "$bisected_head") ||
			die "$(eval_gettext "Bad rev input: \$bisected_head")"
		git bisect--helper --bisect-write "$state" "$rev" "$TERM_GOOD" "$TERM_BAD" || exit
		git bisect--helper --check-expected-revs "$rev" ;;
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
			git bisect--helper --bisect-write "$state" "$rev" "$TERM_GOOD" "$TERM_BAD" || exit
		done
		git bisect--helper --check-expected-revs $hash_list ;;
	*,"$TERM_BAD")
		die "$(eval_gettext "'git bisect \$TERM_BAD' can take only one argument.")" ;;
	*)
		usage ;;
	esac
	bisect_auto_next
}

bisect_auto_next() {
	git bisect--helper --bisect-next-check $TERM_GOOD $TERM_BAD && bisect_next || :
}

bisect_next() {
	case "$#" in 0) ;; *) usage ;; esac
	bisect_autostart
	git bisect--helper --bisect-next-check $TERM_GOOD $TERM_BAD $TERM_GOOD|| exit

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
	git bisect--helper --bisect-next-check $TERM_GOOD $TERM_BAD fail || exit

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

bisect_replay () {
	file="$1"
	test "$#" -eq 1 || die "$(gettext "No logfile given")"
	test -r "$file" || die "$(eval_gettext "cannot read \$file for replaying")"
	git bisect--helper --bisect-reset || exit
	while read git bisect command rev
	do
		test "$git $bisect" = "git bisect" || test "$git" = "git-bisect" || continue
		if test "$git" = "git-bisect"
		then
			rev="$command"
			command="$bisect"
		fi
		get_terms
		git bisect--helper --check-and-set-terms "$command" "$TERM_GOOD" "$TERM_BAD" || exit
		get_terms
		case "$command" in
		start)
			cmd="bisect_start $rev"
			eval "$cmd" ;;
		"$TERM_GOOD"|"$TERM_BAD"|skip)
			git bisect--helper --bisect-write "$command" "$rev" "$TERM_GOOD" "$TERM_BAD" || exit;;
		terms)
			git bisect--helper --bisect-terms $rev || exit;;
		*)
			die "$(gettext "?? what are you talking about?")" ;;
		esac
	done <"$file"
	bisect_auto_next
}

bisect_run () {
	git bisect--helper --bisect-next-check $TERM_GOOD $TERM_BAD fail || exit

	test -n "$*" || die "$(gettext "bisect run failed: no command provided.")"

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
	bad|good|new|old|"$TERM_BAD"|"$TERM_GOOD")
		bisect_state "$cmd" "$@" ;;
	skip)
		bisect_skip "$@" ;;
	next)
		# Not sure we want "next" at the UI level anymore.
		bisect_next "$@" ;;
	visualize|view)
		bisect_visualize "$@" ;;
	reset)
		git bisect--helper --bisect-reset "$@" ;;
	replay)
		bisect_replay "$@" ;;
	log)
		bisect_log ;;
	run)
		bisect_run "$@" ;;
	terms)
		git bisect--helper --bisect-terms "$@" || exit;;
	*)
		usage ;;
	esac
esac
