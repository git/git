#!/usr/bin/perl

use strict;
use warnings;

use Getopt::Long;
use File::Basename;
use Git;

my $VERSION = "0.2";

my %options = (
	       help => 0,
	       debug => 0,
	       verbose => 0,
	       insecure => 0,
	       file => [],

	       # identical token maps, e.g. host -> host, will be inserted later
	       tmap => {
			port => 'protocol',
			machine => 'host',
			path => 'path',
			login => 'username',
			user => 'username',
			password => 'password',
		       }
	      );

# Map each credential protocol token to itself on the netrc side.
foreach (values %{$options{tmap}}) {
	$options{tmap}->{$_} = $_;
}

# Now, $options{tmap} has a mapping from the netrc format to the Git credential
# helper protocol.

# Next, we build the reverse token map.

# When $rmap{foo} contains 'bar', that means that what the Git credential helper
# protocol calls 'bar' is found as 'foo' in the netrc/authinfo file.  Keys in
# %rmap are what we expect to read from the netrc/authinfo file.

my %rmap;
foreach my $k (keys %{$options{tmap}}) {
	push @{$rmap{$options{tmap}->{$k}}}, $k;
}

Getopt::Long::Configure("bundling");

# TODO: maybe allow the token map $options{tmap} to be configurable.
GetOptions(\%options,
           "help|h",
           "debug|d",
           "insecure|k",
           "verbose|v",
           "file|f=s@",
           'gpg|g:s',
          );

if ($options{help}) {
	my $shortname = basename($0);
	$shortname =~ s/git-credential-//;

	print <<EOHIPPUS;

$0 [(-f <authfile>)...] [-g <program>] [-d] [-v] [-k] get

Version $VERSION by tzz\@lifelogs.com.  License: BSD.

Options:

  -f|--file <authfile>: specify netrc-style files.  Files with the .gpg
                        extension will be decrypted by GPG before parsing.
                        Multiple -f arguments are OK.  They are processed in
                        order, and the first matching entry found is returned
                        via the credential helper protocol (see below).

                        When no -f option is given, .authinfo.gpg, .netrc.gpg,
                        .authinfo, and .netrc files in your home directory are
                        used in this order.

  -g|--gpg <program>  : specify the program for GPG. By default, this is the
                        value of gpg.program in the git repository or global
                        option or gpg.

  -k|--insecure       : ignore bad file ownership or permissions

  -d|--debug          : turn on debugging (developer info)

  -v|--verbose        : be more verbose (show files and information found)

To enable this credential helper:

  git config credential.helper '$shortname -f AUTHFILE1 -f AUTHFILE2'

(Note that Git will prepend "git-credential-" to the helper name and look for it
in the path.)

...and if you want lots of debugging info:

  git config credential.helper '$shortname -f AUTHFILE -d'

...or to see the files opened and data found:

  git config credential.helper '$shortname -f AUTHFILE -v'

Only "get" mode is supported by this credential helper.  It opens every
<authfile> and looks for the first entry that matches the requested search
criteria:

 'port|protocol':
   The protocol that will be used (e.g., https). (protocol=X)

 'machine|host':
   The remote hostname for a network credential. (host=X)

 'path':
   The path with which the credential will be used. (path=X)

 'login|user|username':
   The credential’s username, if we already have one. (username=X)

Thus, when we get this query on STDIN:

host=github.com
protocol=https
username=tzz

this credential helper will look for the first entry in every <authfile> that
matches

machine github.com port https login tzz

OR

machine github.com protocol https login tzz

OR... etc. acceptable tokens as listed above.  Any unknown tokens are
simply ignored.

Then, the helper will print out whatever tokens it got from the entry, including
"password" tokens, mapping back to Git's helper protocol; e.g. "port" is mapped
back to "protocol".  Any redundant entry tokens (part of the original query) are
skipped.

Again, note that only the first matching entry from all the <authfile>s,
processed in the sequence given on the command line, is used.

Netrc/authinfo tokens can be quoted as 'STRING' or "STRING".

No caching is performed by this credential helper.

EOHIPPUS

	exit 0;
}

