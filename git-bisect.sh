#!/bin/sh

USAGE='[help|start|bad|good|new|old|terms|skip|next|reset|visualize|view|replay|log|run]'
LONG_USAGE='but bisect help
	print this long help message.
but bisect start [--term-{new,bad}=<term> --term-{old,good}=<term>]
		 [--no-checkout] [--first-parent] [<bad> [<good>...]] [--] [<pathspec>...]
	reset bisect state and start bisection.
but bisect (bad|new) [<rev>]
	mark <rev> a known-bad revision/
		a revision after change in a given property.
but bisect (good|old) [<rev>...]
	mark <rev>... known-good revisions/
		revisions before change in a given property.
but bisect terms [--term-good | --term-bad]
	show the terms used for old and new cummits (default: bad, good)
but bisect skip [(<rev>|<range>)...]
	mark <rev>... untestable revisions.
but bisect next
	find next bisection to test and check it out.
but bisect reset [<cummit>]
	finish bisection search and go back to cummit.
but bisect (visualize|view)
	show bisect status in butk.
but bisect replay <logfile>
	replay bisection log.
but bisect log
	show bisect log.
but bisect run <cmd>...
	use <cmd>... to automatically bisect.

Please use "but help bisect" to get the full man page.'

OPTIONS_SPEC=
. but-sh-setup

TERM_BAD=bad
TERM_GOOD=good

get_terms () {
	if test -s "$BUT_DIR/BISECT_TERMS"
	then
		{
		read TERM_BAD
		read TERM_GOOD
		} <"$BUT_DIR/BISECT_TERMS"
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
		but bisect -h ;;
	start)
		but bisect--helper --bisect-start "$@" ;;
	bad|good|new|old|"$TERM_BAD"|"$TERM_GOOD")
		but bisect--helper --bisect-state "$cmd" "$@" ;;
	skip)
		but bisect--helper --bisect-skip "$@" || exit;;
	next)
		# Not sure we want "next" at the UI level anymore.
		but bisect--helper --bisect-next "$@" || exit ;;
	visualize|view)
		but bisect--helper --bisect-visualize "$@" || exit;;
	reset)
		but bisect--helper --bisect-reset "$@" ;;
	replay)
		but bisect--helper --bisect-replay "$@" || exit;;
	log)
		but bisect--helper --bisect-log || exit ;;
	run)
		but bisect--helper --bisect-run "$@" || exit;;
	terms)
		but bisect--helper --bisect-terms "$@" || exit;;
	*)
		usage ;;
	esac
esac
