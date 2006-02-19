#!/bin/sh

USAGE='[--mixed | --soft | --hard]  [<commit-ish>]'
. git-sh-setup

tmp=${GIT_DIR}/reset.$$
trap 'rm -f $tmp-*' 0 1 2 3 15

reset_type=--mixed
case "$1" in
--mixed | --soft | --hard)
	reset_type="$1"
	shift
	;;
-*)
        usage ;;
esac

rev=$(git-rev-parse --verify --default HEAD "$@") || exit
rev=$(git-rev-parse --verify $rev^0) || exit

# We need to remember the set of paths that _could_ be left
# behind before a hard reset, so that we can remove them.
if test "$reset_type" = "--hard"
then
	{
		git-ls-files --stage -z
		git-rev-parse --verify HEAD 2>/dev/null &&
		git-ls-tree -r -z HEAD
	} | perl -e '
	    use strict;
	    my %seen;
	    $/ = "\0";
	    while (<>) {
		chomp;
		my ($info, $path) = split(/\t/, $_);
		next if ($info =~ / tree /);
		if (!$seen{$path}) {
			$seen{$path} = 1;
			print "$path\0";
		}
	    }
	' >$tmp-exists
fi

# Soft reset does not touch the index file nor the working tree
# at all, but requires them in a good order.  Other resets reset
# the index file to the tree object we are switching to.
if test "$reset_type" = "--soft"
then
	if test -f "$GIT_DIR/MERGE_HEAD" ||
	   test "" != "$(git-ls-files --unmerged)"
	then
		die "Cannot do a soft reset in the middle of a merge."
	fi
else
	git-read-tree --reset "$rev" || exit
fi

# Any resets update HEAD to the head being switched to.
if orig=$(git-rev-parse --verify HEAD 2>/dev/null)
then
	echo "$orig" >"$GIT_DIR/ORIG_HEAD"
else
	rm -f "$GIT_DIR/ORIG_HEAD"
fi
git-update-ref HEAD "$rev"

case "$reset_type" in
--hard )
	# Hard reset matches the working tree to that of the tree
	# being switched to.
	git-checkout-index -f -u -q -a
	git-ls-files --cached -z |
	perl -e '
		use strict;
		my (%keep, $fh);
		$/ = "\0";
		while (<STDIN>) {
			chomp;
			$keep{$_} = 1;
		}
		open $fh, "<", $ARGV[0]
			or die "cannot open $ARGV[0]";
		while (<$fh>) {
			chomp;
			if (! exists $keep{$_}) {
				# it is ok if this fails -- it may already
				# have been culled by checkout-index.
				unlink $_;
				while (s|/[^/]*$||) {
					rmdir($_) or last;
				}
			}
		}
	' $tmp-exists
	;;
--soft )
	;; # Nothing else to do
--mixed )
	# Report what has not been updated.
	git-update-index --refresh
	;;
esac

rm -f "$GIT_DIR/MERGE_HEAD" "$GIT_DIR/rr-cache/MERGE_RR"
