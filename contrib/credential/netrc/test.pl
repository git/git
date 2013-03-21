#!/usr/bin/perl

use warnings;
use strict;
use Test;
use IPC::Open2;

BEGIN { plan tests => 15 }

my @global_credential_args = @ARGV;
my $netrc = './test.netrc';
print "# Testing insecure file, nothing should be found\n";
chmod 0644, $netrc;
my $cred = run_credential(['-f', $netrc, 'get'],
			  { host => 'github.com' });

ok(scalar keys %$cred, 0, "Got 0 keys from insecure file");

print "# Testing missing file, nothing should be found\n";
chmod 0644, $netrc;
$cred = run_credential(['-f', '///nosuchfile///', 'get'],
		       { host => 'github.com' });

ok(scalar keys %$cred, 0, "Got 0 keys from missing file");

chmod 0600, $netrc;

print "# Testing with invalid data\n";
$cred = run_credential(['-f', $netrc, 'get'],
		       "bad data");
ok(scalar keys %$cred, 4, "Got first found keys with bad data");

print "# Testing netrc file for a missing corovamilkbar entry\n";
$cred = run_credential(['-f', $netrc, 'get'],
		       { host => 'corovamilkbar' });

ok(scalar keys %$cred, 0, "Got no corovamilkbar keys");

print "# Testing netrc file for a github.com entry\n";
$cred = run_credential(['-f', $netrc, 'get'],
		       { host => 'github.com' });

ok(scalar keys %$cred, 2, "Got 2 Github keys");

ok($cred->{password}, 'carolknows', "Got correct Github password");
ok($cred->{username}, 'carol', "Got correct Github username");

print "# Testing netrc file for a username-specific entry\n";
$cred = run_credential(['-f', $netrc, 'get'],
		       { host => 'imap', username => 'bob' });

ok(scalar keys %$cred, 2, "Got 2 username-specific keys");

ok($cred->{password}, 'bobwillknow', "Got correct user-specific password");
ok($cred->{protocol}, 'imaps', "Got correct user-specific protocol");

print "# Testing netrc file for a host:port-specific entry\n";
$cred = run_credential(['-f', $netrc, 'get'],
		       { host => 'imap2:1099' });

ok(scalar keys %$cred, 2, "Got 2 host:port-specific keys");

ok($cred->{password}, 'tzzknow', "Got correct host:port-specific password");
ok($cred->{username}, 'tzz', "Got correct host:port-specific username");

print "# Testing netrc file that 'host:port kills host' entry\n";
$cred = run_credential(['-f', $netrc, 'get'],
		       { host => 'imap2' });

ok(scalar keys %$cred, 2, "Got 2 'host:port kills host' keys");

ok($cred->{password}, 'bobwillknow', "Got correct 'host:port kills host' password");
ok($cred->{username}, 'bob', "Got correct 'host:port kills host' username");

sub run_credential
{
	my $args = shift @_;
	my $data = shift @_;
	my $pid = open2(my $chld_out, my $chld_in,
			'./git-credential-netrc', @global_credential_args,
			@$args);

	die "Couldn't open pipe to netrc credential helper: $!" unless $pid;

	if (ref $data eq 'HASH')
	{
		print $chld_in "$_=$data->{$_}\n" foreach sort keys %$data;
	}
	else
	{
		print $chld_in "$data\n";
	}

	close $chld_in;
	my %ret;

	while (<$chld_out>)
	{
		chomp;
		next unless m/^([^=]+)=(.+)/;

		$ret{$1} = $2;
	}

	return \%ret;
}
