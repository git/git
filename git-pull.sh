#!/bin/sh
#
# Copyright (c) 2005 Junio C Hamano
#
# Fetch one or more remote refs and merge it/them into the current HEAD.

. git-sh-setup || die "Not a git archive"

usage () {
    die "git pull [-n] [-s strategy]... <repo> <head>..."
}

strategy_args= no_summary=
while case "$#,$1" in 0) break ;; *,-*) ;; *) break ;; esac
do
	case "$1" in
	-n|--n|--no|--no-|--no-s|--no-su|--no-sum|--no-summ|\
		--no-summa|--no-summar|--no-summary)
		no_summary=-n ;;
	-s=*|--s=*|--st=*|--str=*|--stra=*|--strat=*|--strate=*|\
		--strateg=*|--strategy=*|\
	-s|--s|--st|--str|--stra|--strat|--strate|--strateg|--strategy)
		case "$#,$1" in
		*,*=*)
			strategy=`expr "$1" : '-[^=]*=\(.*\)'` ;;
		1,*)
			usage ;;
		*)
			strategy="$2"
			shift ;;
		esac
		strategy_args="${strategy_args}-s $strategy "
		;;
	-*)
		usage
		;;
	esac
	shift
done

orig_head=$(cat "$GIT_DIR/HEAD") || die "Pulling into a black hole?"
git-fetch --update-head-ok "$@" || exit 1

curr_head=$(cat "$GIT_DIR/HEAD")
if test "$curr_head" != "$orig_head"
then
	# The fetch involved updating the current branch.

	# The working tree and the index file is still based on the
	# $orig_head commit, but we are merging into $curr_head.
	# First update the working tree to match $curr_head.

	echo >&2 "Warning: fetch updated the current branch head."
	echo >&2 "Warning: fast forwarding your working tree."
	git-read-tree -u -m "$orig_head" "$curr_head" ||
		die "You need to first update your working tree."
fi

merge_head=$(sed -e 's/	.*//' "$GIT_DIR"/FETCH_HEAD | tr '\012' ' ')

case "$merge_head" in
?*' '?*)
	merge_name="Octopus merge of "$(
	    perl -e '
	    my @src;
	    my %src;

	    sub andjoin {
		my ($label, $labels, $stuff, $src) = @_;
		my $l = scalar @$stuff;
		my $m = "";
		if ($l == 0) {
		    return "";
		}
		if ($l == 1) {
		    $m = "$label $stuff->[0]";
		}
		else {
		    $m = ("$labels " .
			  join (", ", @{$stuff}[0..$l-2]) .
			  " and $stuff->[-1]");
		}
		if ($src ne ".") {
		    $m .= " from $src";
		}
		return $m;
	    }

	    while (<>) {
		my ($bname, $tname, $gname, $src);
		s/^[0-9a-f]*	//;
		if (s/ of (.*)$//) {
		    $src = $1;
		} else {
		    $src = ".";
		}
		if (! exists $src{$src}) {
		    push @src, $src;
		    $src{$src} = { BRANCH => [], TAG => [], GENERIC => [] };
		}
		if (/^branch (.*)$/) {
		    push @{$src{$src}{BRANCH}}, $1;
		}
		elsif (/^tag (.*)$/) {
		    push @{$src{$src}{TAG}}, $1;
		}
		else {
		    push @{$src{$src}{GENERIC}}, $1;
		}
	    }
	    my @msg;
	    for my $src (@src) {
		my $bag = $src{$src}{BRANCH};
		if (@{$bag}) {
		    push @msg, andjoin("branch", "branches", $bag, $src);
		}
		$bag = $src{$src}{TAG};
		if (@{$bag}) {
		    push @msg, andjoin("tag", "tags", $bag, $src);
		}
		$bag = $src{$src}{GENERIC};
		if (@{$bag}) {
		    push @msg, andjoin("commit", "commits", $bag, $src);
		}
	    }
	    print join("; ", @msg);
	    ' "$GIT_DIR"/FETCH_HEAD
	)
	;;
*)
	merge_name="Merge "$(sed -e 's/^[0-9a-f]*	//' \
		"$GIT_DIR"/FETCH_HEAD)
	;;
esac

case "$merge_head" in
'')
	echo >&2 "No changes."
	exit 0
	;;
esac

git-merge $no_summary $strategy_args "$merge_name" HEAD $merge_head
