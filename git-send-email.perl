#!/usr/bin/perl -w
#
# Copyright 2002,2005 Greg Kroah-Hartman <greg@kroah.com>
# Copyright 2005 Ryan Anderson <ryan@michonline.com>
#
# GPL v2 (See COPYING)
#
# Ported to support git "mbox" format files by Ryan Anderson <ryan@michonline.com>
#
# Sends a collection of emails to the given email addresses, disturbingly fast.
#
# Supports two formats:
# 1. mbox format files (ignoring most headers and MIME formatting - this is designed for sending patches)
# 2. The original format support by Greg's script:
#    first line of the message is who to CC,
#    and second line is the subject of the message.
#

use strict;
use warnings;
use Term::ReadLine;
use Getopt::Long;
use Data::Dumper;
use Git;

package FakeTerm;
sub new {
	my ($class, $reason) = @_;
	return bless \$reason, shift;
}
sub readline {
	my $self = shift;
	die "Cannot use readline on FakeTerm: $$self";
}
package main;


sub usage {
	print <<EOT;
git-send-email [options] <file | directory>...
Options:
   --from         Specify the "From:" line of the email to be sent.

   --to           Specify the primary "To:" line of the email.

   --cc           Specify an initial "Cc:" list for the entire series
                  of emails.

   --bcc          Specify a list of email addresses that should be Bcc:
		  on all the emails.

   --compose      Use \$EDITOR to edit an introductory message for the
                  patch series.

   --subject      Specify the initial "Subject:" line.
                  Only necessary if --compose is also set.  If --compose
		  is not set, this will be prompted for.

   --in-reply-to  Specify the first "In-Reply-To:" header line.
                  Only used if --compose is also set.  If --compose is not
		  set, this will be prompted for.

   --chain-reply-to If set, the replies will all be to the previous
                  email sent, rather than to the first email sent.
                  Defaults to on.

   --no-signed-off-cc Suppress the automatic addition of email addresses
                 that appear in a Signed-off-by: line, to the cc: list.
		 Note: Using this option is not recommended.

   --smtp-server  If set, specifies the outgoing SMTP server to use.
                  Defaults to localhost.

   --suppress-from Suppress sending emails to yourself if your address
                  appears in a From: line.

   --quiet	  Make git-send-email less verbose.  One line per email
                  should be all that is output.

EOT
	exit(1);
}

# most mail servers generate the Date: header, but not all...
sub format_2822_time {
	my ($time) = @_;
	my @localtm = localtime($time);
	my @gmttm = gmtime($time);
	my $localmin = $localtm[1] + $localtm[2] * 60;
	my $gmtmin = $gmttm[1] + $gmttm[2] * 60;
	if ($localtm[0] != $gmttm[0]) {
		die "local zone differs from GMT by a non-minute interval\n";
	}
	if ((($gmttm[6] + 1) % 7) == $localtm[6]) {
		$localmin += 1440;
	} elsif ((($gmttm[6] - 1) % 7) == $localtm[6]) {
		$localmin -= 1440;
	} elsif ($gmttm[6] != $localtm[6]) {
		die "local time offset greater than or equal to 24 hours\n";
	}
	my $offset = $localmin - $gmtmin;
	my $offhour = $offset / 60;
	my $offmin = abs($offset % 60);
	if (abs($offhour) >= 24) {
		die ("local time offset greater than or equal to 24 hours\n");
	}

	return sprintf("%s, %2d %s %d %02d:%02d:%02d %s%02d%02d",
		       qw(Sun Mon Tue Wed Thu Fri Sat)[$localtm[6]],
		       $localtm[3],
		       qw(Jan Feb Mar Apr May Jun
			  Jul Aug Sep Oct Nov Dec)[$localtm[4]],
		       $localtm[5]+1900,
		       $localtm[2],
		       $localtm[1],
		       $localtm[0],
		       ($offset >= 0) ? '+' : '-',
		       abs($offhour),
		       $offmin,
		       );
}

my $have_email_valid = eval { require Email::Valid; 1 };
my $smtp;

sub unique_email_list(@);
sub cleanup_compose_files();

# Constants (essentially)
my $compose_filename = ".msg.$$";

# Variables we fill in automatically, or via prompting:
my (@to,@cc,@initial_cc,@bcclist,@xh,
	$initial_reply_to,$initial_subject,@files,$from,$compose,$time);

# Behavior modification variables
my ($chain_reply_to, $quiet, $suppress_from, $no_signed_off_cc,
	$dry_run) = (1, 0, 0, 0, 0);
my $smtp_server;

# Example reply to:
#$initial_reply_to = ''; #<20050203173208.GA23964@foobar.com>';

