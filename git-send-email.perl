#!/usr/bin/perl
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

use 5.008;
use strict;
use warnings;
use Term::ReadLine;
use Getopt::Long;
use Text::ParseWords;
use Data::Dumper;
use Term::ANSIColor;
use File::Temp qw/ tempdir tempfile /;
use File::Spec::Functions qw(catfile);
use Error qw(:try);
use Git;

Getopt::Long::Configure qw/ pass_through /;

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
git send-email [options] <file | directory | rev-list options >

  Composing:
    --from                  <str>  * Email From:
    --[no-]to               <str>  * Email To:
    --[no-]cc               <str>  * Email Cc:
    --[no-]bcc              <str>  * Email Bcc:
    --subject               <str>  * Email "Subject:"
    --in-reply-to           <str>  * Email "In-Reply-To:"
    --annotate                     * Review each patch that will be sent in an editor.
    --compose                      * Open an editor for introduction.
    --8bit-encoding         <str>  * Encoding to assume 8bit mails if undeclared

  Sending:
    --envelope-sender       <str>  * Email envelope sender.
    --smtp-server       <str:int>  * Outgoing SMTP server to use. The port
                                     is optional. Default 'localhost'.
    --smtp-server-option    <str>  * Outgoing SMTP server option to use.
    --smtp-server-port      <int>  * Outgoing SMTP server port.
    --smtp-user             <str>  * Username for SMTP-AUTH.
    --smtp-pass             <str>  * Password for SMTP-AUTH; not necessary.
    --smtp-encryption       <str>  * tls or ssl; anything else disables.
    --smtp-ssl                     * Deprecated. Use '--smtp-encryption ssl'.
    --smtp-domain           <str>  * The domain name sent to HELO/EHLO handshake
    --smtp-debug            <0|1>  * Disable, enable Net::SMTP debug.

  Automating:
    --identity              <str>  * Use the sendemail.<id> options.
    --to-cmd                <str>  * Email To: via `<str> \$patch_path`
    --cc-cmd                <str>  * Email Cc: via `<str> \$patch_path`
    --suppress-cc           <str>  * author, self, sob, cc, cccmd, body, bodycc, all.
    --[no-]signed-off-by-cc        * Send to Signed-off-by: addresses. Default on.
    --[no-]suppress-from           * Send to self. Default off.
    --[no-]chain-reply-to          * Chain In-Reply-To: fields. Default off.
    --[no-]thread                  * Use In-Reply-To: field. Default on.

  Administering:
    --confirm               <str>  * Confirm recipients before sending;
                                     auto, cc, compose, always, or never.
    --quiet                        * Output one line of info per email.
    --dry-run                      * Don't actually send the emails.
    --[no-]validate                * Perform patch sanity checks. Default on.
    --[no-]format-patch            * understand any non optional arguments as
                                     `git format-patch` ones.
    --force                        * Send even if safety checks would prevent it.

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
my $have_mail_address = eval { require Mail::Address; 1 };
my $smtp;
my $auth;

# Variables we fill in automatically, or via prompting:
my (@to,$no_to,@initial_to,@cc,$no_cc,@initial_cc,@bcclist,$no_bcc,@xh,
	$initial_reply_to,$initial_subject,@files,
	$author,$sender,$smtp_authpass,$annotate,$compose,$time);

my $envelope_sender;

# Example reply to:
#$initial_reply_to = ''; #<20050203173208.GA23964@foobar.com>';

my $repo = eval { Git->repository() };
my @repo = $repo ? ($repo) : ();
my $term = eval {
	$ENV{"GIT_SEND_EMAIL_NOTTY"}
		? new Term::ReadLine 'git-send-email', \*STDIN, \*STDOUT
		: new Term::ReadLine 'git-send-email';
};
if ($@) {
	$term = new FakeTerm "$@: going non-interactive";
}

# Behavior modification variables
my ($quiet, $dry_run) = (0, 0);
my $format_patch;
my $compose_filename;
my $force = 0;

# Handle interactive edition of files.
my $multiedit;
my $editor;

sub do_edit {
	if (!defined($editor)) {
		$editor = Git::command_oneline('var', 'GIT_EDITOR');
	}
	if (defined($multiedit) && !$multiedit) {
		map {
			system('sh', '-c', $editor.' "$@"', $editor, $_);
			if (($? & 127) || ($? >> 8)) {
				die("the editor exited uncleanly, aborting everything");
			}
		} @_;
	} else {
		system('sh', '-c', $editor.' "$@"', $editor, @_);
		if (($? & 127) || ($? >> 8)) {
			die("the editor exited uncleanly, aborting everything");
		}
	}
}

# Variables with corresponding config settings
my ($thread, $chain_reply_to, $suppress_from, $signed_off_by_cc);
my ($to_cmd, $cc_cmd);
my ($smtp_server, $smtp_server_port, @smtp_server_options);
my ($smtp_authuser, $smtp_encryption);
my ($identity, $aliasfiletype, @alias_files, $smtp_domain);
my ($validate, $confirm);
my (@suppress_cc);
my ($auto_8bit_encoding);

my ($debug_net_smtp) = 0;		# Net::SMTP, see send_message()

my $not_set_by_user = "true but not set by the user";

