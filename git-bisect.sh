#!/bin/sh

USAGE='[start|bad|good|next|reset|visualize|replay|log|run]'
LONG_USAGE='git bisect start [<bad> [<good>...]] [--] [<pathspec>...]
        reset bisect state and start bisection.
git bisect bad [<rev>]
        mark <rev> a known-bad revision.
git bisect good [<rev>...]
        mark <rev>... known-good revisions.
git bisect next
        find next bisection to test and check it out.
git bisect reset [<branch>]
        finish bisection search and go back to branch.
git bisect visualize
        show bisect status in gitk.
git bisect replay <logfile>
        replay bisection log.
git bisect log
        show bisect log.
git bisect run <cmd>...
        use <cmd>... to automatically bisect.'

. git-sh-setup
require_work_tree

sq() {
	@@PERL@@ -e '
		for (@ARGV) {
			s/'\''/'\'\\\\\'\''/g;
			print " '\''$_'\''";
		}
		print "\n";
	' "$@"
}

bisect_autostart() {
	test -d "$GIT_DIR/refs/bisect" || {
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
	# Verify HEAD. If we were bisecting before this, reset to the
	# top-of-line master first!
	#
	head=$(GIT_DIR="$GIT_DIR" git-symbolic-ref HEAD) ||
	die "Bad HEAD - I need a symbolic ref"
	case "$head" in
	refs/heads/bisect)
		if [ -s "$GIT_DIR/head-name" ]; then
		    branch=`cat "$GIT_DIR/head-name"`
		else
		    branch=master
	        fi
		git checkout $branch || exit
		;;
	refs/heads/*)
		[ -s "$GIT_DIR/head-name" ] && die "won't bisect on seeked tree"
		echo "$head" | sed 's#^refs/heads/##' >"$GIT_DIR/head-name"
		;;
	*)
		die "Bad HEAD - strange symbolic ref"
		;;
	esac

	#
	# Get rid of any old bisect state
	#
	bisect_clean_state
	mkdir "$GIT_DIR/refs/bisect"

	#
	# Check for one bad and then some good revisions.
	#
	has_double_dash=0
	for arg; do
	    case "$arg" in --) has_double_dash=1; break ;; esac
	done
	orig_args=$(sq "$@")
	bad_seen=0
	while [ $# -gt 0 ]; do
	    arg="$1"
	    case "$arg" in
	    --)
	        shift
		break
		;;
	    *)
	        rev=$(git-rev-parse --verify "$arg^{commit}" 2>/dev/null) || {
		    test $has_double_dash -eq 1 &&
		        die "'$arg' does not appear to be a valid revision"
		    break
		}
		if [ $bad_seen -eq 0 ]; then
		    bad_seen=1
		    bisect_write_bad "$rev"
		else
		    bisect_write_good "$rev"
		fi
	        shift
		;;
	    esac
        done

	sq "$@" >"$GIT_DIR/BISECT_NAMES"
	echo "git-bisect start$orig_args" >>"$GIT_DIR/BISECT_LOG"
	bisect_auto_next
}

bisect_bad() {
	bisect_autostart
	case "$#" in
	0)
		rev=$(git-rev-parse --verify HEAD) ;;
	1)
		rev=$(git-rev-parse --verify "$1^{commit}") ;;
	*)
		usage ;;
	esac || exit
	bisect_write_bad "$rev"
	echo "git-bisect bad $rev" >>"$GIT_DIR/BISECT_LOG"
	bisect_auto_next
}

bisect_write_bad() {
	rev="$1"
	echo "$rev" >"$GIT_DIR/refs/bisect/bad"
	echo "# bad: "$(git-show-branch $rev) >>"$GIT_DIR/BISECT_LOG"
}

bisect_good() {
	bisect_autostart
        case "$#" in
	0)    revs=$(git-rev-parse --verify HEAD) || exit ;;
	*)    revs=$(git-rev-parse --revs-only --no-flags "$@") &&
		test '' != "$revs" || die "Bad rev input: $@" ;;
	esac
	for rev in $revs
	do
		rev=$(git-rev-parse --verify "$rev^{commit}") || exit
		bisect_write_good "$rev"
		echo "git-bisect good $rev" >>"$GIT_DIR/BISECT_LOG"

	done
	bisect_auto_next
}

bisect_write_good() {
	rev="$1"
	echo "$rev" >"$GIT_DIR/refs/bisect/good-$rev"
	echo "# good: "$(git-show-branch $rev) >>"$GIT_DIR/BISECT_LOG"
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
			case "$(read yesno)" in [Nn]*) exit 1 ;; esac
		fi
		: bisect without good...
		;;
	*)
		THEN=''
		test -d "$GIT_DIR/refs/bisect" || {
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

	bad=$(git-rev-parse --verify refs/bisect/bad) &&
	good=$(git for-each-ref --format='^%(objectname)' \
		"refs/bisect/good-*" | tr '[\012]' ' ') &&
	eval="git-rev-list --bisect-vars $good $bad --" &&
	eval="$eval $(cat "$GIT_DIR/BISECT_NAMES")" &&
	eval=$(eval "$eval") &&
	eval "$eval" || exit

	if [ -z "$bisect_rev" ]; then
		echo "$bad was both good and bad"
		exit 1
	fi
	if [ "$bisect_rev" = "$bad" ]; then
		echo "$bisect_rev is first bad commit"
		git-diff-tree --pretty $bisect_rev
		exit 0
	fi

	echo "Bisecting: $bisect_nr revisions left to test after this"
	echo "$bisect_rev" >"$GIT_DIR/refs/heads/new-bisect"
	git checkout -q new-bisect || exit
	mv "$GIT_DIR/refs/heads/new-bisect" "$GIT_DIR/refs/heads/bisect" &&
	GIT_DIR="$GIT_DIR" git-symbolic-ref HEAD refs/heads/bisect
	git-show-branch "$bisect_rev"
}

bisect_visualize() {
	bisect_next_check fail
	not=`cd "$GIT_DIR/refs" && echo bisect/good-*`
	eval gitk bisect/bad --not $not -- $(cat "$GIT_DIR/BISECT_NAMES")
}

bisect_reset() {
	case "$#" in
	0) if [ -s "$GIT_DIR/head-name" ]; then
	       branch=`cat "$GIT_DIR/head-name"`
	   else
	       branch=master
	   fi ;;
	1) git-show-ref --verify --quiet -- "refs/heads/$1" || {
	       echo >&2 "$1 does not seem to be a valid branch"
	       exit 1
	   }
	   branch="$1" ;;
        *)
	    usage ;;
	esac
	if git checkout "$branch"; then
		rm -f "$GIT_DIR/head-name"
		bisect_clean_state
	fi
}

bisect_clean_state() {
	rm -fr "$GIT_DIR/refs/bisect"
	rm -f "$GIT_DIR/refs/heads/bisect"
	rm -f "$GIT_DIR/BISECT_LOG"
	rm -f "$GIT_DIR/BISECT_NAMES"
	rm -f "$GIT_DIR/BISECT_RUN"
}

bisect_replay () {
	test -r "$1" || {
		echo >&2 "cannot read $1 for replaying"
		exit 1
	}
	bisect_reset
	while read bisect command rev
	do
		test "$bisect" = "git-bisect" || continue
		case "$command" in
		start)
			cmd="bisect_start $rev"
			eval "$cmd"
			;;
		good)
			echo "$rev" >"$GIT_DIR/refs/bisect/good-$rev"
			echo "# good: "$(git-show-branch $rev) >>"$GIT_DIR/BISECT_LOG"
			echo "git-bisect good $rev" >>"$GIT_DIR/BISECT_LOG"
			;;
		bad)
			echo "$rev" >"$GIT_DIR/refs/bisect/bad"
			echo "# bad: "$(git-show-branch $rev) >>"$GIT_DIR/BISECT_LOG"
			echo "git-bisect bad $rev" >>"$GIT_DIR/BISECT_LOG"
			;;
		*)
			echo >&2 "?? what are you talking about?"
			exit 1 ;;
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

      # Use "bisect_good" or "bisect_bad"
      # depending on run success or failure.
      if [ $res -gt 0 ]; then
	  next_bisect='bisect_bad'
      else
	  next_bisect='bisect_good'
      fi

      # We have to use a subshell because bisect_good or
      # bisect_bad functions can exit.
      ( $next_bisect > "$GIT_DIR/BISECT_RUN" )
      res=$?

      cat "$GIT_DIR/BISECT_RUN"

      if [ $res -ne 0 ]; then
	  echo >&2 "bisect run failed:"
	  echo >&2 "$next_bisect exited with error code $res"
	  exit $res
      fi

      if grep "is first bad commit" "$GIT_DIR/BISECT_RUN" > /dev/null; then
	  echo "bisect run success"
	  exit 0;
      fi

    done
}


case "$#" in
0)
    usage ;;
*)
    cmd="$1"
    shift
    case "$cmd" in
    start)
        bisect_start "$@" ;;
    bad)
        bisect_bad "$@" ;;
    good)
        bisect_good "$@" ;;
    next)
        # Not sure we want "next" at the UI level anymore.
        bisect_next "$@" ;;
    visualize)
	bisect_visualize "$@" ;;
    reset)
        bisect_reset "$@" ;;
    replay)
	bisect_replay "$@" ;;
    log)
	cat "$GIT_DIR/BISECT_LOG" ;;
    run)
        bisect_run "$@" ;;
    *)
        usage ;;
    esac
esac