my $mode = shift @ARGV;

# Credentials must get a parameter, so die if it's missing.
die "Syntax: $0 [(-f <authfile>)...] [-d] get" unless defined $mode;

# Only support 'get' mode; with any other unsupported ones we just exit.
exit 0 unless $mode eq 'get';

my $files = $options{file};

# if no files were given, use a predefined list.
# note that .gpg files come first
unless (scalar @$files) {
	my @candidates = qw[
				   ~/.authinfo.gpg
				   ~/.netrc.gpg
				   ~/.authinfo
				   ~/.netrc
			  ];

	$files = $options{file} = [ map { glob $_ } @candidates ];
}

load_config(\%options);

my $query = read_credential_data_from_stdin();

FILE:
foreach my $file (@$files) {
	my $gpgmode = $file =~ m/\.gpg$/;
	unless (-r $file) {
		log_verbose("Unable to read $file; skipping it");
		next FILE;
	}

	# the following check is copied from Net::Netrc, for non-GPG files
	# OS/2 and Win32 do not handle stat in a way compatible with this check :-(
	unless ($gpgmode || $options{insecure} ||
		$^O eq 'os2'
		|| $^O eq 'MSWin32'
		|| $^O eq 'MacOS'
		|| $^O =~ /^cygwin/) {
		my @stat = stat($file);

		if (@stat) {
			if ($stat[2] & 077) {
				log_verbose("Insecure $file (mode=%04o); skipping it",
					    $stat[2] & 07777);
				next FILE;
			}

			if ($stat[4] != $<) {
				log_verbose("Not owner of $file; skipping it");
				next FILE;
			}
		}
	}

	my @entries = load_netrc($file, $gpgmode);

	unless (scalar @entries) {
		if ($!) {
			log_verbose("Unable to open $file: $!");
		} else {
			log_verbose("No netrc entries found in $file");
		}

		next FILE;
	}

	my $entry = find_netrc_entry($query, @entries);
	if ($entry) {
		print_credential_data($entry, $query);
		# we're done!
		last FILE;
	}
}

exit 0;

sub load_netrc {
	my $file = shift @_;
	my $gpgmode = shift @_;

	my $io;
	if ($gpgmode) {
		my @cmd = ($options{'gpg'}, qw(--decrypt), $file);
		log_verbose("Using GPG to open $file: [@cmd]");
		open $io, "-|", @cmd;
	} else {
		log_verbose("Opening $file...");
		open $io, '<', $file;
	}

	# nothing to do if the open failed (we log the error later)
	return unless $io;

	# Net::Netrc does this, but the functionality is merged with the file
	# detection logic, so we have to extract just the part we need
	my @netrc_entries = net_netrc_loader($io);

	# these entries will use the credential helper protocol token names
	my @entries;

	foreach my $nentry (@netrc_entries) {
		my %entry;
		my $num_port;

		if (!defined $nentry->{machine}) {
			next;
		}
		if (defined $nentry->{port} && $nentry->{port} =~ m/^\d+$/) {
			$num_port = $nentry->{port};
			delete $nentry->{port};
		}

		# create the new entry for the credential helper protocol
		$entry{$options{tmap}->{$_}} = $nentry->{$_} foreach keys %$nentry;

		# for "host X port Y" where Y is an integer (captured by
		# $num_port above), set the host to "X:Y"
		if (defined $entry{host} && defined $num_port) {
			$entry{host} = join(':', $entry{host}, $num_port);
		}

		push @entries, \%entry;
	}

	return @entries;
}

