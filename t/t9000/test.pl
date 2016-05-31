#!/usr/bin/perl
use lib (split(/:/, $ENV{GITPERLLIB}));

use 5.008;
use warnings;
use strict;

use Test::More qw(no_plan);
use Mail::Address;

BEGIN { use_ok('Git') }

my @success_list = (q[Jane],
	q[jdoe@example.com],
	q[<jdoe@example.com>],
	q[Jane <jdoe@example.com>],
	q[Jane Doe <jdoe@example.com>],
	q["Jane" <jdoe@example.com>],
	q["Doe, Jane" <jdoe@example.com>],
	q["Jane@:;\>.,()<Doe" <jdoe@example.com>],
	q[Jane!#$%&'*+-/=?^_{|}~Doe' <jdoe@example.com>],
	q["<jdoe@example.com>"],
	q["Jane jdoe@example.com"],
	q[Jane Doe <jdoe    @   example.com  >],
	q[Jane       Doe <  jdoe@example.com  >],
	q[Jane @ Doe @ Jane @ Doe],
	q["Jane, 'Doe'" <jdoe@example.com>],
	q['Doe, "Jane' <jdoe@example.com>],
	q["Jane" "Do"e <jdoe@example.com>],
	q["Jane' Doe" <jdoe@example.com>],
	q["Jane Doe <jdoe@example.com>" <jdoe@example.com>],
	q["Jane\" Doe" <jdoe@example.com>],
	q[Doe, jane <jdoe@example.com>],
	q["Jane Doe <jdoe@example.com>],
	q['Jane 'Doe' <jdoe@example.com>]);

my @known_failure_list = (q[Jane\ Doe <jdoe@example.com>],
	q["Doe, Ja"ne <jdoe@example.com>],
	q["Doe, Katarina" Jane <jdoe@example.com>],
	q[Jane@:;\.,()<>Doe <jdoe@example.com>],
	q[Jane jdoe@example.com],
	q[<jdoe@example.com> Jane Doe],
	q[Jane <jdoe@example.com> Doe],
	q["Jane "Kat"a" ri"na" ",Doe" <jdoe@example.com>],
	q[Jane Doe],
	q[Jane "Doe <jdoe@example.com>"],
	q[\"Jane Doe <jdoe@example.com>],
	q[Jane\"\" Doe <jdoe@example.com>],
	q['Jane "Katarina\" \' Doe' <jdoe@example.com>]);

foreach my $str (@success_list) {
	my @expected = map { $_->format } Mail::Address->parse("$str");
	my @actual = Git::parse_mailboxes("$str");
	is_deeply(\@expected, \@actual, qq[same output : $str]);
}

TODO: {
	local $TODO = "known breakage";
	foreach my $str (@known_failure_list) {
		my @expected = map { $_->format } Mail::Address->parse("$str");
		my @actual = Git::parse_mailboxes("$str");
		is_deeply(\@expected, \@actual, qq[same output : $str]);
	}
}

my $is_passing = eval { Test::More->is_passing };
exit($is_passing ? 0 : 1) unless $@ =~ /Can't locate object method/;