my %config_bool_settings = (
    "thread" => [\$thread, 1],
    "chainreplyto" => [\$chain_reply_to, $not_set_by_user],
    "suppressfrom" => [\$suppress_from, undef],
    "signedoffbycc" => [\$signed_off_by_cc, undef],
    "signedoffcc" => [\$signed_off_by_cc, undef],      # Deprecated
    "validate" => [\$validate, 1],
);

my %config_settings = (
    "smtpserver" => \$smtp_server,
    "smtpserverport" => \$smtp_server_port,
    "smtpserveroption" => \@smtp_server_options,
    "smtpuser" => \$smtp_authuser,
    "smtppass" => \$smtp_authpass,
    "smtpdomain" => \$smtp_domain,
    "to" => \@initial_to,
    "tocmd" => \$to_cmd,
    "cc" => \@initial_cc,
    "cccmd" => \$cc_cmd,
    "aliasfiletype" => \$aliasfiletype,
    "bcc" => \@bcclist,
    "aliasesfile" => \@alias_files,
    "suppresscc" => \@suppress_cc,
    "envelopesender" => \$envelope_sender,
    "multiedit" => \$multiedit,
    "confirm"   => \$confirm,
    "from" => \$sender,
    "assume8bitencoding" => \$auto_8bit_encoding,
);

# Help users prepare for 1.7.0
sub chain_reply_to {
	if (defined $chain_reply_to &&
	    $chain_reply_to eq $not_set_by_user) {
		print STDERR
		    "In git 1.7.0, the default has changed to --no-chain-reply-to\n" .
		    "Set sendemail.chainreplyto configuration variable to true if\n" .
		    "you want to keep --chain-reply-to as your default.\n";
		$chain_reply_to = 0;
	}
	return $chain_reply_to;
}

# Handle Uncouth Termination
sub signal_handler {

	# Make text normal
	print color("reset"), "\n";

	# SMTP password masked
	system "stty echo";

	# tmp files from --compose
	if (defined $compose_filename) {
		if (-e $compose_filename) {
			print "'$compose_filename' contains an intermediate version of the email you were composing.\n";
		}
		if (-e ($compose_filename . ".final")) {
			print "'$compose_filename.final' contains the composed email.\n"
		}
	}

	exit;
};

$SIG{TERM} = \&signal_handler;
$SIG{INT}  = \&signal_handler;

# Begin by accumulating all the variables (defined above), that we will end up
# needing, first, from the command line:

my $rc = GetOptions("sender|from=s" => \$sender,
                    "in-reply-to=s" => \$initial_reply_to,
		    "subject=s" => \$initial_subject,
		    "to=s" => \@initial_to,
		    "to-cmd=s" => \$to_cmd,
		    "no-to" => \$no_to,
		    "cc=s" => \@initial_cc,
		    "no-cc" => \$no_cc,
		    "bcc=s" => \@bcclist,
		    "no-bcc" => \$no_bcc,
		    "chain-reply-to!" => \$chain_reply_to,
		    "smtp-server=s" => \$smtp_server,
		    "smtp-server-option=s" => \@smtp_server_options,
		    "smtp-server-port=s" => \$smtp_server_port,
		    "smtp-user=s" => \$smtp_authuser,
		    "smtp-pass:s" => \$smtp_authpass,
		    "smtp-ssl" => sub { $smtp_encryption = 'ssl' },
		    "smtp-encryption=s" => \$smtp_encryption,
		    "smtp-debug:i" => \$debug_net_smtp,
		    "smtp-domain:s" => \$smtp_domain,
		    "identity=s" => \$identity,
		    "annotate" => \$annotate,
		    "compose" => \$compose,
		    "quiet" => \$quiet,
		    "cc-cmd=s" => \$cc_cmd,
		    "suppress-from!" => \$suppress_from,
		    "suppress-cc=s" => \@suppress_cc,
		    "signed-off-cc|signed-off-by-cc!" => \$signed_off_by_cc,
		    "confirm=s" => \$confirm,
		    "dry-run" => \$dry_run,
		    "envelope-sender=s" => \$envelope_sender,
		    "thread!" => \$thread,
		    "validate!" => \$validate,
		    "format-patch!" => \$format_patch,
		    "8bit-encoding=s" => \$auto_8bit_encoding,
		    "force" => \$force,
	 );

unless ($rc) {
    usage();
}

die "Cannot run git format-patch from outside a repository\n"
	if $format_patch and not $repo;

# Now, let's fill any that aren't set in with defaults:

sub read_config {
	my ($prefix) = @_;

	foreach my $setting (keys %config_bool_settings) {
		my $target = $config_bool_settings{$setting}->[0];
		$$target = Git::config_bool(@repo, "$prefix.$setting") unless (defined $$target);
	}

	foreach my $setting (keys %config_settings) {
		my $target = $config_settings{$setting};
		next if $setting eq "to" and defined $no_to;
		next if $setting eq "cc" and defined $no_cc;
		next if $setting eq "bcc" and defined $no_bcc;
		if (ref($target) eq "ARRAY") {
			unless (@$target) {
				my @values = Git::config(@repo, "$prefix.$setting");
				@$target = @values if (@values && defined $values[0]);
			}
		}
		else {
			$$target = Git::config(@repo, "$prefix.$setting") unless (defined $$target);
		}
	}

	if (!defined $smtp_encryption) {
		my $enc = Git::config(@repo, "$prefix.smtpencryption");
		if (defined $enc) {
			$smtp_encryption = $enc;
		} elsif (Git::config_bool(@repo, "$prefix.smtpssl")) {
			$smtp_encryption = 'ssl';
		}
	}
}

