#!/bin/sh

LC_ALL=C LANG=C
export LC_ALL LANG

fmt="%-10s | %7d %7d %7d | %7d %7d | %-10s\n"
hfmt=$(printf "%s" "$fmt" | sed -e 's/d/s/g')
head=$(printf "$hfmt" release new this total this total date)

old= ocommitcnt=
git for-each-ref --format='%(refname:short)' refs/tags/ |
perl -w -e '
	use strict;
	my @version = ();
	my %asked = map { $_ => $_ } @ARGV;

	while (<STDIN>) {
		next unless (/^(v(\d+)\.(\d+)(?:\.(\d+))?(?:-rc(\d+))?)$/);
		# $1 = tag == v$2.$3(.$4)?(-rc$5)?

		if (exists $asked{$1}) {
			; # ok
		} elsif (defined $5) {
			# skip -rc releases
			next;
		} elsif ($2 == 0) {
			# not worth showing breakdown during v0.99 period
			next unless ($1 eq "v0.99");
		} elsif ($2 == 1) {
			# not worth showing breakdown before v1.4.0
			next if ($3 < 4 && $4);
		}
		push @version, [$1, $2, $3, $4, $5];
	}
	for (sort { (
		$a->[1] <=> $b->[1] ||
		$a->[2] <=> $b->[2] ||
		$a->[3] <=> $b->[3] ||
		( (defined $a->[4] && defined $b->[4])
		  ? $a->[4] <=> $b->[4]
		  : defined $a->[4]
		  ? -1 : 1 ) ); } @version) {
		print $_->[0], "\n";
	}
' "$@" |
while read new
do
	commitcnt=$(git rev-list --no-merges "$new" | wc -l)
	git shortlog -s -n "$new" |
	sed -e 's/^[ 	0-9]*//' |
	sort >/var/tmp/new
	if test -n "$old"
	then
		comm -13 /var/tmp/old /var/tmp/new >"/var/tmp/cont-$new"
		i=$(git shortlog -s -n "$old..$new" |
			sed -e 's/^[ 	0-9]*//' |
			wc -l)
		cc=$(( $commitcnt - $ocommitcnt ))
	else
		i=$(wc -l </var/tmp/new)
		cat /var/tmp/new >"/var/tmp/cont-$new"
		cc=$(( $commitcnt + 0 ))
	fi
	old=$new
	mv /var/tmp/new /var/tmp/old
	n=$(wc -l <"/var/tmp/cont-$new")
	c=$(wc -l <"/var/tmp/old")
	t=$(git show -s --format="%ci" "$old^0" | sed -e "s/ .*//")
	ocommitcnt=$commitcnt
	test -z "$head" || echo "$head"
	printf "$fmt" $new $n $i $c $cc $commitcnt $t
	head=
done
