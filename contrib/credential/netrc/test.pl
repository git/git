#!/usr/bin/perl

use warnings;
use strict;
use Test::More qw(no_plan);
use File::Basename;
use File::Spec::Functions qw(:DEFAULT rel2abs);
use IPC::Open2;

BEGIN {
	# t-git-credential-netrc.sh kicks off our testing, so we have to go
	# from there.
	Test::More->builder->current_test(1);
}

my @global_credential_args = @ARGV;
my $scriptDir = dirname rel2abs $0;
my ($netrc, $netrcGpg, $gcNetrc) = map { catfile $scriptDir, $_; }
                                       qw(test.netrc
                                          test.netrc.gpg
                                          git-credential-netrc);
local $ENV{PATH} = join ':'
                      , $scriptDir
                      , $ENV{PATH}
                      ? $ENV{PATH}
                      : ();

diag "Testing insecure file, nothing should be found\n";
chmod 0644, $netrc;
my $cred = run_credential(['-f', $netrc, 'get'],
			  { host => 'github.com' });

ok(scalar keys %$cred == 0, "Got 0 keys from insecure file");

diag "Testing missing file, nothing should be found\n";
chmod 0644, $netrc;
$cred = run_credential(['-f', '///nosuchfile///', 'get'],
		       { host => 'github.com' });

ok(scalar keys %$cred == 0, "Got 0 keys from missing file");

chmod 0600, $netrc;

diag "Testing with invalid data\n";
$cred = run_credential(['-f', $netrc, 'get'],
		       "bad data");
ok(scalar keys %$cred == 4, "Got first found keys with bad data");

diag "Testing netrc file for a missing corovamilkbar entry\n";
$cred = run_credential(['-f', $netrc, 'get'],
		       { host => 'corovamilkbar' });

ok(scalar keys %$cred == 0, "Got no corovamilkbar keys");

diag "Testing netrc file for a github.com entry\n";
$cred = run_credential(['-f', $netrc, 'get'],
		       { host => 'github.com' });

ok(scalar keys %$cred == 2, "Got 2 Github keys");

is($cred->{password}, 'carolknows', "Got correct Github password");
is($cred->{username}, 'carol', "Got correct Github username");

diag "Testing netrc file for a username-specific entry\n";
$cred = run_credential(['-f', $netrc, 'get'],
		       { host => 'imap', username => 'bob' });

ok(scalar keys %$cred == 2, "Got 2 username-specific keys");

is($cred->{password}, 'bobwillknow', "Got correct user-specific password");
is($cred->{protocol}, 'imaps', "Got correct user-specific protocol");

diag "Testing netrc file for a host:port-specific entry\n";
$cred = run_credential(['-f', $netrc, 'get'],
		       { host => 'imap2:1099' });

ok(scalar keys %$cred == 2, "Got 2 host:port-specific keys");

is($cred->{password}, 'tzzknow', "Got correct host:port-specific password");
is($cred->{username}, 'tzz', "Got correct host:port-specific username");

diag "Testing netrc file that 'host:port kills host' entry\n";
$cred = run_credential(['-f', $netrc, 'get'],
		       { host => 'imap2' });

ok(scalar keys %$cred == 2, "Got 2 'host:port kills host' keys");

is($cred->{password}, 'bobwillknow', "Got correct 'host:port kills host' password");
is($cred->{username}, 'bob', "Got correct 'host:port kills host' username");

diag 'Testing netrc file decryption by git config gpg.program setting\n';
$cred = run_credential( ['-f', $netrcGpg, 'get']
                      , { host => 'git-config-gpg' }
                      );

ok(scalar keys %$cred == 2, 'Got keys decrypted by git config option');

diag 'Testing netrc file decryption by gpg option\n';
$cred = run_credential( ['-f', $netrcGpg, '-g', 'test.command-option-gpg', 'get']
                      , { host => 'command-option-gpg' }
                      );

ok(scalar keys %$cred == 2, 'Got keys decrypted by command option');

my $is_passing = eval { Test::More->is_passing };
exit($is_passing ? 0 : 1) unless $@ =~ /Can't locate object method/;

sub run_credential
{
	my $args = shift @_;
	my $data = shift @_;
	my $pid = open2(my $chld_out, my $chld_in,
			$gcNetrc, @global_credential_args,
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