# read configuration from [sendemail "$identity"], fall back on [sendemail]
$identity = Git::config(@repo, "sendemail.identity") unless (defined $identity);
read_config("sendemail.$identity") if (defined $identity);
read_config("sendemail");

# fall back on builtin bool defaults
foreach my $setting (values %config_bool_settings) {
	${$setting->[0]} = $setting->[1] unless (defined (${$setting->[0]}));
}

# 'default' encryption is none -- this only prevents a warning
$smtp_encryption = '' unless (defined $smtp_encryption);

# Set CC suppressions
my(%suppress_cc);
if (@suppress_cc) {
	foreach my $entry (@suppress_cc) {
		die "Unknown --suppress-cc field: '$entry'\n"
			unless $entry =~ /^(?:all|cccmd|cc|author|self|sob|body|bodycc)$/;
		$suppress_cc{$entry} = 1;
	}
}

if ($suppress_cc{'all'}) {
	foreach my $entry (qw (cccmd cc author self sob body bodycc)) {
		$suppress_cc{$entry} = 1;
	}
	delete $suppress_cc{'all'};
}

# If explicit old-style ones are specified, they trump --suppress-cc.
$suppress_cc{'self'} = $suppress_from if defined $suppress_from;
$suppress_cc{'sob'} = !$signed_off_by_cc if defined $signed_off_by_cc;

if ($suppress_cc{'body'}) {
	foreach my $entry (qw (sob bodycc)) {
		$suppress_cc{$entry} = 1;
	}
	delete $suppress_cc{'body'};
}

# Set confirm's default value
my $confirm_unconfigured = !defined $confirm;
if ($confirm_unconfigured) {
	$confirm = scalar %suppress_cc ? 'compose' : 'auto';
};
die "Unknown --confirm setting: '$confirm'\n"
	unless $confirm =~ /^(?:auto|cc|compose|always|never)/;

# Debugging, print out the suppressions.
if (0) {
	print "suppressions:\n";
	foreach my $entry (keys %suppress_cc) {
		printf "  %-5s -> $suppress_cc{$entry}\n", $entry;
	}
}

my ($repoauthor, $repocommitter);
($repoauthor) = Git::ident_person(@repo, 'author');
($repocommitter) = Git::ident_person(@repo, 'committer');

# Verify the user input

foreach my $entry (@initial_to) {
	die "Comma in --to entry: $entry'\n" unless $entry !~ m/,/;
}

foreach my $entry (@initial_cc) {
	die "Comma in --cc entry: $entry'\n" unless $entry !~ m/,/;
}

foreach my $entry (@bcclist) {
	die "Comma in --bcclist entry: $entry'\n" unless $entry !~ m/,/;
}

sub parse_address_line {
	if ($have_mail_address) {
		return map { $_->format } Mail::Address->parse($_[0]);
	} else {
		return split_addrs($_[0]);
	}
}

sub split_addrs {
	return quotewords('\s*,\s*', 1, @_);
}

