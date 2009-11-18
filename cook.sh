#!/bin/sh

LANG=C LC_ALL=C GIT_PAGER=cat
export LANG LC_ALL GIT_PAGER

tmpdir=/var/tmp/cook.$$
mkdir "$tmpdir" || exit
tmp="$tmpdir/t"
trap 'rm -fr "$tmpdir"' 0

git branch --merged "master" | sed -n -e 's/^..//' -e '/\//p' >"$tmp.in.master"
git branch --merged "pu" | sed -n -e 's/^..//' -e '/\//p' >"$tmp.in.pu"
{
	comm -13 "$tmp.in.master" "$tmp.in.pu" 
	git branch --no-merged pu |
	sed -n -e 's/^..//' -e '/\//p'
} >"$tmp.branches"

git log --first-parent --format="%H %ci" master..next |
sed -e 's/ [0-2][0-9]:[0-6][0-9]:[0-6][0-9] [-+][0-2][0-9][0-6][0-9]$//' >"$tmp.next"
git rev-list master..pu >"$tmp.commits.in.pu"

format_branch () {
	# branch=$1 others=$2
	git rev-list --no-merges --topo-order "master..$1" --not $2 >"$tmp.list"
	count=$(wc -l <"$tmp.list" | tr -d ' ')
	label="* $1 ($(git show -s --format="%ai" $1 | sed -e 's/ .*//')) $count commit"
	test "$count" = 1 || label="${label}s"

	count=$(git rev-list "master..$1" | wc -l)
	mcount=$(git rev-list "maint..$1" | wc -l)
	if test $mcount = $count
	then
	    label="$label."
	fi

	echo "$label"
	lasttimelabel=
	lastfoundmerge=
	while read commit
	do
		merged= merged_with=
		while read merge at
		do
			if test -n "$lastfoundmerge"
			then
				if test "$lastfoundmerge" = "$merge"
				then
					lastfoundmerge=
				else
					continue
				fi
			fi
			mb=$(git merge-base $merge $commit)
			if test "$mb" = "$commit"
			then
				merged=$at merged_with=$merge
			else
				break
			fi
		done <"$tmp.next"

		lastfoundmerge=$merged_with
		thistimelabel=
		if test -n "$merged"
		then
			thistimelabel=$merged
			commitlabel="+"
		elif grep "$commit" "$tmp.commits.in.pu" >/dev/null
		then
			commitlabel="-"
		else
			commitlabel="."
		fi
		if test "$lasttimelabel" != "$thistimelabel"
		then
			with=$(git rev-parse --short $merged_with)
			echo "  (merged to 'next' on $thistimelabel at $with)"
			lasttimelabel=$thistimelabel
		fi
		git show -s --format=" $commitlabel %s" $commit
	done <"$tmp.list"
}

add_desc () {
	kind=$1
	shift
	test -z "$description" || description="$description;"
	others=
	while :
	do
		other=$1
		shift
		case "$#,$others" in
		0,)
			others="$other"
			break ;;
		0,?*)
			others="$others and $other"
			break ;;
		*,)
			others="$other"
			;;
		*,?*)
			others="$others, $other"
			;;
		esac
	done
	description="$description $kind $others"
}

show_topic () {
	old=$1 new=$2

	sed -n -e '/^ ..*/p' -e '/^\* /p' "$old" >"$tmp.old.nc"
	sed -n -e '/^ ..*/p' -e '/^\* /p' "$new" >"$tmp.new.nc"
	if cmp "$tmp.old.nc" "$tmp.new.nc" >/dev/null
	then
		cat "$old"
	else
		cat "$new"
		echo "<<"
		cat "$old"
		echo ">>"
	fi
}

while read b
do
	git rev-list --no-merges "master..$b"
done <"$tmp.branches" | sort | uniq -d >"$tmp.shared"

