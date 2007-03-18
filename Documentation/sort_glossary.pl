#!/usr/bin/perl

%terms=();

while(<>) {
	if(/^(\S.*)::$/) {
		my $term=$1;
		if(defined($terms{$term})) {
			die "$1 defined twice\n";
		}
		$terms{$term}="";
		LOOP: while(<>) {
			if(/^$/) {
				last LOOP;
			}
			if(/^	\S/) {
				$terms{$term}.=$_;
			} else {
				die "Error 1: $_";
			}
		}
	}
}

sub format_tab_80 ($) {
	my $text=$_[0];
	my $result="";
	$text=~s/\s+/ /g;
	$text=~s/^\s+//;
	while($text=~/^(.{1,72})(|\s+(\S.*)?)$/) {
		$result.="	".$1."\n";
		$text=$3;
	}
	return $result;
}

sub no_spaces ($) {
	my $result=$_[0];
	$result=~tr/ /_/;
	return $result;
}

print 'GIT Glossary
============

This list is sorted alphabetically:

';

@keys=sort {uc($a) cmp uc($b)} keys %terms;
$pattern='(\b(?<!link:git-)'.join('\b|\b(?<!-)',reverse @keys).'\b)';
foreach $key (@keys) {
	$terms{$key}=~s/$pattern/sprintf "<<def_".no_spaces($1).",$1>>";/eg;
	print '[[def_'.no_spaces($key).']]'.$key."::\n"
		.format_tab_80($terms{$key})."\n";
}

print '

Author
------
Written by Johannes Schindelin <Johannes.Schindelin@gmx.de> and
the git-list <git@vger.kernel.org>.

GIT
---
Part of the link:git.html[git] suite
';

