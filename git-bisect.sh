#!/bin/sh

USAGE='[help|start|bad|good|skip|next|reset|visualize|replay|log|run]'
LONG_USAGE='git bisect help
        print this long help message.
git bisect start [<bad> [<good>...]] [--] [<pathspec>...]
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
require_work_tree

_x40='[0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f]'
_x40="$_x40$_x40$_x40$_x40$_x40$_x40$_x40$_x40"

bisect_autostart() {
	test -s "$GIT_DIR/BISECT_START" || {
		echo >&2 'You need to start by "git bisect start"'
		if test -t 0
		then
			echo >&2 -n 'Do you want me to do it for you [Y/n]? '
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
	# Verify HEAD.
	#
	head=$(GIT_DIR="$GIT_DIR" git symbolic-ref -q HEAD) ||
	head=$(GIT_DIR="$GIT_DIR" git rev-parse --verify HEAD) ||
	die "Bad HEAD - I need a HEAD"

	#
	# Check if we are bisecting.
	#
	start_head=''
	if test -s "$GIT_DIR/BISECT_START"
	then
		# Reset to the rev from where we started.
		start_head=$(cat "$GIT_DIR/BISECT_START")
		git checkout "$start_head" -- || exit
	else
		# Get rev from where we start.
		case "$head" in
		refs/heads/*|$_x40)
			# This error message should only be triggered by
			# cogito usage, and cogito users should understand
			# it relates to cg-seek.
			[ -s "$GIT_DIR/head-name" ] &&
				die "won't bisect on seeked tree"
			start_head="${head#refs/heads/}"
			;;
		*)
			die "Bad HEAD - strange symbolic ref"
			;;
		esac
	fi

	#
	# Get rid of any old bisect state.
	#
	bisect_clean_state || exit

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
	while [ $# -gt 0 ]; do
	    arg="$1"
	    case "$arg" in
	    --)
		shift
		break
		;;
	    *)
		rev=$(git rev-parse -q --verify "$arg^{commit}") || {
		    test $has_double_dash -eq 1 &&
		        die "'$arg' does not appear to be a valid revision"
		    break
		}
		case $bad_seen in
		0) state='bad' ; bad_seen=1 ;;
		*) state='good' ;;
		esac
		eval="$eval bisect_write '$state' '$rev' 'nolog'; "
		shift
		;;
	    esac
	done

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
	echo "$start_head" >"$GIT_DIR/BISECT_START" &&
	git rev-parse --sq-quote "$@" >"$GIT_DIR/BISECT_NAMES" &&
	eval "$eval" &&
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
		bad)		tag="$state" ;;
		good|skip)	tag="$state"-"$rev" ;;
		*)		die "Bad bisect_write argument: $state" ;;
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
		if ! is_expected_rev "$_rev"; then
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
                revs=$(git rev-list "$arg") || die "Bad rev input: $arg" ;;
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
	case "$#,$state" in
	0,*)
		die "Please call 'bisect_state' with at least one argument." ;;
	1,bad|1,good|1,skip)
		rev=$(git rev-parse --verify HEAD) ||
			die "Bad rev input: HEAD"
		bisect_write "$state" "$rev"
		check_expected_revs "$rev" ;;
	2,bad|*,good|*,skip)
		shift
		eval=''
		for rev in "$@"
		do
			sha=$(git rev-parse --verify "$rev^{commit}") ||
				die "Bad rev input: $rev"
			eval="$eval bisect_write '$state' '$sha'; "
		done
		eval "$eval"
		check_expected_revs "$@" ;;
	*,bad)
		die "'git bisect bad' can take only one argument." ;;
	*)
		usage ;;
	esac
	bisect_auto_next
}

bisect_next_check() {
	missing_good= missing_bad=
	git show-ref -q --verify refs/bisect/bad || missing_bad=t
	test -n "$(git for-each-ref "refs/bisect/good-*")" || missing_good=t

	case "$missing_good,$missing_bad,$1" in
	,,*)
		: have both good and bad - ok
		;;
	*,)
		# do not have both but not asked to fail - just report.
		false
		;;
	t,,good)
		# have bad but not good.  we could bisect although
		# this is less optimum.
		echo >&2 'Warning: bisecting only with a bad commit.'
		if test -t 0
		then
			printf >&2 'Are you sure [Y/n]? '
			read yesno
			case "$yesno" in [Nn]*) exit 1 ;; esac
		fi
		: bisect without good...
		;;
	*)
		THEN=''
		test -s "$GIT_DIR/BISECT_START" || {
			echo >&2 'You need to start by "git bisect start".'
			THEN='then '
		}
		echo >&2 'You '$THEN'need to give me at least one good' \
			'and one bad revisions.'
		echo >&2 '(You can use "git bisect bad" and' \
			'"git bisect good" for that.)'
		exit 1 ;;
	esac
}

bisect_auto_next() {
	bisect_next_check && bisect_next || :
}

bisect_next() {
	case "$#" in 0) ;; *) usage ;; esac
	bisect_autostart
	bisect_next_check good

	# Perform all bisection computation, display and checkout
	git bisect--helper --next-all
	res=$?

        # Check if we should exit because bisection is finished
	test $res -eq 10 && exit 0

	# Check for an error in the bisection process
	test $res -ne 0 && exit $res

	return 0
}

bisect_visualize() {
	bisect_next_check fail

	if test $# = 0
	then
		if test -n "${DISPLAY+set}${SESSIONNAME+set}${MSYSTEM+set}${SECURITYSESSIONID+set}" &&
		   type gitk >/dev/null 2>&1; then
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
		echo "We are not bisecting."
		return
	}
	case "$#" in
	0) branch=$(cat "$GIT_DIR/BISECT_START") ;;
	1) git rev-parse --quiet --verify "$1^{commit}" > /dev/null ||
	       die "'$1' is not a valid commit"
	   branch="$1" ;;
	*)
	    usage ;;
	esac
	if git checkout "$branch" -- ; then
		bisect_clean_state
	else
		die "Could not check out original HEAD '$branch'." \
				"Try 'git bisect reset <commit>'."
	fi
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
	# Cleanup head-name if it got left by an old version of git-bisect
	rm -f "$GIT_DIR/head-name" &&

	rm -f "$GIT_DIR/BISECT_START"
}

bisect_replay () {
	test "$#" -eq 1 || die "No logfile given"
	test -r "$1" || die "cannot read $1 for replaying"
	bisect_reset
	while read git bisect command rev
	do
		test "$git $bisect" = "git bisect" -o "$git" = "git-bisect" || continue
		if test "$git" = "git-bisect"; then
			rev="$command"
			command="$bisect"
		fi
		case "$command" in
		start)
			cmd="bisect_start $rev"
			eval "$cmd" ;;
		good|bad|skip)
			bisect_write "$command" "$rev" ;;
		*)
			die "?? what are you talking about?" ;;
		esac
	done <"$1"
	bisect_auto_next
}

bisect_run () {
    bisect_next_check fail

    while true
    do
      echo "running $@"
      "$@"
      res=$?

      # Check for really bad run error.
      if [ $res -lt 0 -o $res -ge 128 ]; then
	  echo >&2 "bisect run failed:"
	  echo >&2 "exit code $res from '$@' is < 0 or >= 128"
	  exit $res
      fi

      # Find current state depending on run success or failure.
      # A special exit code of 125 means cannot test.
      if [ $res -eq 125 ]; then
	  state='skip'
      elif [ $res -gt 0 ]; then
	  state='bad'
      else
	  state='good'
      fi

      # We have to use a subshell because "bisect_state" can exit.
      ( bisect_state $state > "$GIT_DIR/BISECT_RUN" )
      res=$?

      cat "$GIT_DIR/BISECT_RUN"

      if sane_grep "first bad commit could be any of" "$GIT_DIR/BISECT_RUN" \
		> /dev/null; then
	  echo >&2 "bisect run cannot continue any more"
	  exit $res
      fi

      if [ $res -ne 0 ]; then
	  echo >&2 "bisect run failed:"
	  echo >&2 "'bisect_state $state' exited with error code $res"
	  exit $res
      fi

      if sane_grep "is the first bad commit" "$GIT_DIR/BISECT_RUN" > /dev/null; then
	  echo "bisect run success"
	  exit 0;
      fi

    done
}

bisect_log () {
	test -s "$GIT_DIR/BISECT_LOG" || die "We are not bisecting."
	cat "$GIT_DIR/BISECT_LOG"
}

case "$#" in
0)
    usage ;;
*)
    cmd="$1"
    shift
    case "$cmd" in
    help)
        git bisect -h ;;
    start)
        bisect_start "$@" ;;
    bad|good)
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