while read shared
do
	b=$(git branch --contains "$shared" | sed -n -e 's/^..//' -e '/\//p')
	echo "" $b ""
done <"$tmp.shared" | sort -u >"$tmp.related"

serial=1
while read b
do
	related=$(grep " $b " "$tmp.related" | tr ' ' '\012' | sort -u | sed -e '/^$/d')

	based_on=
	used_by=
	forks=
	same_as=
	if test -n "$related"
	then
		for r in $related
		do
			test "$b" = "$r" && continue
			based=$(git rev-list --no-merges $b..$r | wc -l | tr -d ' ')
			bases=$(git rev-list --no-merges $r..$b | wc -l | tr -d ' ')
			case "$based,$bases" in
			0,0)
				same_as="$same_as$r "
				;;
			0,*)
				based_on="$based_on$r "
				;;
			*,0)
				used_by="$used_by$r "
				;;
			*,*)
				forks="$forks$r "
				;;
			esac
		done
	fi

	{
		format_branch "$b" "$based_on"

		description=
		test -z "$same_as" || add_desc 'is same as' $same_as
		test -z "$based_on" || add_desc 'uses' $based_on
		test -z "$used_by" || add_desc 'is used by' $used_by
		test -z "$forks" || add_desc 'is related to' $forks

		test -z "$description" ||
		echo " (this branch$description.)"
	} >"$tmp.output.$serial"
	echo "$b $serial"
	serial=$(( $serial + 1 ))
done <"$tmp.branches" >"$tmp.output.toc"

eval $(date +"monthname=%b month=%m year=%Y date=%d dow=%a")
lead="whats/cooking/$year/$month"
issue=$(
	cd Meta &&
	git ls-files "$lead" | tail -n 1
)
if test -n "$issue"
then
	issue=$( expr "$issue" : '.*/0*\([1-9][0-9]*\)\.txt$' )
	issue=$(( $issue + 1 ))
else
	issue=1
fi
issue=$( printf "%02d" $issue )
mkdir -p "Meta/$lead"

last=$(
	cd Meta &&
	git ls-files "whats/cooking"  | tail -n 1
)

# We may have a half-written one already.
incremental=no
if test -f "Meta/$lead/$issue.txt"
then
	last="$lead/$issue.txt"
	incremental=yes
fi

master_at=$(git rev-parse --verify refs/heads/master)
next_at=$(git rev-parse --verify refs/heads/next)
cat >"$tmp.output.blurb" <<EOF
To: git@vger.kernel.org
Subject: What's cooking in git.git ($monthname $year, #$issue; $dow, $date)
X-master-at: $master_at
X-next-at: $next_at

What's cooking in git.git ($monthname $year, #$issue; $dow, $date)
--------------------------------------------------

Here are the topics that have been cooking.  Commits prefixed with '-' are
only in 'pu' while commits prefixed with '+' are in 'next'.  The ones
marked with '.' do not appear in any of the branches, but I am still
holding onto them.

EOF

if test -z "$NO_TEMPLATE" && test -f "Meta/$last"
then
	template="Meta/$last"
else
	template=/dev/null