my $repo = Git->repository();
my $term = eval {
	new Term::ReadLine 'git-send-email';
};
if ($@) {
	$term = new FakeTerm "$@: going non-interactive";
}

# Begin by accumulating all the variables (defined above), that we will end up
# needing, first, from the command line:

my $rc = GetOptions("from=s" => \$from,
                    "in-reply-to=s" => \$initial_reply_to,
		    "subject=s" => \$initial_subject,
		    "to=s" => \@to,
		    "cc=s" => \@initial_cc,
		    "bcc=s" => \@bcclist,
		    "chain-reply-to!" => \$chain_reply_to,
		    "smtp-server=s" => \$smtp_server,
		    "compose" => \$compose,
		    "quiet" => \$quiet,
		    "suppress-from" => \$suppress_from,
		    "no-signed-off-cc|no-signed-off-by-cc" => \$no_signed_off_cc,
		    "dry-run" => \$dry_run,
	 );

unless ($rc) {
    usage();
}

# Verify the user input

foreach my $entry (@to) {
	die "Comma in --to entry: $entry'\n" unless $entry !~ m/,/;
}

foreach my $entry (@initial_cc) {
	die "Comma in --cc entry: $entry'\n" unless $entry !~ m/,/;
}

foreach my $entry (@bcclist) {
	die "Comma in --bcclist entry: $entry'\n" unless $entry !~ m/,/;
}

# Now, let's fill any that aren't set in with defaults:

my ($author) = $repo->ident_person('author');
my ($committer) = $repo->ident_person('committer');

my %aliases;
my @alias_files = $repo->config('sendemail.aliasesfile');
my $aliasfiletype = $repo->config('sendemail.aliasfiletype');
my %parse_alias = (
	# multiline formats can be supported in the future
	mutt => sub { my $fh = shift; while (<$fh>) {
		if (/^alias\s+(\S+)\s+(.*)$/) {
			my ($alias, $addr) = ($1, $2);
			$addr =~ s/#.*$//; # mutt allows # comments
			 # commas delimit multiple addresses
			$aliases{$alias} = [ split(/\s*,\s*/, $addr) ];
		}}},
	mailrc => sub { my $fh = shift; while (<$fh>) {
		if (/^alias\s+(\S+)\s+(.*)$/) {
			# spaces delimit multiple addresses
			$aliases{$1} = [ split(/\s+/, $2) ];
		}}},
	pine => sub { my $fh = shift; while (<$fh>) {
		if (/^(\S+)\s+(.*)$/) {
			$aliases{$1} = [ split(/\s*,\s*/, $2) ];
		}}},
	gnus => sub { my $fh = shift; while (<$fh>) {
		if (/\(define-mail-alias\s+"(\S+?)"\s+"(\S+?)"\)/) {
			$aliases{$1} = [ $2 ];
		}}}
);

if (@alias_files and $aliasfiletype and defined $parse_alias{$aliasfiletype}) {
	foreach my $file (@alias_files) {
		open my $fh, '<', $file or die "opening $file: $!\n";
		$parse_alias{$aliasfiletype}->($fh);
		close $fh;
	}
}

my $prompting = 0;
if (!defined $from) {
	$from = $author || $committer;
	do {
		$_ = $term->readline("Who should the emails appear to be from? [$from] ");
	} while (!defined $_);

	$from = $_ if ($_);
	print "Emails will be sent from: ", $from, "\n";
	$prompting++;
}

if (!@to) {
	do {
		$_ = $term->readline("Who should the emails be sent to? ",
				"");
	} while (!defined $_);
	my $to = $_;
	push @to, split /,/, $to;
	$prompting++;
}

sub expand_aliases {
	my @cur = @_;
	my @last;
	do {
		@last = @cur;
		@cur = map { $aliases{$_} ? @{$aliases{$_}} : $_ } @last;
	} while (join(',',@cur) ne join(',',@last));
	return @cur;
}

@to = expand_aliases(@to);
@initial_cc = expand_aliases(@initial_cc);
@bcclist = expand_aliases(@bcclist);

if (!defined $initial_subject && $compose) {
	do {
		$_ = $term->readline("What subject should the emails start with? ",
			$initial_subject);
	} while (!defined $_);
	$initial_subject = $_;
	$prompting++;
}

if (!defined $initial_reply_to && $prompting) {
	do {
		$_= $term->readline("Message-ID to be used as In-Reply-To for the first email? ",
			$initial_reply_to);
	} while (!defined $_);

	$initial_reply_to = $_;
	$initial_reply_to =~ s/(^\s+|\s+$)//g;
}

if (!$smtp_server) {
	$smtp_server = $repo->config('sendemail.smtpserver');
}
if (!$smtp_server) {
	foreach (qw( /usr/sbin/sendmail /usr/lib/sendmail )) {
		if (-x $_) {
			$smtp_server = $_;
			last;
		}
	}
	$smtp_server ||= 'localhost'; # could be 127.0.0.1, too... *shrug*
}