my %aliases;
my %parse_alias = (
	# multiline formats can be supported in the future
	mutt => sub { my $fh = shift; while (<$fh>) {
		if (/^\s*alias\s+(?:-group\s+\S+\s+)*(\S+)\s+(.*)$/) {
			my ($alias, $addr) = ($1, $2);
			$addr =~ s/#.*$//; # mutt allows # comments
			 # commas delimit multiple addresses
			$aliases{$alias} = [ split_addrs($addr) ];
		}}},
	mailrc => sub { my $fh = shift; while (<$fh>) {
		if (/^alias\s+(\S+)\s+(.*)$/) {
			# spaces delimit multiple addresses
			$aliases{$1} = [ quotewords('\s+', 0, $2) ];
		}}},
	pine => sub { my $fh = shift; my $f='\t[^\t]*';
	        for (my $x = ''; defined($x); $x = $_) {
			chomp $x;
		        $x .= $1 while(defined($_ = <$fh>) && /^ +(.*)$/);
			$x =~ /^(\S+)$f\t\(?([^\t]+?)\)?(:?$f){0,2}$/ or next;
			$aliases{$1} = [ split_addrs($2) ];
		}},
	elm => sub  { my $fh = shift;
		      while (<$fh>) {
			  if (/^(\S+)\s+=\s+[^=]+=\s(\S+)/) {
			      my ($alias, $addr) = ($1, $2);
			       $aliases{$alias} = [ split_addrs($addr) ];
			  }
		      } },

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

($sender) = expand_aliases($sender) if defined $sender;

# returns 1 if the conflict must be solved using it as a format-patch argument
sub check_file_rev_conflict($) {
	return unless $repo;
	my $f = shift;
	try {
		$repo->command('rev-parse', '--verify', '--quiet', $f);
		if (defined($format_patch)) {
			return $format_patch;
		}
		die(<<EOF);
File '$f' exists but it could also be the range of commits
to produce patches for.  Please disambiguate by...

    * Saying "./$f" if you mean a file; or
    * Giving --format-patch option if you mean a range.
EOF
	} catch Git::Error::Command with {
		return 0;
	}
}

# Now that all the defaults are set, process the rest of the command line
# arguments and collect up the files that need to be processed.
my @rev_list_opts;
while (defined(my $f = shift @ARGV)) {
	if ($f eq "--") {
		push @rev_list_opts, "--", @ARGV;
		@ARGV = ();
	} elsif (-d $f and !check_file_rev_conflict($f)) {
		opendir my $dh, $f
			or die "Failed to opendir $f: $!";

		push @files, grep { -f $_ } map { catfile($f, $_) }
				sort readdir $dh;
		closedir $dh;
	} elsif ((-f $f or -p $f) and !check_file_rev_conflict($f)) {
		push @files, $f;
	} else {
		push @rev_list_opts, $f;
	}
}

if (@rev_list_opts) {
	die "Cannot run git format-patch from outside a repository\n"
		unless $repo;
	push @files, $repo->command('format-patch', '-o', tempdir(CLEANUP => 1), @rev_list_opts);
}

if ($validate) {
	foreach my $f (@files) {
		unless (-p $f) {
			my $error = validate_patch($f);
			$error and die "fatal: $f: $error\nwarning: no patches were sent\n";
		}
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

sub get_patch_subject {
	my $fn = shift;
	open (my $fh, '<', $fn);
	while (my $line = <$fh>) {
		next unless ($line =~ /^Subject: (.*)$/);
		close $fh;
		return "GIT: $1\n";
	}
	close $fh;
	die "No subject line in $fn ?";
}

if ($compose) {
	# Note that this does not need to be secure, but we will make a small
	# effort to have it be unique
	$compose_filename = ($repo ?
		tempfile(".gitsendemail.msg.XXXXXX", DIR => $repo->repo_path()) :
		tempfile(".gitsendemail.msg.XXXXXX", DIR => "."))[1];
	open my $c, ">", $compose_filename
		or die "Failed to open for writing $compose_filename: $!";


	my $tpl_sender = $sender || $repoauthor || $repocommitter || '';
	my $tpl_subject = $initial_subject || '';
	my $tpl_reply_to = $initial_reply_to || '';

	print $c <<EOT;
From $tpl_sender # This line is ignored.
GIT: Lines beginning in "GIT:" will be removed.
GIT: Consider including an overall diffstat or table of contents
GIT: for the patch you are writing.
GIT:
GIT: Clear the body content if you don't wish to send a summary.
From: $tpl_sender
Subject: $tpl_subject
In-Reply-To: $tpl_reply_to

EOT
	for my $f (@files) {
		print $c get_patch_subject($f);
	}
	close $c;

	if ($annotate) {
		do_edit($compose_filename, @files);
	} else {
		do_edit($compose_filename);
	}

	open my $c2, ">", $compose_filename . ".final"
		or die "Failed to open $compose_filename.final : " . $!;

	open $c, "<", $compose_filename
		or die "Failed to open $compose_filename : " . $!;

	my $need_8bit_cte = file_has_nonascii($compose_filename);
	my $in_body = 0;
	my $summary_empty = 1;
	while(<$c>) {
		next if m/^GIT:/;
		if ($in_body) {
			$summary_empty = 0 unless (/^\n$/);
		} elsif (/^\n$/) {
			$in_body = 1;
			if ($need_8bit_cte) {
				print $c2 "MIME-Version: 1.0\n",
					 "Content-Type: text/plain; ",
					   "charset=UTF-8\n",
					 "Content-Transfer-Encoding: 8bit\n";
			}
		} elsif (/^MIME-Version:/i) {
			$need_8bit_cte = 0;
		} elsif (/^Subject:\s*(.+)\s*$/i) {
			$initial_subject = $1;
			my $subject = $initial_subject;
			$_ = "Subject: " .
				($subject =~ /[^[:ascii:]]/ ?
				 quote_rfc2047($subject) :
				 $subject) .
				"\n";
		} elsif (/^In-Reply-To:\s*(.+)\s*$/i) {
			$initial_reply_to = $1;
			next;
		} elsif (/^From:\s*(.+)\s*$/i) {
			$sender = $1;
			next;
		} elsif (/^(?:To|Cc|Bcc):/i) {
			print "To/Cc/Bcc fields are not interpreted yet, they have been ignored\n";
			next;
		}
		print $c2 $_;
	}
	close $c;
	close $c2;

	if ($summary_empty) {
		print "Summary email is empty, skipping it\n";
		$compose = -1;
	}
} elsif ($annotate) {
	do_edit(@files);
}

sub ask {
	my ($prompt, %arg) = @_;
	my $valid_re = $arg{valid_re};
	my $default = $arg{default};
	my $resp;
	my $i = 0;
	return defined $default ? $default : undef
		unless defined $term->IN and defined fileno($term->IN) and
		       defined $term->OUT and defined fileno($term->OUT);
	while ($i++ < 10) {
		$resp = $term->readline($prompt);
		if (!defined $resp) { # EOF
			print "\n";
			return defined $default ? $default : undef;
		}
		if ($resp eq '' and defined $default) {
			return $default;
		}
		if (!defined $valid_re or $resp =~ /$valid_re/) {
			return $resp;
		}
	}
	return undef;
}

my %broken_encoding;

sub file_declares_8bit_cte {
	my $fn = shift;
	open (my $fh, '<', $fn);
	while (my $line = <$fh>) {
		last if ($line =~ /^$/);
		return 1 if ($line =~ /^Content-Transfer-Encoding: .*8bit.*$/);
	}
	close $fh;
	return 0;
}

foreach my $f (@files) {
	next unless (body_or_subject_has_nonascii($f)
		     && !file_declares_8bit_cte($f));
	$broken_encoding{$f} = 1;
}

if (!defined $auto_8bit_encoding && scalar %broken_encoding) {
	print "The following files are 8bit, but do not declare " .
		"a Content-Transfer-Encoding.\n";
	foreach my $f (sort keys %broken_encoding) {
		print "    $f\n";
	}
	$auto_8bit_encoding = ask("Which 8bit encoding should I declare [UTF-8]? ",
				  default => "UTF-8");
}

if (!$force) {
	for my $f (@files) {
		if (get_patch_subject($f) =~ /\Q*** SUBJECT HERE ***\E/) {
			die "Refusing to send because the patch\n\t$f\n"
				. "has the template subject '*** SUBJECT HERE ***'. "
				. "Pass --force if you really want to send.\n";
		}
	}
}

my $prompting = 0;
if (!defined $sender) {
	$sender = $repoauthor || $repocommitter || '';
	$sender = ask("Who should the emails appear to be from? [$sender] ",
	              default => $sender);
	print "Emails will be sent from: ", $sender, "\n";
	$prompting++;
}

if (!@initial_to && !defined $to_cmd) {
	my $to = ask("Who should the emails be sent to? ");
	push @initial_to, parse_address_line($to) if defined $to; # sanitized/validated later
	$prompting++;
}

sub expand_aliases {
	return map { expand_one_alias($_) } @_;
}

my %EXPANDED_ALIASES;
sub expand_one_alias {
	my $alias = shift;
	if ($EXPANDED_ALIASES{$alias}) {
		die "fatal: alias '$alias' expands to itself\n";
	}
	local $EXPANDED_ALIASES{$alias} = 1;
	return $aliases{$alias} ? expand_aliases(@{$aliases{$alias}}) : $alias;
}

@initial_to = expand_aliases(@initial_to);
@initial_to = (map { sanitize_address($_) } @initial_to);
@initial_cc = expand_aliases(@initial_cc);
@bcclist = expand_aliases(@bcclist);

if ($thread && !defined $initial_reply_to && $prompting) {
	$initial_reply_to = ask(
		"Message-ID to be used as In-Reply-To for the first email? ");
}
if (defined $initial_reply_to) {
	$initial_reply_to =~ s/^\s*<?//;
	$initial_reply_to =~ s/>?\s*$//;
	$initial_reply_to = "<$initial_reply_to>" if $initial_reply_to ne '';
}

if (!defined $smtp_server) {
	foreach (qw( /usr/sbin/sendmail /usr/lib/sendmail )) {
		if (-x $_) {
			$smtp_server = $_;
			last;
		}
	}
	$smtp_server ||= 'localhost'; # could be 127.0.0.1, too... *shrug*
}

if ($compose && $compose > 0) {
	@files = ($compose_filename . ".final", @files);
}

# Variables we set as part of the loop over files
our ($message_id, %mail, $subject, $reply_to, $references, $message,
	$needs_confirm, $message_num, $ask_default);

sub extract_valid_address {
	my $address = shift;
	my $local_part_regexp = qr/[^<>"\s@]+/;
	my $domain_regexp = qr/[^.<>"\s@]+(?:\.[^.<>"\s@]+)+/;

	# check for a local address:
	return $address if ($address =~ /^($local_part_regexp)$/);

	$address =~ s/^\s*<(.*)>\s*$/$1/;
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

my ($message_id_stamp, $message_id_serial);
sub make_message_id {
	my $uniq;
	if (!defined $message_id_stamp) {
		$message_id_stamp = sprintf("%s-%s", time, $$);
		$message_id_serial = 0;
	}
	$message_id_serial++;
	$uniq = "$message_id_stamp-$message_id_serial";

	my $du_part;
	for ($sender, $repocommitter, $repoauthor) {
		$du_part = extract_valid_address(sanitize_address($_));
		last if (defined $du_part and $du_part ne '');
	}
	if (not defined $du_part or $du_part eq '') {
		require Sys::Hostname;
		$du_part = 'user@' . Sys::Hostname::hostname();
	}
	my $message_id_template = "<%s-git-send-email-%s>";
	$message_id = sprintf($message_id_template, $uniq, $du_part);
	#print "new message id = $message_id\n"; # Was useful for debugging
}



$time = time - scalar $#files;

sub unquote_rfc2047 {
	local ($_) = @_;
	my $encoding;
	if (s/=\?([^?]+)\?q\?(.*)\?=/$2/g) {
		$encoding = $1;
		s/_/ /g;
		s/=([0-9A-F]{2})/chr(hex($1))/eg;
	}
	return wantarray ? ($_, $encoding) : $_;
}

sub quote_rfc2047 {
	local $_ = shift;
	my $encoding = shift || 'UTF-8';
	s/([^-a-zA-Z0-9!*+\/])/sprintf("=%02X", ord($1))/eg;
	s/(.*)/=\?$encoding\?q\?$1\?=/;
	return $_;
}

sub is_rfc2047_quoted {
	my $s = shift;
	my $token = qr/[^][()<>@,;:"\/?.= \000-\037\177-\377]+/;
	my $encoded_text = qr/[!->@-~]+/;
	length($s) <= 75 &&
	$s =~ m/^(?:"[[:ascii:]]*"|=\?$token\?$token\?$encoded_text\?=)$/o;
}

# use the simplest quoting being able to handle the recipient
sub sanitize_address {
	my ($recipient) = @_;
	my ($recipient_name, $recipient_addr) = ($recipient =~ /^(.*?)\s*(<.*)/);

	if (not $recipient_name) {
		return $recipient;
	}

	# if recipient_name is already quoted, do nothing
	if (is_rfc2047_quoted($recipient_name)) {
		return $recipient;
	}

	# rfc2047 is needed if a non-ascii char is included
	if ($recipient_name =~ /[^[:ascii:]]/) {
		$recipient_name =~ s/^"(.*)"$/$1/;
		$recipient_name = quote_rfc2047($recipient_name);
	}

	# double quotes are needed if specials or CTLs are included
	elsif ($recipient_name =~ /[][()<>@,;:\\".\000-\037\177]/) {
		$recipient_name =~ s/(["\\\r])/\\$1/g;
		$recipient_name = qq["$recipient_name"];
	}

	return "$recipient_name $recipient_addr";

}

# Returns the local Fully Qualified Domain Name (FQDN) if available.
#
# Tightly configured MTAa require that a caller sends a real DNS
# domain name that corresponds the IP address in the HELO/EHLO
# handshake. This is used to verify the connection and prevent
# spammers from trying to hide their identity. If the DNS and IP don't
# match, the receiveing MTA may deny the connection.
#
# Here is a deny example of Net::SMTP with the default "localhost.localdomain"
#
# Net::SMTP=GLOB(0x267ec28)>>> EHLO localhost.localdomain
# Net::SMTP=GLOB(0x267ec28)<<< 550 EHLO argument does not match calling host
#
# This maildomain*() code is based on ideas in Perl library Test::Reporter
# /usr/share/perl5/Test/Reporter/Mail/Util.pm ==> sub _maildomain ()

sub valid_fqdn {
	my $domain = shift;
	return defined $domain && !($^O eq 'darwin' && $domain =~ /\.local$/) && $domain =~ /\./;
}

sub maildomain_net {
	my $maildomain;

	if (eval { require Net::Domain; 1 }) {
		my $domain = Net::Domain::domainname();
		$maildomain = $domain if valid_fqdn($domain);
	}

	return $maildomain;
}

sub maildomain_mta {
	my $maildomain;

	if (eval { require Net::SMTP; 1 }) {
		for my $host (qw(mailhost localhost)) {
			my $smtp = Net::SMTP->new($host);
			if (defined $smtp) {
				my $domain = $smtp->domain;
				$smtp->quit;

				$maildomain = $domain if valid_fqdn($domain);

				last if $maildomain;
			}
		}
	}

	return $maildomain;
}

sub maildomain {
	return maildomain_net() || maildomain_mta() || 'localhost.localdomain';
}

# Returns 1 if the message was sent, and 0 otherwise.
# In actuality, the whole program dies when there
# is an error sending a message.

sub send_message {
	my @recipients = unique_email_list(@to);
	@cc = (grep { my $cc = extract_valid_address($_);
		      not grep { $cc eq $_ || $_ =~ /<\Q${cc}\E>$/ } @recipients
		    }
	       map { sanitize_address($_) }
	       @cc);
	my $to = join (",\n\t", @recipients);
	@recipients = unique_email_list(@recipients,@cc,@bcclist);
	@recipients = (map { extract_valid_address($_) } @recipients);
	my $date = format_2822_time($time++);
	my $gitversion = '@@GIT_VERSION@@';
	if ($gitversion =~ m/..GIT_VERSION../) {
	    $gitversion = Git::version();
	}

	my $cc = join(",\n\t", unique_email_list(@cc));
	my $ccline = "";
	if ($cc ne '') {
		$ccline = "\nCc: $cc";
	}
	my $sanitized_sender = sanitize_address($sender);
	make_message_id() unless defined($message_id);

	my $header = "From: $sanitized_sender
To: $to${ccline}
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

	my @sendmail_parameters = ('-i', @recipients);
	my $raw_from = $sanitized_sender;
	if (defined $envelope_sender && $envelope_sender ne "auto") {
		$raw_from = $envelope_sender;
	}
	$raw_from = extract_valid_address($raw_from);
	unshift (@sendmail_parameters,
			'-f', $raw_from) if(defined $envelope_sender);

	if ($needs_confirm && !$dry_run) {
		print "\n$header\n";
		if ($needs_confirm eq "inform") {
			$confirm_unconfigured = 0; # squelch this message for the rest of this run
			$ask_default = "y"; # assume yes on EOF since user hasn't explicitly asked for confirmation
			print "    The Cc list above has been expanded by additional\n";
			print "    addresses found in the patch commit message. By default\n";
			print "    send-email prompts before sending whenever this occurs.\n";
			print "    This behavior is controlled by the sendemail.confirm\n";
			print "    configuration setting.\n";
			print "\n";
			print "    For additional information, run 'git send-email --help'.\n";
			print "    To retain the current behavior, but squelch this message,\n";
			print "    run 'git config --global sendemail.confirm auto'.\n\n";
		}
		$_ = ask("Send this email? ([y]es|[n]o|[q]uit|[a]ll): ",
		         valid_re => qr/^(?:yes|y|no|n|quit|q|all|a)/i,
		         default => $ask_default);
		die "Send this email reply required" unless defined $_;
		if (/^n/i) {
			return 0;
		} elsif (/^q/i) {
			cleanup_compose_files();
			exit(0);
		} elsif (/^a/i) {
			$confirm = 'never';
		}
	}

	unshift (@sendmail_parameters, @smtp_server_options);

	if ($dry_run) {
		# We don't want to send the email.
	} elsif ($smtp_server =~ m#^/#) {
		my $pid = open my $sm, '|-';
		defined $pid or die $!;
		if (!$pid) {
			exec($smtp_server, @sendmail_parameters) or die $!;
		}
		print $sm "$header\n$message";
		close $sm or die $!;
	} else {

		if (!defined $smtp_server) {
			die "The required SMTP server is not properly defined."
		}

		if ($smtp_encryption eq 'ssl') {
			$smtp_server_port ||= 465; # ssmtp
			require Net::SMTP::SSL;
			$smtp_domain ||= maildomain();
			$smtp ||= Net::SMTP::SSL->new($smtp_server,
						      Hello => $smtp_domain,
						      Port => $smtp_server_port);
		}
		else {
			require Net::SMTP;
			$smtp_domain ||= maildomain();
			$smtp ||= Net::SMTP->new((defined $smtp_server_port)
						 ? "$smtp_server:$smtp_server_port"
						 : $smtp_server,
						 Hello => $smtp_domain,
						 Debug => $debug_net_smtp);
			if ($smtp_encryption eq 'tls' && $smtp) {
				require Net::SMTP::SSL;
				$smtp->command('STARTTLS');
				$smtp->response();
				if ($smtp->code == 220) {
					$smtp = Net::SMTP::SSL->start_SSL($smtp)
						or die "STARTTLS failed! ".$smtp->message;
					$smtp_encryption = '';
					# Send EHLO again to receive fresh
					# supported commands
					$smtp->hello();
				} else {
					die "Server does not support STARTTLS! ".$smtp->message;
				}
			}
		}

		if (!$smtp) {
			die "Unable to initialize SMTP properly. Check config and use --smtp-debug. ",
			    "VALUES: server=$smtp_server ",
			    "encryption=$smtp_encryption ",
			    "hello=$smtp_domain",
			    defined $smtp_server_port ? " port=$smtp_server_port" : "";
		}

		if (defined $smtp_authuser) {

			if (!defined $smtp_authpass) {

				system "stty -echo";

				do {
					print "Password: ";
					$_ = <STDIN>;
					print "\n";
				} while (!defined $_);

				chomp($smtp_authpass = $_);

				system "stty echo";
			}

			$auth ||= $smtp->auth( $smtp_authuser, $smtp_authpass ) or die $smtp->message;
		}

		$smtp->mail( $raw_from ) or die $smtp->message;
		$smtp->to( @recipients ) or die $smtp->message;
		$smtp->data or die $smtp->message;
		$smtp->datasend("$header\n$message") or die $smtp->message;
		$smtp->dataend() or die $smtp->message;
		$smtp->code =~ /250|200/ or die "Failed to send $subject\n".$smtp->message;
	}
	if ($quiet) {
		printf (($dry_run ? "Dry-" : "")."Sent %s\n", $subject);
	} else {
		print (($dry_run ? "Dry-" : "")."OK. Log says:\n");
		if ($smtp_server !~ m#^/#) {
			print "Server: $smtp_server\n";
			print "MAIL FROM:<$raw_from>\n";
			foreach my $entry (@recipients) {
			    print "RCPT TO:<$entry>\n";
			}
		} else {
			print "Sendmail: $smtp_server ".join(' ',@sendmail_parameters)."\n";
		}
		print $header, "\n";
		if ($smtp) {
			print "Result: ", $smtp->code, ' ',
				($smtp->message =~ /\n([^\n]+\n)$/s), "\n";
		} else {
			print "Result: OK\n";
		}
	}

	return 1;
}

$reply_to = $initial_reply_to;
$references = $initial_reply_to || '';
$subject = $initial_subject;
$message_num = 0;

foreach my $t (@files) {
	open my $fh, "<", $t or die "can't open file $t";

	my $author = undef;
	my $author_encoding;
	my $has_content_type;
	my $body_encoding;
	@to = ();
	@cc = ();
	@xh = ();
	my $input_format = undef;
	my @header = ();
	$message = "";
	$message_num++;
	# First unfold multiline header fields
	while(<$fh>) {
		last if /^\s*$/;
		if (/^\s+\S/ and @header) {
			chomp($header[$#header]);
			s/^\s+/ /;
			$header[$#header] .= $_;
	    } else {
			push(@header, $_);
		}
	}
	# Now parse the header
	foreach(@header) {
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
			}
			elsif (/^From:\s+(.*)$/) {
				($author, $author_encoding) = unquote_rfc2047($1);
				next if $suppress_cc{'author'};
				next if $suppress_cc{'self'} and $author eq $sender;
				printf("(mbox) Adding cc: %s from line '%s'\n",
					$1, $_) unless $quiet;
				push @cc, $1;
			}
			elsif (/^To:\s+(.*)$/) {
				foreach my $addr (parse_address_line($1)) {
					printf("(mbox) Adding to: %s from line '%s'\n",
						$addr, $_) unless $quiet;
					push @to, sanitize_address($addr);
				}
			}
			elsif (/^Cc:\s+(.*)$/) {
				foreach my $addr (parse_address_line($1)) {
					if (unquote_rfc2047($addr) eq $sender) {
						next if ($suppress_cc{'self'});
					} else {
						next if ($suppress_cc{'cc'});
					}
					printf("(mbox) Adding cc: %s from line '%s'\n",
						$addr, $_) unless $quiet;
					push @cc, $addr;
				}
			}
			elsif (/^Content-type:/i) {
				$has_content_type = 1;
				if (/charset="?([^ "]+)/) {
					$body_encoding = $1;
				}
				push @xh, $_;
			}
			elsif (/^Message-Id: (.*)/i) {
				$message_id = $1;
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
			if (@cc == 0 && !$suppress_cc{'cc'}) {
				printf("(non-mbox) Adding cc: %s from line '%s'\n",
					$_, $_) unless $quiet;
				push @cc, $_;
			} elsif (!defined $subject) {
				$subject = $_;
			}
		}
	}
	# Now parse the message body
	while(<$fh>) {
		$message .=  $_;
		if (/^(Signed-off-by|Cc): (.*)$/i) {
			chomp;
			my ($what, $c) = ($1, $2);
			chomp $c;
			if ($c eq $sender) {
				next if ($suppress_cc{'self'});
			} else {
				next if $suppress_cc{'sob'} and $what =~ /Signed-off-by/i;
				next if $suppress_cc{'bodycc'} and $what =~ /Cc/i;
			}
			push @cc, $c;
			printf("(body) Adding cc: %s from line '%s'\n",
				$c, $_) unless $quiet;
		}
	}
	close $fh;

	push @to, recipients_cmd("to-cmd", "to", $to_cmd, $t)
		if defined $to_cmd;
	push @cc, recipients_cmd("cc-cmd", "cc", $cc_cmd, $t)
		if defined $cc_cmd && !$suppress_cc{'cccmd'};

	if ($broken_encoding{$t} && !$has_content_type) {
		$has_content_type = 1;
		push @xh, "MIME-Version: 1.0",
			"Content-Type: text/plain; charset=$auto_8bit_encoding",
			"Content-Transfer-Encoding: 8bit";
		$body_encoding = $auto_8bit_encoding;
	}

	if ($broken_encoding{$t} && !is_rfc2047_quoted($subject)) {
		$subject = quote_rfc2047($subject, $auto_8bit_encoding);
	}

	if (defined $author and $author ne $sender) {
		$message = "From: $author\n\n$message";
		if (defined $author_encoding) {
			if ($has_content_type) {
				if ($body_encoding eq $author_encoding) {
					# ok, we already have the right encoding
				}
				else {
					# uh oh, we should re-encode
				}
			}
			else {
				$has_content_type = 1;
				push @xh,
				  'MIME-Version: 1.0',
				  "Content-Type: text/plain; charset=$author_encoding",
				  'Content-Transfer-Encoding: 8bit';
			}
		}
	}

	$needs_confirm = (
		$confirm eq "always" or
		($confirm =~ /^(?:auto|cc)$/ && @cc) or
		($confirm =~ /^(?:auto|compose)$/ && $compose && $message_num == 1));
	$needs_confirm = "inform" if ($needs_confirm && $confirm_unconfigured && @cc);

	@to = (@initial_to, @to);
	@cc = (@initial_cc, @cc);

	my $message_was_sent = send_message();

	# set up for the next message
	if ($thread && $message_was_sent &&
		(chain_reply_to() || !defined $reply_to || length($reply_to) == 0 ||
		$message_num == 1)) {
		$reply_to = $message_id;
		if (length $references > 0) {
			$references .= "\n $message_id";
		} else {
			$references = "$message_id";
		}
	}
	$message_id = undef;
}

# Execute a command (e.g. $to_cmd) to get a list of email addresses
# and return a results array
sub recipients_cmd {
	my ($prefix, $what, $cmd, $file) = @_;

	my $sanitized_sender = sanitize_address($sender);
	my @addresses = ();
	open my $fh, "$cmd \Q$file\E |"
	    or die "($prefix) Could not execute '$cmd'";
	while (my $address = <$fh>) {
		$address =~ s/^\s*//g;
		$address =~ s/\s*$//g;
		$address = sanitize_address($address);
		next if ($address eq $sanitized_sender and $suppress_from);
		push @addresses, $address;
		printf("($prefix) Adding %s: %s from: '%s'\n",
		       $what, $address, $cmd) unless $quiet;
		}
	close $fh
	    or die "($prefix) failed to close pipe to '$cmd'";
	return @addresses;
}

cleanup_compose_files();

sub cleanup_compose_files {
	unlink($compose_filename, $compose_filename . ".final") if $compose;
}

$smtp->quit if $smtp;

sub unique_email_list {
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

sub validate_patch {
	my $fn = shift;
	open(my $fh, '<', $fn)
		or die "unable to open $fn: $!\n";
	while (my $line = <$fh>) {
		if (length($line) > 998) {
			return "$.: patch contains a line longer than 998 characters";
		}
	}
	return undef;
}

sub file_has_nonascii {
	my $fn = shift;
	open(my $fh, '<', $fn)
		or die "unable to open $fn: $!\n";
	while (my $line = <$fh>) {
		return 1 if $line =~ /[^[:ascii:]]/;
	}
	return 0;
}

sub body_or_subject_has_nonascii {
	my $fn = shift;
	open(my $fh, '<', $fn)
		or die "unable to open $fn: $!\n";
	while (my $line = <$fh>) {
		last if $line =~ /^$/;
		return 1 if $line =~ /^Subject.*[^[:ascii:]]/;
	}
	while (my $line = <$fh>) {
		return 1 if $line =~ /[^[:ascii:]]/;
	}
	return 0;
}