fi
perl -w -e '
	my $section = undef;
	my $serial = 1;
	my $blurb = "b..l..u..r..b";
	my $branch = $blurb;
	my $tmp = $ARGV[0];
	my $incremental = $ARGV[1] eq "yes";
	my $last_empty = undef;
	my (@section, %section, @branch, %branch, %description, @leader);
	my $in_unedited_olde = 0;

	while (<STDIN>) {
		if ($in_unedited_olde) {
			if (/^>>$/) {
				$in_unedited_olde = 0;
				$_ = " | $_";
			}
		} elsif (/^<<$/) {
			$in_unedited_olde = 1;
		}

		if ($in_unedited_olde) {
			$_ = " | $_";
		}

		if (defined $section && /^-{20,}$/) {
			$_ = "\n";
		}
		if (/^$/) {
			$last_empty = 1;
			next;
		}
		if (/^\[(.*)\]\s*$/) {
			$section = $1;
			$branch = undef;
			if ($section eq "New Topics" && !$incremental) {
				$section = "Old New Topics";
			}
			if (!exists $section{$section}) {
				push @section, $section;
				$section{$section} = [];
			}
			next;
		}
		if (defined $section && /^\* (\S+) /) {
			$branch = $1;
			$last_empty = 0;
			if (!exists $branch{$branch}) {
				push @branch, [$branch, $section];
				$branch{$branch} = 1;
			}
			push @{$section{$section}}, $branch;
		}
		if (defined $branch) {
			my $was_last_empty = $last_empty;
			$last_empty = 0;
			if (!exists $description{$branch}) {
				$description{$branch} = [];
			}
			if ($was_last_empty) {
				push @{$description{$branch}}, "\n";
			}
			push @{$description{$branch}}, $_;
		}
	}

	if (open I, "<$tmp.output.toc") {
		$section = "New Topics";
		while (<I>) {
			my ($branch, $oldserial) = /^(\S*) (\d+)$/;
			next if (exists $branch{$branch});
			if (!exists $section{$section}) {
				# Have it at the beginning
				unshift @section, $section;
				$section{$section} = [];
			}
			push @{$section{$section}}, $branch;
			push @branch, [$branch, $section];
			$branch{$branch} = 1;
			if (!exists $description{$branch}) {
				$description{$branch} = [];
			}
			open II, "<$tmp.output.$oldserial";
			while (<II>) {
				push @{$description{$branch}}, $_;
			}
			close II;
		}
		close I;
	}

	while (0 <= @{$description{$blurb}}) {
		my $last = pop @{$description{$blurb}};
		if ($last =~ /^$/ || $last =~ /^-{20,}$/) {
			next;
		} else {
			push @{$description{$blurb}}, $last;
			last;
		}
	}

	open O, ">$tmp.template.blurb";
	for (@{$description{$blurb}}) {
		print O $_;
	}
	close O;

	open TOC, ">$tmp.template.toc";
	$serial = 1;
	for my $section (@section) {
		for my $branch (@{$section{$section}}) {
			print TOC "$branch $serial $section\n";
			open O, ">$tmp.template.$serial";
			for (@{$description{$branch}}) {
				print O $_;
			}
			close O;
			$serial++;
		}
	}
' <"$template" "$tmp" "$incremental"

tmpserial=$(
	tail -n 1 "$tmp.template.toc" | read branch serial section && echo $serial
)

# Assemble them all

if test -z "$TO_STDOUT"
then
	exec >"Meta/$lead/$issue.txt"
fi

if test -s "$tmp.template.blurb"
then
	sed -e '/^---------------*$/q' <"$tmp.output.blurb"
	sed -e '1,/^---------------*$/d' <"$tmp.template.blurb"
else
	cat "$tmp.output.blurb"
fi

current='
--------------------------------------------------
[Graduated to "master"]
'
while read branch oldserial section
do
	test "$section" = 'Graduated to "master"' &&
	test "$incremental" = no && continue

	tip=$(git rev-parse --quiet --verify "refs/heads/$branch") || continue
	mb=$(git merge-base master $tip)
	test "$mb" = "$tip" || continue
	if test -n "$current"
	then
		echo "$current"
		current=
	else
		echo
	fi
	cat "$tmp.template.$oldserial"
done <"$tmp.template.toc"

current=
while read branch oldserial section
do
	found=$(grep "^$branch " "$tmp.output.toc") || continue
	newserial=$(expr "$found" : '[^ ]* \(.*\)')
	if test "$current" != "$section"
	then
		current=$section
		echo "
--------------------------------------------------
[$section]
"
	else
		echo
	fi

	show_topic "$tmp.template.$oldserial" "$tmp.output.$newserial"
done <"$tmp.template.toc"