sub net_netrc_loader {
	my $fh = shift @_;
	my @entries;
	my ($mach, $macdef, $tok, @tok);

    LINE:
	while (<$fh>) {
		undef $macdef if /\A\n\Z/;

		if ($macdef) {
			next LINE;
		}

		s/^\s*//;
		chomp;

		while (length && s/^("((?:[^"]+|\\.)*)"|((?:[^\\\s]+|\\.)*))\s*//) {
			(my $tok = $+) =~ s/\\(.)/$1/g;
			push(@tok, $tok);
		}

	    TOKEN:
		while (@tok) {
			if ($tok[0] eq "default") {
				shift(@tok);
				$mach = { machine => undef };
				next TOKEN;
			}

			$tok = shift(@tok);

			if ($tok eq "machine") {
				my $host = shift @tok;
				$mach = { machine => $host };
				push @entries, $mach;
			} elsif (exists $options{tmap}->{$tok}) {
				unless ($mach) {
					log_debug("Skipping token $tok because no machine was given");
					next TOKEN;
				}

				my $value = shift @tok;
				unless (defined $value) {
					log_debug("Token $tok had no value, skipping it.");
					next TOKEN;
				}

				# Following line added by rmerrell to remove '/' escape char in .netrc
				$value =~ s/\/\\/\\/g;
				$mach->{$tok} = $value;
			} elsif ($tok eq "macdef") { # we ignore macros
				next TOKEN unless $mach;
				my $value = shift @tok;
				$macdef = 1;
			}
		}
	}

	return @entries;
}

sub read_credential_data_from_stdin {
	# the query: start with every token with no value
	my %q = map { $_ => undef } values(%{$options{tmap}});

	while (<STDIN>) {
		next unless m/^([^=]+)=(.+)/;

		my ($token, $value) = ($1, $2);

		# skip any unknown tokens
		next unless exists $q{$token};

		$q{$token} = $value;
		log_debug("We were given search token $token and value $value");
	}

	foreach (sort keys %q) {
		log_debug("Searching for %s = %s", $_, $q{$_} || '(any value)');
	}

	return \%q;
}

# takes the search tokens and then a list of entries
# each entry is a hash reference
sub find_netrc_entry {
	my $query = shift @_;

    ENTRY:
	foreach my $entry (@_)
	{
		my $entry_text = join ', ', map { "$_=$entry->{$_}" } keys %$entry;
		foreach my $check (sort keys %$query) {
			if (!defined $entry->{$check}) {
			        log_debug("OK: entry has no $check token, so any value satisfies check $check");
			} elsif (defined $query->{$check}) {
				log_debug("compare %s [%s] to [%s] (entry: %s)",
					  $check,
					  $entry->{$check},
					  $query->{$check},
					  $entry_text);
				unless ($query->{$check} eq $entry->{$check}) {
					next ENTRY;
				}
			} else {
				log_debug("OK: any value satisfies check $check");
			}
		}

		return $entry;
	}

	# nothing was found
	return;
}

sub print_credential_data {
	my $entry = shift @_;
	my $query = shift @_;

	log_debug("entry has passed all the search checks");
 TOKEN:
	foreach my $git_token (sort keys %$entry) {
		log_debug("looking for useful token $git_token");
		# don't print unknown (to the credential helper protocol) tokens
		next TOKEN unless exists $query->{$git_token};

		# don't print things asked in the query (the entry matches them)
		next TOKEN if defined $query->{$git_token};

		log_debug("FOUND: $git_token=$entry->{$git_token}");
		printf "%s=%s\n", $git_token, $entry->{$git_token};
	}
}
sub load_config {
	# load settings from git config
	my $options = shift;
	# set from command argument, gpg.program option, or default to gpg
	$options->{'gpg'} //= Git::config('gpg.program')
	                  // 'gpg';
	log_verbose("using $options{'gpg'} for GPG operations");
}
sub log_verbose {
	return unless $options{verbose};
	printf STDERR @_;
	printf STDERR "\n";
}

sub log_debug {
	return unless $options{debug};
	printf STDERR @_;
	printf STDERR "\n";
}