if ($compose) {
	# Note that this does not need to be secure, but we will make a small
	# effort to have it be unique
	open(C,">",$compose_filename)
		or die "Failed to open for writing $compose_filename: $!";
	print C "From $from # This line is ignored.\n";
	printf C "Subject: %s\n\n", $initial_subject;
	printf C <<EOT;
GIT: Please enter your email below.
GIT: Lines beginning in "GIT: " will be removed.
GIT: Consider including an overall diffstat or table of contents
GIT: for the patch you are writing.

EOT
	close(C);

	my $editor = $ENV{EDITOR};
	$editor = 'vi' unless defined $editor;
	system($editor, $compose_filename);

	open(C2,">",$compose_filename . ".final")
		or die "Failed to open $compose_filename.final : " . $!;

	open(C,"<",$compose_filename)
		or die "Failed to open $compose_filename : " . $!;

	while(<C>) {
		next if m/^GIT: /;
		print C2 $_;
	}
	close(C);
	close(C2);

	do {
		$_ = $term->readline("Send this email? (y|n) ");
	} while (!defined $_);

	if (uc substr($_,0,1) ne 'Y') {
		cleanup_compose_files();
		exit(0);
	}

	@files = ($compose_filename . ".final");
}


# Now that all the defaults are set, process the rest of the command line
# arguments and collect up the files that need to be processed.
for my $f (@ARGV) {
	if (-d $f) {
		opendir(DH,$f)
			or die "Failed to opendir $f: $!";

		push @files, grep { -f $_ } map { +$f . "/" . $_ }
				sort readdir(DH);

	} elsif (-f $f) {
		push @files, $f;

	} else {
		print STDERR "Skipping $f - not found.\n";
	}
}

if (@files) {
	unless ($quiet) {
		print $_,"\n" for (@files);
	}
} else {
	print STDERR "\nNo patch files specified!\n\n";
	usage();
}

# Variables we set as part of the loop over files
our ($message_id, $cc, %mail, $subject, $reply_to, $references, $message);

sub extract_valid_address {
	my $address = shift;
	my $local_part_regexp = '[^<>"\s@]+';
	my $domain_regexp = '[^.<>"\s@]+(?:\.[^.<>"\s@]+)+';

	# check for a local address:
	return $address if ($address =~ /^($local_part_regexp)$/);

	if ($have_email_valid) {
		return scalar Email::Valid->address($address);
	} else {
		# less robust/correct than the monster regexp in Email::Valid,
		# but still does a 99% job, and one less dependency
		$address =~ /($local_part_regexp\@$domain_regexp)/;
		return $1;
	}
}

# Usually don't need to change anything below here.

# we make a "fake" message id by taking the current number
# of seconds since the beginning of Unix time and tacking on
# a random number to the end, in case we are called quicker than
# 1 second since the last time we were called.

# We'll setup a template for the message id, using the "from" address:
my $message_id_from = extract_valid_address($from);
my $message_id_template = "<%s-git-send-email-$message_id_from>";

sub make_message_id
{
	my $date = time;
	my $pseudo_rand = int (rand(4200));
	$message_id = sprintf $message_id_template, "$date$pseudo_rand";
	#print "new message id = $message_id\n"; # Was useful for debugging
}



$cc = "";
$time = time - scalar $#files;

sub unquote_rfc2047 {
	local ($_) = @_;
	if (s/=\?utf-8\?q\?(.*)\?=/$1/g) {
		s/_/ /g;
		s/=([0-9A-F]{2})/chr(hex($1))/eg;
	}
	return "$_";
}

sub send_message
{
	my @recipients = unique_email_list(@to);
	my $to = join (",\n\t", @recipients);
	@recipients = unique_email_list(@recipients,@cc,@bcclist);
	my $date = format_2822_time($time++);
	my $gitversion = '@@GIT_VERSION@@';
	if ($gitversion =~ m/..GIT_VERSION../) {
	    $gitversion = Git::version();
	}

	my ($author_name) = ($from =~ /^(.*?)\s+</);
	if ($author_name && $author_name =~ /\./ && $author_name !~ /^".*"$/) {
		my ($name, $addr) = ($from =~ /^(.*?)(\s+<.*)/);
		$from = "\"$name\"$addr";
	}
	my $header = "From: $from
To: $to
Cc: $cc
Subject: $subject
Date: $date
Message-Id: $message_id
X-Mailer: git-send-email $gitversion
";
	if ($reply_to) {

		$header .= "In-Reply-To: $reply_to\n";
		$header .= "References: $references\n";
	}
	if (@xh) {
		$header .= join("\n", @xh) . "\n";
	}

	if ($dry_run) {
		# We don't want to send the email.
	} elsif ($smtp_server =~ m#^/#) {
		my $pid = open my $sm, '|-';
		defined $pid or die $!;
		if (!$pid) {
			exec($smtp_server,'-i',
			     map { extract_valid_address($_) }
			     @recipients) or die $!;
		}
		print $sm "$header\n$message";
		close $sm or die $?;
	} else {
		require Net::SMTP;
		$smtp ||= Net::SMTP->new( $smtp_server );
		$smtp->mail( $from ) or die $smtp->message;
		$smtp->to( @recipients ) or die $smtp->message;
		$smtp->data or die $smtp->message;
		$smtp->datasend("$header\n$message") or die $smtp->message;
		$smtp->dataend() or die $smtp->message;
		$smtp->ok or die "Failed to send $subject\n".$smtp->message;
	}
	if ($quiet) {
		printf "Sent %s\n", $subject;
	} else {
		print "OK. Log says:\nDate: $date\n";
		if ($smtp) {
			print "Server: $smtp_server\n";
		} else {
			print "Sendmail: $smtp_server\n";
		}
		print "From: $from\nSubject: $subject\nCc: $cc\nTo: $to\n\n";
		if ($smtp) {
			print "Result: ", $smtp->code, ' ',
				($smtp->message =~ /\n([^\n]+\n)$/s), "\n";
		} else {
			print "Result: OK\n";
		}
	}
}

$reply_to = $initial_reply_to;
$references = $initial_reply_to || '';
make_message_id();
$subject = $initial_subject;

foreach my $t (@files) {
	open(F,"<",$t) or die "can't open file $t";

	my $author_not_sender = undef;
	@cc = @initial_cc;
	@xh = ();
	my $input_format = undef;
	my $header_done = 0;
	$message = "";
	while(<F>) {
		if (!$header_done) {
			if (/^From /) {
				$input_format = 'mbox';
				next;
			}
			chomp;
			if (!defined $input_format && /^[-A-Za-z]+:\s/) {
				$input_format = 'mbox';
			}

			if (defined $input_format && $input_format eq 'mbox') {
				if (/^Subject:\s+(.*)$/) {
					$subject = $1;

				} elsif (/^(Cc|From):\s+(.*)$/) {
					if ($2 eq $from) {
						next if ($suppress_from);
					}
					elsif ($1 eq 'From') {
						$author_not_sender = $2;
					}
					printf("(mbox) Adding cc: %s from line '%s'\n",
						$2, $_) unless $quiet;
					push @cc, $2;
				}
				elsif (!/^Date:\s/ && /^[-A-Za-z]+:\s+\S/) {
					push @xh, $_;
				}

			} else {
				# In the traditional
				# "send lots of email" format,
				# line 1 = cc
				# line 2 = subject
				# So let's support that, too.
				$input_format = 'lots';
				if (@cc == 0) {
					printf("(non-mbox) Adding cc: %s from line '%s'\n",
						$_, $_) unless $quiet;

					push @cc, $_;

				} elsif (!defined $subject) {
					$subject = $_;
				}
			}

			# A whitespace line will terminate the headers
			if (m/^\s*$/) {
				$header_done = 1;
			}
		} else {
			$message .=  $_;
			if (/^Signed-off-by: (.*)$/i && !$no_signed_off_cc) {
				my $c = $1;
				chomp $c;
				push @cc, $c;
				printf("(sob) Adding cc: %s from line '%s'\n",
					$c, $_) unless $quiet;
			}
		}
	}
	close F;
	if (defined $author_not_sender) {
		$author_not_sender = unquote_rfc2047($author_not_sender);
		$message = "From: $author_not_sender\n\n$message";
	}

	$cc = join(", ", unique_email_list(@cc));

	send_message();

	# set up for the next message
	if ($chain_reply_to || !defined $reply_to || length($reply_to) == 0) {
		$reply_to = $message_id;
		if (length $references > 0) {
			$references .= " $message_id";
		} else {
			$references = "$message_id";
		}
	}
	make_message_id();
}

if ($compose) {
	cleanup_compose_files();
}

sub cleanup_compose_files() {
	unlink($compose_filename, $compose_filename . ".final");

}

$smtp->quit if $smtp;

sub unique_email_list(@) {
	my %seen;
	my @emails;

	foreach my $entry (@_) {
		if (my $clean = extract_valid_address($entry)) {
			$seen{$clean} ||= 0;
			next if $seen{$clean}++;
			push @emails, $entry;
		} else {
			print STDERR "W: unable to extract a valid address",
					" from: $entry\n";
		}
	}
	return @emails;
}
