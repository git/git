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

require v5.26;
use strict;
use warnings $ENV{GIT_PERL_FATAL_WARNINGS} ? qw(FATAL all) : ();
use Getopt::Long;
use Git::LoadCPAN::Error qw(:try);
use Git;
use Git::I18N;

Getopt::Long::Configure qw/ pass_through /;

sub usage {
	print <<EOT;
git send-email [<options>] <file|directory>
git send-email [<options>] <format-patch options>
git send-email --dump-aliases
git send-email --translate-aliases

  Composing:
    --from                  <str>  * Email From:
    --[no-]to               <str>  * Email To:
    --[no-]cc               <str>  * Email Cc:
    --[no-]bcc              <str>  * Email Bcc:
    --subject               <str>  * Email "Subject:"
    --reply-to              <str>  * Email "Reply-To:"
    --in-reply-to           <str>  * Email "In-Reply-To:"
    --[no-]outlook-id-fix          * The SMTP host is an Outlook server that munges the
                                     Message-ID. Retrieve it from the server.
    --[no-]xmailer                 * Add "X-Mailer:" header (default).
    --[no-]annotate                * Review each patch that will be sent in an editor.
    --compose                      * Open an editor for introduction.
    --compose-encoding      <str>  * Encoding to assume for introduction.
    --8bit-encoding         <str>  * Encoding to assume 8bit mails if undeclared
    --transfer-encoding     <str>  * Transfer encoding to use (quoted-printable, 8bit, base64)
    --[no-]mailmap                 * Use mailmap file to map all email addresses to canonical
                                     real names and email addresses.

  Sending:
    --envelope-sender       <str>  * Email envelope sender.
    --sendmail-cmd          <str>  * Command to run to send email.
    --smtp-server       <str:int>  * Outgoing SMTP server to use. The port
                                     is optional. Default 'localhost'.
    --smtp-server-option    <str>  * Outgoing SMTP server option to use.
    --smtp-server-port      <int>  * Outgoing SMTP server port.
    --smtp-user             <str>  * Username for SMTP-AUTH.
    --smtp-pass             <str>  * Password for SMTP-AUTH; not necessary.
    --smtp-encryption       <str>  * tls or ssl; anything else disables.
    --smtp-ssl                     * Deprecated. Use '--smtp-encryption ssl'.
    --smtp-ssl-cert-path    <str>  * Path to ca-certificates (either directory or file).
                                     Pass an empty string to disable certificate
                                     verification.
    --smtp-domain           <str>  * The domain name sent to HELO/EHLO handshake
    --smtp-auth             <str>  * Space-separated list of allowed AUTH mechanisms, or
                                     "none" to disable authentication.
                                     This setting forces to use one of the listed mechanisms.
    --no-smtp-auth                 * Disable SMTP authentication. Shorthand for
                                     `--smtp-auth=none`
    --smtp-debug            <0|1>  * Disable, enable Net::SMTP debug.

    --batch-size            <int>  * send max <int> message per connection.
    --relogin-delay         <int>  * delay <int> seconds between two successive login.
                                     This option can only be used with --batch-size

  Automating:
    --identity              <str>  * Use the sendemail.<id> options.
    --to-cmd                <str>  * Email To: via `<str> \$patch_path`.
    --cc-cmd                <str>  * Email Cc: via `<str> \$patch_path`.
    --header-cmd            <str>  * Add headers via `<str> \$patch_path`.
    --no-header-cmd                * Disable any header command in use.
    --suppress-cc           <str>  * author, self, sob, cc, cccmd, body, bodycc, misc-by, all.
    --[no-]cc-cover                * Email Cc: addresses in the cover letter.
    --[no-]to-cover                * Email To: addresses in the cover letter.
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

  Information:
    --dump-aliases                 * Dump configured aliases and exit.
    --translate-aliases            * Translate aliases read from standard
                                     input according to the configured email
                                     alias file(s), outputting the result to
                                     standard output.

EOT
	exit(1);
}

sub uniq {
	my %seen;
	grep !$seen{$_}++, @_;
}

sub completion_helper {
	my ($original_opts) = @_;
	my %not_for_completion = (
		"git-completion-helper" => undef,
		"h" => undef,
	);
	my @send_email_opts = ();

	foreach my $key (keys %$original_opts) {
		unless (exists $not_for_completion{$key}) {
			my $negatable = ($key =~ s/!$//);

			if ($key =~ /[:=][si]$/) {
				$key =~ s/[:=][si]$//;
				push (@send_email_opts, "--$_=") foreach (split (/\|/, $key));
			} else {
				push (@send_email_opts, "--$_") foreach (split (/\|/, $key));
				if ($negatable) {
					push (@send_email_opts, "--no-$_") foreach (split (/\|/, $key));
				}
			}
		}
	}

	my @format_patch_opts = split(/ /, Git::command('format-patch', '--git-completion-helper'));
	my @opts = (@send_email_opts, @format_patch_opts);
	@opts = uniq (grep !/^$/, @opts);
	# There's an implicit '\n' here already, no need to add an explicit one.
	print "@opts";
	exit(0);
}

# most mail servers generate the Date: header, but not all...
sub format_2822_time {
	my ($time) = @_;
	my @localtm = localtime($time);
	my @gmttm = gmtime($time);
	my $localmin = $localtm[1] + $localtm[2] * 60;
	my $gmtmin = $gmttm[1] + $gmttm[2] * 60;
	if ($localtm[0] != $gmttm[0]) {
		die __("local zone differs from GMT by a non-minute interval\n");
	}
	if ((($gmttm[6] + 1) % 7) == $localtm[6]) {
		$localmin += 1440;
	} elsif ((($gmttm[6] - 1) % 7) == $localtm[6]) {
		$localmin -= 1440;
	} elsif ($gmttm[6] != $localtm[6]) {
		die __("local time offset greater than or equal to 24 hours\n");
	}
	my $offset = $localmin - $gmtmin;
	my $offhour = $offset / 60;
	my $offmin = abs($offset % 60);
	if (abs($offhour) >= 24) {
		die __("local time offset greater than or equal to 24 hours\n");
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

my $smtp;
my $auth;
my $num_sent = 0;

# Regexes for RFC 2047 productions.
my $re_token = qr/[^][()<>@,;:\\"\/?.= \000-\037\177-\377]+/;
my $re_encoded_text = qr/[^? \000-\037\177-\377]+/;
my $re_encoded_word = qr/=\?($re_token)\?($re_token)\?($re_encoded_text)\?=/;

# Variables we fill in automatically, or via prompting:
my (@to,@cc,@xh,$envelope_sender,
	$initial_in_reply_to,$reply_to,$initial_subject,@files,
	$author,$sender,$smtp_authpass,$annotate,$compose,$time);
# Things we either get from config, *or* are overridden on the
# command-line.
my ($no_cc, $no_to, $no_bcc, $no_identity, $no_header_cmd);
my (@config_to, @getopt_to);
my (@config_cc, @getopt_cc);
my (@config_bcc, @getopt_bcc);

# Example reply to:
#$initial_in_reply_to = ''; #<20050203173208.GA23964@foobar.com>';

my $repo = eval { Git->repository() };
my @repo = $repo ? ($repo) : ();

# Behavior modification variables
my ($quiet, $dry_run) = (0, 0);
my $format_patch;
my $compose_filename;
my $force = 0;
my $dump_aliases = 0;
my $translate_aliases = 0;

# Variables to prevent short format-patch options from being captured
# as abbreviated send-email options
my $reroll_count;

# Handle interactive edition of files.
my $multiedit;
my $editor;

sub system_or_msg {
	my ($args, $msg, $cmd_name) = @_;
	system(@$args);
	my $signalled = $? & 127;
	my $exit_code = $? >> 8;
	return unless $signalled or $exit_code;

	my @sprintf_args = ($cmd_name ? $cmd_name : $args->[0], $exit_code);
	if (defined $msg) {
		# Quiet the 'redundant' warning category, except we
		# need to support down to Perl 5.8.1, so we can't do a
		# "no warnings 'redundant'", since that category was
		# introduced in perl 5.22, and asking for it will die
		# on older perls.
		no warnings;
		return sprintf($msg, @sprintf_args);
	}
	return sprintf(__("fatal: command '%s' died with exit code %d"),
		       @sprintf_args);
}

sub system_or_die {
	my $msg = system_or_msg(@_);
	die $msg if $msg;
}

sub do_edit {
	if (!defined($editor)) {
		$editor = Git::command_oneline('var', 'GIT_EDITOR');
	}
	my $die_msg = __("the editor exited uncleanly, aborting everything");
	if (defined($multiedit) && !$multiedit) {
		system_or_die(['sh', '-c', $editor.' "$@"', $editor, $_], $die_msg) for @_;
	} else {
		system_or_die(['sh', '-c', $editor.' "$@"', $editor, @_], $die_msg);
	}
}

# Variables with corresponding config settings
my ($suppress_from, $signed_off_by_cc);
my ($cover_cc, $cover_to);
my ($to_cmd, $cc_cmd, $header_cmd);
my ($smtp_server, $smtp_server_port, @smtp_server_options);
my ($smtp_authuser, $smtp_encryption, $smtp_ssl_cert_path);
my ($batch_size, $relogin_delay);
my ($identity, $aliasfiletype, @alias_files, $smtp_domain, $smtp_auth);
my ($confirm);
my (@suppress_cc);
my ($auto_8bit_encoding);
my ($compose_encoding);
my ($sendmail_cmd);
my ($mailmap_file, $mailmap_blob);
# Variables with corresponding config settings & hardcoded defaults
my ($debug_net_smtp) = 0;		# Net::SMTP, see send_message()
my $thread = 1;
my $chain_reply_to = 0;
my $use_xmailer = 1;
my $validate = 1;
my $mailmap = 0;
my $target_xfer_encoding = 'auto';
my $forbid_sendmail_variables = 1;
my $outlook_id_fix = 'auto';

my %config_bool_settings = (
    "thread" => \$thread,
    "chainreplyto" => \$chain_reply_to,
    "suppressfrom" => \$suppress_from,
    "signedoffbycc" => \$signed_off_by_cc,
    "cccover" => \$cover_cc,
    "tocover" => \$cover_to,
    "signedoffcc" => \$signed_off_by_cc,
    "validate" => \$validate,
    "multiedit" => \$multiedit,
    "annotate" => \$annotate,
    "xmailer" => \$use_xmailer,
    "forbidsendmailvariables" => \$forbid_sendmail_variables,
    "mailmap" => \$mailmap,
    "outlookidfix" => \$outlook_id_fix,
);

my %config_settings = (
    "smtpencryption" => \$smtp_encryption,
    "smtpserver" => \$smtp_server,
    "smtpserverport" => \$smtp_server_port,
    "smtpserveroption" => \@smtp_server_options,
    "smtpuser" => \$smtp_authuser,
    "smtppass" => \$smtp_authpass,
    "smtpdomain" => \$smtp_domain,
    "smtpauth" => \$smtp_auth,
    "smtpbatchsize" => \$batch_size,
    "smtprelogindelay" => \$relogin_delay,
    "to" => \@config_to,
    "tocmd" => \$to_cmd,
    "cc" => \@config_cc,
    "cccmd" => \$cc_cmd,
    "headercmd" => \$header_cmd,
    "aliasfiletype" => \$aliasfiletype,
    "bcc" => \@config_bcc,
    "suppresscc" => \@suppress_cc,
    "envelopesender" => \$envelope_sender,
    "confirm"   => \$confirm,
    "from" => \$sender,
    "assume8bitencoding" => \$auto_8bit_encoding,
    "composeencoding" => \$compose_encoding,
    "transferencoding" => \$target_xfer_encoding,
    "sendmailcmd" => \$sendmail_cmd,
);

my %config_path_settings = (
    "aliasesfile" => \@alias_files,
    "smtpsslcertpath" => \$smtp_ssl_cert_path,
    "mailmap.file" => \$mailmap_file,
    "mailmap.blob" => \$mailmap_blob,
);

# Handle Uncouth Termination
sub signal_handler {
	# Make text normal
	require Term::ANSIColor;
	print Term::ANSIColor::color("reset"), "\n";

	# SMTP password masked
	system "stty echo";

	# tmp files from --compose
	if (defined $compose_filename) {
		if (-e $compose_filename) {
			printf __("'%s' contains an intermediate version ".
				  "of the email you were composing.\n"),
				  $compose_filename;
		}
		if (-e ($compose_filename . ".final")) {
			printf __("'%s.final' contains the composed email.\n"),
				  $compose_filename;
		}
	}

	exit;
};

$SIG{TERM} = \&signal_handler;
$SIG{INT}  = \&signal_handler;

# Read our sendemail.* config
sub read_config {
	my ($known_keys, $configured, $prefix) = @_;

	foreach my $setting (keys %config_bool_settings) {
		my $target = $config_bool_settings{$setting};
		my $key = "$prefix.$setting";
		next unless exists $known_keys->{$key};
		my $v = (@{$known_keys->{$key}} == 1 &&
			 (defined $known_keys->{$key}->[0] &&
			  $known_keys->{$key}->[0] =~ /^(?:true|false)$/s))
			? $known_keys->{$key}->[0] eq 'true'
			: Git::config_bool(@repo, $key);
		next unless defined $v;
		next if $configured->{$setting}++;
		$$target = $v;
	}

	foreach my $setting (keys %config_path_settings) {
		my $target = $config_path_settings{$setting};
		my $key = "$prefix.$setting";
		next unless exists $known_keys->{$key};
		if (ref($target) eq "ARRAY") {
			my @values = Git::config_path(@repo, $key);
			next unless @values;
			next if $configured->{$setting}++;
			@$target = @values;
		}
		else {
			my $v = Git::config_path(@repo, "$prefix.$setting");
			next unless defined $v;
			next if $configured->{$setting}++;
			$$target = $v;
		}
	}

	foreach my $setting (keys %config_settings) {
		my $target = $config_settings{$setting};
		my $key = "$prefix.$setting";
		next unless exists $known_keys->{$key};
		if (ref($target) eq "ARRAY") {
			my @values = @{$known_keys->{$key}};
			@values = grep { defined } @values;
			next if $configured->{$setting}++;
			@$target = @values;
		}
		else {
			my $v = $known_keys->{$key}->[-1];
			next unless defined $v;
			next if $configured->{$setting}++;
			$$target = $v;
		}
	}
}

sub config_regexp {
	my ($regex) = @_;
	my @ret;
	eval {
		my $ret = Git::command(
			'config',
			'--null',
			'--get-regexp',
			$regex,
		);
		@ret = map {
			# We must always return ($k, $v) here, since
			# empty config values will be just "key\0",
			# not "key\nvalue\0".
			my ($k, $v) = split /\n/, $_, 2;
			($k, $v);
		} split /\0/, $ret;
		1;
	} or do {
		# If we have no keys we're OK, otherwise re-throw
		die $@ if $@->value != 1;
	};
	return @ret;
}

# Save ourselves a lot of work of shelling out to 'git config' (it
# parses 'bool' etc.) by only doing so for config keys that exist.
my %known_config_keys;
{
	my @kv = config_regexp("^sende?mail[.]");
	while (my ($k, $v) = splice @kv, 0, 2) {
		push @{$known_config_keys{$k}} => $v;
	}
}

# sendemail.identity yields to --identity. We must parse this
# special-case first before the rest of the config is read.
{
	my $key = "sendemail.identity";
	$identity = Git::config(@repo, $key) if exists $known_config_keys{$key};
}
my %identity_options = (
	"identity=s" => \$identity,
	"no-identity" => \$no_identity,
);
my $rc = GetOptions(%identity_options);
usage() unless $rc;
undef $identity if $no_identity;

# Now we know enough to read the config
{
    my %configured;
    read_config(\%known_config_keys, \%configured, "sendemail.$identity") if defined $identity;
    read_config(\%known_config_keys, \%configured, "sendemail");
}

# Begin by accumulating all the variables (defined above), that we will end up
# needing, first, from the command line:

my $help;
my $git_completion_helper;
my %dump_aliases_options = (
	"h" => \$help,
	"dump-aliases" => \$dump_aliases,
	"translate-aliases" => \$translate_aliases,
);
$rc = GetOptions(%dump_aliases_options);
usage() unless $rc;
die __("--dump-aliases incompatible with other options\n")
    if !$help and ($dump_aliases or $translate_aliases) and @ARGV;
die __("--dump-aliases and --translate-aliases are mutually exclusive\n")
    if !$help and $dump_aliases and $translate_aliases;
my %options = (
		    "sender|from=s" => \$sender,
		    "in-reply-to=s" => \$initial_in_reply_to,
		    "reply-to=s" => \$reply_to,
		    "subject=s" => \$initial_subject,
		    "to=s" => \@getopt_to,
		    "to-cmd=s" => \$to_cmd,
		    "no-to" => \$no_to,
		    "cc=s" => \@getopt_cc,
		    "no-cc" => \$no_cc,
		    "bcc=s" => \@getopt_bcc,
		    "no-bcc" => \$no_bcc,
		    "chain-reply-to!" => \$chain_reply_to,
		    "sendmail-cmd=s" => \$sendmail_cmd,
		    "smtp-server=s" => \$smtp_server,
		    "smtp-server-option=s" => \@smtp_server_options,
		    "smtp-server-port=s" => \$smtp_server_port,
		    "smtp-user=s" => \$smtp_authuser,
		    "smtp-pass:s" => \$smtp_authpass,
		    "smtp-ssl" => sub { $smtp_encryption = 'ssl' },
		    "smtp-encryption=s" => \$smtp_encryption,
		    "smtp-ssl-cert-path=s" => \$smtp_ssl_cert_path,
		    "smtp-debug:i" => \$debug_net_smtp,
		    "smtp-domain:s" => \$smtp_domain,
		    "smtp-auth=s" => \$smtp_auth,
		    "no-smtp-auth" => sub {$smtp_auth = 'none'},
		    "annotate!" => \$annotate,
		    "compose" => \$compose,
		    "quiet" => \$quiet,
		    "cc-cmd=s" => \$cc_cmd,
		    "header-cmd=s" => \$header_cmd,
		    "no-header-cmd" => \$no_header_cmd,
		    "suppress-from!" => \$suppress_from,
		    "suppress-cc=s" => \@suppress_cc,
		    "signed-off-cc|signed-off-by-cc!" => \$signed_off_by_cc,
		    "cc-cover!" => \$cover_cc,
		    "to-cover!" => \$cover_to,
		    "confirm=s" => \$confirm,
		    "dry-run" => \$dry_run,
		    "envelope-sender=s" => \$envelope_sender,
		    "thread!" => \$thread,
		    "validate!" => \$validate,
		    "transfer-encoding=s" => \$target_xfer_encoding,
		    "mailmap!" => \$mailmap,
		    "use-mailmap!" => \$mailmap,
		    "format-patch!" => \$format_patch,
		    "8bit-encoding=s" => \$auto_8bit_encoding,
		    "compose-encoding=s" => \$compose_encoding,
		    "force" => \$force,
		    "xmailer!" => \$use_xmailer,
		    "batch-size=i" => \$batch_size,
		    "relogin-delay=i" => \$relogin_delay,
		    "git-completion-helper" => \$git_completion_helper,
		    "v=s" => \$reroll_count,
		    "outlook-id-fix!" => \$outlook_id_fix,
);
$rc = GetOptions(%options);

# Munge any "either config or getopt, not both" variables
my @initial_to = @getopt_to ? @getopt_to : ($no_to ? () : @config_to);
my @initial_cc = @getopt_cc ? @getopt_cc : ($no_cc ? () : @config_cc);
my @initial_bcc = @getopt_bcc ? @getopt_bcc : ($no_bcc ? () : @config_bcc);

usage() if $help;
my %all_options = (%options, %dump_aliases_options, %identity_options);
completion_helper(\%all_options) if $git_completion_helper;
unless ($rc) {
    usage();
}

if ($forbid_sendmail_variables && grep { /^sendmail/s } keys %known_config_keys) {
	die __("fatal: found configuration options for 'sendmail'\n" .
		"git-send-email is configured with the sendemail.* options - note the 'e'.\n" .
		"Set sendemail.forbidSendmailVariables to false to disable this check.\n");
}

die __("Cannot run git format-patch from outside a repository\n")
	if $format_patch and not $repo;

die __("`batch-size` and `relogin` must be specified together " .
	"(via command-line or configuration option)\n")
	if defined $relogin_delay and not defined $batch_size;

# 'default' encryption is none -- this only prevents a warning
$smtp_encryption = '' unless (defined $smtp_encryption);

# Set CC suppressions
my(%suppress_cc);
if (@suppress_cc) {
	foreach my $entry (@suppress_cc) {
		# Please update $__git_send_email_suppresscc_options
		# in git-completion.bash when you add new options.
		die sprintf(__("Unknown --suppress-cc field: '%s'\n"), $entry)
			unless $entry =~ /^(?:all|cccmd|cc|author|self|sob|body|bodycc|misc-by)$/;
		$suppress_cc{$entry} = 1;
	}
}

if ($suppress_cc{'all'}) {
	foreach my $entry (qw (cccmd cc author self sob body bodycc misc-by)) {
		$suppress_cc{$entry} = 1;
	}
	delete $suppress_cc{'all'};
}

# If explicit old-style ones are specified, they trump --suppress-cc.
$suppress_cc{'self'} = $suppress_from if defined $suppress_from;
$suppress_cc{'sob'} = !$signed_off_by_cc if defined $signed_off_by_cc;

if ($suppress_cc{'body'}) {
	foreach my $entry (qw (sob bodycc misc-by)) {
		$suppress_cc{$entry} = 1;
	}
	delete $suppress_cc{'body'};
}

# Set confirm's default value
my $confirm_unconfigured = !defined $confirm;
if ($confirm_unconfigured) {
	$confirm = scalar %suppress_cc ? 'compose' : 'auto';
};
# Please update $__git_send_email_confirm_options in
# git-completion.bash when you add new options.
die sprintf(__("Unknown --confirm setting: '%s'\n"), $confirm)
	unless $confirm =~ /^(?:auto|cc|compose|always|never)/;

# Debugging, print out the suppressions.
if (0) {
	print "suppressions:\n";
	foreach my $entry (keys %suppress_cc) {
		printf "  %-5s -> $suppress_cc{$entry}\n", $entry;
	}
}

my ($repoauthor, $repocommitter);
{
	my %cache;
	my ($author, $committer);
	my $common = sub {
		my ($what) = @_;
		return $cache{$what} if exists $cache{$what};
		($cache{$what}) = Git::ident_person(@repo, $what);
		return $cache{$what};
	};
	$repoauthor = sub { $common->('author') };
	$repocommitter = sub { $common->('committer') };
}

sub parse_address_line {
	require Git::LoadCPAN::Mail::Address;
	return map { $_->format } Mail::Address->parse($_[0]);
}

sub split_addrs {
	require Text::ParseWords;
	return Text::ParseWords::quotewords('\s*,\s*', 1, @_);
}

my %aliases;

sub parse_sendmail_alias {
	local $_ = shift;
	if (/"/) {
		printf STDERR __("warning: sendmail alias with quotes is not supported: %s\n"), $_;
	} elsif (/:include:/) {
		printf STDERR __("warning: `:include:` not supported: %s\n"), $_;
	} elsif (/[\/|]/) {
		printf STDERR __("warning: `/file` or `|pipe` redirection not supported: %s\n"), $_;
	} elsif (/^(\S+?)\s*:\s*(.+)$/) {
		my ($alias, $addr) = ($1, $2);
		$aliases{$alias} = [ split_addrs($addr) ];
	} else {
		printf STDERR __("warning: sendmail line is not recognized: %s\n"), $_;
	}
}

sub parse_sendmail_aliases {
	my $fh = shift;
	my $s = '';
	while (<$fh>) {
		chomp;
		next if /^\s*$/ || /^\s*#/;
		$s .= $_, next if $s =~ s/\\$// || s/^\s+//;
		parse_sendmail_alias($s) if $s;
		$s = $_;
	}
	$s =~ s/\\$//; # silently tolerate stray '\' on last line
	parse_sendmail_alias($s) if $s;
}

my %parse_alias = (
	# multiline formats can be supported in the future
	mutt => sub { my $fh = shift; while (<$fh>) {
		if (/^\s*alias\s+(?:-group\s+\S+\s+)*(\S+)\s+(.*)$/) {
			my ($alias, $addr) = ($1, $2);
			$addr =~ s/#.*$//; # mutt allows # comments
			# commas delimit multiple addresses
			my @addr = split_addrs($addr);

			# quotes may be escaped in the file,
			# unescape them so we do not double-escape them later.
			s/\\"/"/g foreach @addr;
			$aliases{$alias} = \@addr
		}}},
	mailrc => sub {	my $fh = shift; while (<$fh>) {
		if (/^alias\s+(\S+)\s+(.*?)\s*$/) {
			require Text::ParseWords;
			# spaces delimit multiple addresses
			$aliases{$1} = [ Text::ParseWords::quotewords('\s+', 0, $2) ];
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
	sendmail => \&parse_sendmail_aliases,
	gnus => sub { my $fh = shift; while (<$fh>) {
		if (/\(define-mail-alias\s+"(\S+?)"\s+"(\S+?)"\)/) {
			$aliases{$1} = [ $2 ];
		}}}
	# Please update _git_config() in git-completion.bash when you
	# add new MUAs.
);

if (@alias_files and $aliasfiletype and defined $parse_alias{$aliasfiletype}) {
	foreach my $file (@alias_files) {
		open my $fh, '<', $file or die "opening $file: $!\n";
		$parse_alias{$aliasfiletype}->($fh);
		close $fh;
	}
}

if ($dump_aliases) {
    print "$_\n" for (sort keys %aliases);
    exit(0);
}

if ($translate_aliases) {
	while (<STDIN>) {
		my @addr_list = parse_address_line($_);
		@addr_list = expand_aliases(@addr_list);
		@addr_list = sanitize_address_list(@addr_list);
		print "$_\n" for @addr_list;
	}
	exit(0);
}

# is_format_patch_arg($f) returns 0 if $f names a patch, or 1 if
# $f is a revision list specification to be passed to format-patch.
sub is_format_patch_arg {
	return unless $repo;
	my $f = shift;
	try {
		$repo->command('rev-parse', '--verify', '--quiet', $f);
		if (defined($format_patch)) {
			return $format_patch;
		}
		die sprintf(__(<<EOF), $f, $f);
File '%s' exists but it could also be the range of commits
to produce patches for.  Please disambiguate by...

    * Saying "./%s" if you mean a file; or
    * Giving --format-patch option if you mean a range.
EOF
	} catch Git::Error::Command with {
		# Not a valid revision.  Treat it as a filename.
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
	} elsif (-d $f and !is_format_patch_arg($f)) {
		opendir my $dh, $f
			or die sprintf(__("Failed to opendir %s: %s"), $f, $!);

		require File::Spec;
		push @files, grep { -f $_ } map { File::Spec->catfile($f, $_) }
				sort readdir $dh;
		closedir $dh;
	} elsif ((-f $f or -p $f) and !is_format_patch_arg($f)) {
		push @files, $f;
	} else {
		push @rev_list_opts, $f;
	}
}

if (@rev_list_opts) {
	die __("Cannot run git format-patch from outside a repository\n")
		unless $repo;
	require File::Temp;
	push @files, $repo->command('format-patch', '-o', File::Temp::tempdir(CLEANUP => 1),
				    defined $reroll_count ? ('-v', $reroll_count) : (),
				    @rev_list_opts);
}

if (defined $sender) {
	$sender =~ s/^\s+|\s+$//g;
	($sender) = expand_aliases($sender);
} else {
	$sender = $repoauthor->() || $repocommitter->() || '';
}

# $sender could be an already sanitized address
# (e.g. sendemail.from could be manually sanitized by user).
# But it's a no-op to run sanitize_address on an already sanitized address.
$sender = sanitize_address($sender);

$time = time - scalar $#files;

@files = handle_backup_files(@files);

if (@files) {
	unless ($quiet) {
		print $_,"\n" for (@files);
	}
} else {
	print STDERR __("\nNo patch files specified!\n\n");
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
	die sprintf(__("No subject line in %s?"), $fn);
}

if ($compose) {
	# Note that this does not need to be secure, but we will make a small
	# effort to have it be unique
	require File::Temp;
	$compose_filename = ($repo ?
		File::Temp::tempfile(".gitsendemail.msg.XXXXXX", DIR => $repo->repo_path()) :
		File::Temp::tempfile(".gitsendemail.msg.XXXXXX", DIR => "."))[1];
	open my $c, ">", $compose_filename
		or die sprintf(__("Failed to open for writing %s: %s"), $compose_filename, $!);


	my $tpl_sender = $sender || $repoauthor->() || $repocommitter->() || '';
	my $tpl_subject = $initial_subject || '';
	my $tpl_in_reply_to = $initial_in_reply_to || '';
	my $tpl_reply_to = $reply_to || '';
	my $tpl_to = join(',', @initial_to);
	my $tpl_cc = join(',', @initial_cc);
	my $tpl_bcc = join(', ', @initial_bcc);

	print $c <<EOT1, Git::prefix_lines("GIT: ", __(<<EOT2)), <<EOT3;
From $tpl_sender # This line is ignored.
EOT1
Lines beginning in "GIT:" will be removed.
Consider including an overall diffstat or table of contents
for the patch you are writing.

Clear the body content if you don't wish to send a summary.
EOT2
From: $tpl_sender
To: $tpl_to
Cc: $tpl_cc
Bcc: $tpl_bcc
Reply-To: $tpl_reply_to
Subject: $tpl_subject
In-Reply-To: $tpl_in_reply_to

EOT3
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
		or die sprintf(__("Failed to open %s.final: %s"), $compose_filename, $!);

	open $c, "<", $compose_filename
		or die sprintf(__("Failed to open %s: %s"), $compose_filename, $!);

	my $need_8bit_cte = file_has_nonascii($compose_filename);
	my $in_body = 0;
	my $summary_empty = 1;
	if (!defined $compose_encoding) {
		$compose_encoding = "UTF-8";
	}
	while(<$c>) {
		next if m/^GIT:/;
		if ($in_body) {
			$summary_empty = 0 unless (/^\n$/);
		} elsif (/^\n$/) {
			$in_body = 1;
			if ($need_8bit_cte) {
				print $c2 "MIME-Version: 1.0\n",
					 "Content-Type: text/plain; ",
					   "charset=$compose_encoding\n",
					 "Content-Transfer-Encoding: 8bit\n";
			}
		} elsif (/^MIME-Version:/i) {
			$need_8bit_cte = 0;
		} elsif (/^Subject:\s*(.+)\s*$/i) {
			$initial_subject = $1;
			my $subject = $initial_subject;
			$_ = "Subject: " .
				quote_subject($subject, $compose_encoding) .
				"\n";
		} elsif (/^In-Reply-To:\s*(.+)\s*$/i) {
			$initial_in_reply_to = $1;
			next;
		} elsif (/^Reply-To:\s*(.+)\s*$/i) {
			$reply_to = $1;
		} elsif (/^From:\s*(.+)\s*$/i) {
			$sender = $1;
			next;
		} elsif (/^To:\s*(.+)\s*$/i) {
			@initial_to = parse_address_line($1);
			next;
		} elsif (/^Cc:\s*(.+)\s*$/i) {
			@initial_cc = parse_address_line($1);
			next;
		} elsif (/^Bcc:/i) {
			@initial_bcc = parse_address_line($1);
			next;
		}
		print $c2 $_;
	}
	close $c;
	close $c2;

	if ($summary_empty) {
		print __("Summary email is empty, skipping it\n");
		$compose = -1;
	}
} elsif ($annotate) {
	do_edit(@files);
}

{
	# Only instantiate one $term per program run, since some
	# Term::ReadLine providers refuse to create a second instance.
	my $term;
	sub term {
		require Term::ReadLine;
		if (!defined $term) {
			$term = $ENV{"GIT_SEND_EMAIL_NOTTY"}
				? Term::ReadLine->new('git-send-email', \*STDIN, \*STDOUT)
				: Term::ReadLine->new('git-send-email');
		}
		return $term;
	}
}

sub ask {
	my ($prompt, %arg) = @_;
	my $valid_re = $arg{valid_re};
	my $default = $arg{default};
	my $confirm_only = $arg{confirm_only};
	my $resp;
	my $i = 0;
	my $term = term();
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
		if ($confirm_only) {
			my $yesno = $term->readline(
				# TRANSLATORS: please keep [y/N] as is.
				sprintf(__("Are you sure you want to use <%s> [y/N]? "), $resp));
			if (defined $yesno && $yesno =~ /y/i) {
				return $resp;
			}
		}
	}
	return;
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
	print __("The following files are 8bit, but do not declare " .
		 "a Content-Transfer-Encoding.\n");
	foreach my $f (sort keys %broken_encoding) {
		print "    $f\n";
	}
	$auto_8bit_encoding = ask(__("Which 8bit encoding should I declare [UTF-8]? "),
				  valid_re => qr/.{4}/, confirm_only => 1,
				  default => "UTF-8");
}

if (!$force) {
	for my $f (@files) {
		if (get_patch_subject($f) =~ /\Q*** SUBJECT HERE ***\E/) {
			die sprintf(__("Refusing to send because the patch\n\t%s\n"
				. "has the template subject '*** SUBJECT HERE ***'. "
				. "Pass --force if you really want to send.\n"), $f);
		}
	}
}

my $to_whom = __("To whom should the emails be sent (if anyone)?");
my $prompting = 0;
if (!@initial_to && !defined $to_cmd) {
	my $to = ask("$to_whom ",
		     default => "",
		     valid_re => qr/\@.*\./, confirm_only => 1);
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
		die sprintf(__("fatal: alias '%s' expands to itself\n"), $alias);
	}
	local $EXPANDED_ALIASES{$alias} = 1;
	return $aliases{$alias} ? expand_aliases(@{$aliases{$alias}}) : $alias;
}

@initial_to = process_address_list(@initial_to);
@initial_cc = process_address_list(@initial_cc);
@initial_bcc = process_address_list(@initial_bcc);

if ($thread && !defined $initial_in_reply_to && $prompting) {
	$initial_in_reply_to = ask(
		__("Message-ID to be used as In-Reply-To for the first email (if any)? "),
		default => "",
		valid_re => qr/\@.*\./, confirm_only => 1);
}
if (defined $initial_in_reply_to) {
	$initial_in_reply_to =~ s/^\s*<?//;
	$initial_in_reply_to =~ s/>?\s*$//;
	$initial_in_reply_to = "<$initial_in_reply_to>" if $initial_in_reply_to ne '';
}

if (defined $reply_to) {
	$reply_to =~ s/^\s+|\s+$//g;
	($reply_to) = expand_aliases($reply_to);
	$reply_to = sanitize_address($reply_to);
}

if (!defined $sendmail_cmd && !defined $smtp_server) {
	my @sendmail_paths = qw( /usr/sbin/sendmail /usr/lib/sendmail );
	push @sendmail_paths, map {"$_/sendmail"} split /:/, $ENV{PATH};
	foreach (@sendmail_paths) {
		if (-x $_) {
			$sendmail_cmd = $_;
			last;
		}
	}

	if (!defined $sendmail_cmd) {
		$smtp_server = 'localhost'; # could be 127.0.0.1, too... *shrug*
	}
}

if ($compose && $compose > 0) {
	@files = ($compose_filename . ".final", @files);
}

# Variables we set as part of the loop over files
our ($message_id, %mail, $subject, $in_reply_to, $references, $message,
	$needs_confirm, $message_num, $ask_default);

sub mailmap_address_list {
	return @_ unless @_ and $mailmap;
	my @options = ();
	push(@options, "--mailmap-file=$mailmap_file") if $mailmap_file;
	push(@options, "--mailmap-blob=$mailmap_blob") if $mailmap_blob;
	my @addr_list = Git::command('check-mailmap', @options, @_);
	s/^<(.*)>$/$1/ for @addr_list;
	return @addr_list;
}

sub extract_valid_address {
	my $address = shift;
	my $local_part_regexp = qr/[^<>"\s@]+/;
	my $domain_regexp = qr/[^.<>"\s@]+(?:\.[^.<>"\s@]+)+/;

	# check for a local address:
	return $address if ($address =~ /^($local_part_regexp)$/);

	$address =~ s/^\s*<(.*)>\s*$/$1/;
	my $have_email_valid = eval { require Email::Valid; 1 };
	if ($have_email_valid) {
		return scalar Email::Valid->address($address);
	}

	# less robust/correct than the monster regexp in Email::Valid,
	# but still does a 99% job, and one less dependency
	return $1 if $address =~ /($local_part_regexp\@$domain_regexp)/;
	return;
}

sub extract_valid_address_or_die {
	my $address = shift;
	my $valid_address = extract_valid_address($address);
	die sprintf(__("error: unable to extract a valid address from: %s\n"), $address)
		if !$valid_address;
	return $valid_address;
}

sub validate_address {
	my $address = shift;
	while (!extract_valid_address($address)) {
		printf STDERR __("error: unable to extract a valid address from: %s\n"), $address;
		# TRANSLATORS: Make sure to include [q] [d] [e] in your
		# translation. The program will only accept English input
		# at this point.
		$_ = ask(__("What to do with this address? ([q]uit|[d]rop|[e]dit): "),
			valid_re => qr/^(?:quit|q|drop|d|edit|e)/i,
			default => 'q');
		if (/^d/i) {
			return undef;
		} elsif (/^q/i) {
			cleanup_compose_files();
			exit(0);
		}
		$address = ask("$to_whom ",
			default => "",
			valid_re => qr/\@.*\./, confirm_only => 1);
	}
	return $address;
}

sub validate_address_list {
	return (grep { defined $_ }
		map { validate_address($_) } @_);
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
		require POSIX;
		$message_id_stamp = POSIX::strftime("%Y%m%d%H%M%S.$$", gmtime(time));
		$message_id_serial = 0;
	}
	$message_id_serial++;
	$uniq = "$message_id_stamp-$message_id_serial";

	my $du_part;
	for ($sender, $repocommitter->(), $repoauthor->()) {
		$du_part = extract_valid_address(sanitize_address($_));
		last if (defined $du_part and $du_part ne '');
	}
	if (not defined $du_part or $du_part eq '') {
		require Sys::Hostname;
		$du_part = 'user@' . Sys::Hostname::hostname();
	}
	my $message_id_template = "<%s-%s>";
	$message_id = sprintf($message_id_template, $uniq, $du_part);
	#print "new message id = $message_id\n"; # Was useful for debugging
}

sub unquote_rfc2047 {
	local ($_) = @_;
	my $charset;
	my $sep = qr/[ \t]+/;
	s{$re_encoded_word(?:$sep$re_encoded_word)*}{
		my @words = split $sep, $&;
		foreach (@words) {
			m/$re_encoded_word/;
			$charset = $1;
			my $encoding = $2;
			my $text = $3;
			if ($encoding eq 'q' || $encoding eq 'Q') {
				$_ = $text;
				s/_/ /g;
				s/=([0-9A-F]{2})/chr(hex($1))/egi;
			} else {
				# other encodings not supported yet
			}
		}
		join '', @words;
	}eg;
	return wantarray ? ($_, $charset) : $_;
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
	length($s) <= 75 &&
	$s =~ m/^(?:"[[:ascii:]]*"|$re_encoded_word)$/o;
}

sub subject_needs_rfc2047_quoting {
	my $s = shift;

	return ($s =~ /[^[:ascii:]]/) || ($s =~ /=\?/);
}

sub quote_subject {
	local $subject = shift;
	my $encoding = shift || 'UTF-8';

	if (subject_needs_rfc2047_quoting($subject)) {
		return quote_rfc2047($subject, $encoding);
	}
	return $subject;
}

# use the simplest quoting being able to handle the recipient
sub sanitize_address {
	my ($recipient) = @_;

	# remove garbage after email address
	$recipient =~ s/(.*>).*$/$1/;

	my ($recipient_name, $recipient_addr) = ($recipient =~ /^(.*?)\s*(<.*)/);

	if (not $recipient_name) {
		return $recipient;
	}

	# if recipient_name is already quoted, do nothing
	if (is_rfc2047_quoted($recipient_name)) {
		return $recipient;
	}

	# remove non-escaped quotes
	$recipient_name =~ s/(^|[^\\])"/$1/g;

	# rfc2047 is needed if a non-ascii char is included
	if ($recipient_name =~ /[^[:ascii:]]/) {
		$recipient_name = quote_rfc2047($recipient_name);
	}

	# double quotes are needed if specials or CTLs are included
	elsif ($recipient_name =~ /[][()<>@,;:\\".\000-\037\177]/) {
		$recipient_name =~ s/([\\\r])/\\$1/g;
		$recipient_name = qq["$recipient_name"];
	}

	return "$recipient_name $recipient_addr";

}

sub strip_garbage_one_address {
	my ($addr) = @_;
	chomp $addr;
	if ($addr =~ /^(("[^"]*"|[^"<]*)? *<[^>]*>).*/) {
		# "Foo Bar" <foobar@example.com> [possibly garbage here]
		# Foo Bar <foobar@example.com> [possibly garbage here]
		return $1;
	}
	if ($addr =~ /^(<[^>]*>).*/) {
		# <foo@example.com> [possibly garbage here]
		# if garbage contains other addresses, they are ignored.
		return $1;
	}
	if ($addr =~ /^([^"#,\s]*)/) {
		# address without quoting: remove anything after the address
		return $1;
	}
	return $addr;
}

sub sanitize_address_list {
	return (map { sanitize_address($_) } @_);
}

sub process_address_list {
	my @addr_list = map { parse_address_line($_) } @_;
	@addr_list = expand_aliases(@addr_list);
	@addr_list = sanitize_address_list(@addr_list);
	@addr_list = validate_address_list(@addr_list);
	@addr_list = mailmap_address_list(@addr_list);
	return @addr_list;
}

# Returns the local Fully Qualified Domain Name (FQDN) if available.
#
# Tightly configured MTAa require that a caller sends a real DNS
# domain name that corresponds the IP address in the HELO/EHLO
# handshake. This is used to verify the connection and prevent
# spammers from trying to hide their identity. If the DNS and IP don't
# match, the receiving MTA may deny the connection.
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
	my $subdomain = '(?!-)[A-Za-z0-9-]{1,63}(?<!-)';
	return defined $domain && !($^O eq 'darwin' && $domain =~ /\.local$/)
		&& $domain  =~ /^$subdomain(?:\.$subdomain)*$/;
}

sub maildomain_net {
	my $maildomain;

	require Net::Domain;
	my $domain = Net::Domain::domainname();
	$maildomain = $domain if valid_fqdn($domain);

	return $maildomain;
}

sub maildomain_mta {
	my $maildomain;

	for my $host (qw(mailhost localhost)) {
		require Net::SMTP;
		my $smtp = Net::SMTP->new($host);
		if (defined $smtp) {
			my $domain = $smtp->domain;
			$smtp->quit;

			$maildomain = $domain if valid_fqdn($domain);

			last if $maildomain;
		}
	}

	return $maildomain;
}

sub maildomain_hostname_command {
	my $maildomain;

	if ($^O eq 'linux' || $^O eq 'darwin') {
		my $domain = `(hostname -f) 2>/dev/null`;
		if (!$?) {
			chomp($domain);
			$maildomain = $domain if valid_fqdn($domain);
		}
	}
	return $maildomain;
}

sub maildomain {
	return maildomain_net() || maildomain_mta() ||
		maildomain_hostname_command || 'localhost.localdomain';
}

sub smtp_host_string {
	if (defined $smtp_server_port) {
		return "$smtp_server:$smtp_server_port";
	} else {
		return $smtp_server;
	}
}

# Returns 1 if authentication succeeded or was not necessary
# (smtp_user was not specified), and 0 otherwise.

sub smtp_auth_maybe {
	if (!defined $smtp_authuser || $auth || (defined $smtp_auth && $smtp_auth eq "none")) {
		return 1;
	}

	# Workaround AUTH PLAIN/LOGIN interaction defect
	# with Authen::SASL::Cyrus
	eval {
		require Authen::SASL;
		Authen::SASL->import(qw(Perl));
	};

	# Check mechanism naming as defined in:
	# https://tools.ietf.org/html/rfc4422#page-8
	if ($smtp_auth && $smtp_auth !~ /^(\b[A-Z0-9-_]{1,20}\s*)*$/) {
		die "invalid smtp auth: '${smtp_auth}'";
	}

	# Authentication may fail not because credentials were
	# invalid but due to other reasons, in which we should not
	# reject credentials.
	$auth = Git::credential({
		'protocol' => 'smtp',
		'host' => smtp_host_string(),
		'username' => $smtp_authuser,
		# if there's no password, "git credential fill" will
		# give us one, otherwise it'll just pass this one.
		'password' => $smtp_authpass
	}, sub {
		my $cred = shift;
		my $result;
		my $error;

		# catch all SMTP auth error in a unified eval block
		eval {
			if ($smtp_auth) {
				my $sasl = Authen::SASL->new(
					mechanism => $smtp_auth,
					callback => {
						user     => $cred->{'username'},
						pass     => $cred->{'password'},
						authname => $cred->{'username'},
					}
				);
				$result = $smtp->auth($sasl);
			} else {
				$result = $smtp->auth($cred->{'username'}, $cred->{'password'});
			}
			1; # ensure true value is returned if no exception is thrown
		} or do {
			$error = $@ || 'Unknown error';
		};

		return ($error
			? handle_smtp_error($error)
			: ($result ? 1 : 0));
	});

	return $auth;
}

sub handle_smtp_error {
	my ($error) = @_;

	# Parse SMTP status code from error message in:
	# https://www.rfc-editor.org/rfc/rfc5321.html
	if ($error =~ /\b(\d{3})\b/) {
		my $status_code = $1;
		if ($status_code =~ /^4/) {
			# 4yz: Transient Negative Completion reply
			warn "SMTP transient error (status code $status_code): $error";
			return 1;
		} elsif ($status_code =~ /^5/) {
			# 5yz: Permanent Negative Completion reply
			warn "SMTP permanent error (status code $status_code): $error";
			return 0;
		}
		# If no recognized status code is found, treat as transient error
		warn "SMTP unknown error: $error. Treating as transient failure.";
		return 1;
	}

	# If no status code is found, treat as transient error
	warn "SMTP generic error: $error";
	return 1;
}

sub ssl_verify_params {
	eval {
		require IO::Socket::SSL;
		IO::Socket::SSL->import(qw/SSL_VERIFY_PEER SSL_VERIFY_NONE/);
	};
	if ($@) {
		print STDERR "Not using SSL_VERIFY_PEER due to out-of-date IO::Socket::SSL.\n";
		return;
	}

	if (!defined $smtp_ssl_cert_path) {
		# use the OpenSSL defaults
		return (SSL_verify_mode => SSL_VERIFY_PEER());
	}

	if ($smtp_ssl_cert_path eq "") {
		return (SSL_verify_mode => SSL_VERIFY_NONE());
	} elsif (-d $smtp_ssl_cert_path) {
		return (SSL_verify_mode => SSL_VERIFY_PEER(),
			SSL_ca_path => $smtp_ssl_cert_path);
	} elsif (-f $smtp_ssl_cert_path) {
		return (SSL_verify_mode => SSL_VERIFY_PEER(),
			SSL_ca_file => $smtp_ssl_cert_path);
	} else {
		die sprintf(__("CA path \"%s\" does not exist"), $smtp_ssl_cert_path);
	}
}

sub file_name_is_absolute {
	my ($path) = @_;

	# msys does not grok DOS drive-prefixes
	if ($^O eq 'msys') {
		return ($path =~ m#^/# || $path =~ m#^[a-zA-Z]\:#)
	}

	require File::Spec::Functions;
	return File::Spec::Functions::file_name_is_absolute($path);
}

sub gen_header {
	my @recipients = unique_email_list(@to);
	@cc = (grep { my $cc = extract_valid_address_or_die($_);
		      not grep { $cc eq $_ || $_ =~ /<\Q${cc}\E>$/ } @recipients
		    }
	       @cc);
	my $to = join (",\n\t", @recipients);
	@recipients = unique_email_list(@recipients,@cc,@initial_bcc);
	@recipients = (map { extract_valid_address_or_die($_) } @recipients);
	my $date = format_2822_time($time++);
	my $gitversion = '@GIT_VERSION@';
	if ($gitversion =~ m/..GIT_VERSION../) {
	    $gitversion = Git::version();
	}

	my $cc = join(",\n\t", unique_email_list(@cc));
	my $ccline = "";
	if ($cc ne '') {
		$ccline = "\nCc: $cc";
	}
	make_message_id() unless defined($message_id);

	my $header = "From: $sender
To: $to${ccline}
Subject: $subject
Date: $date
Message-ID: $message_id
";
	if ($use_xmailer) {
		$header .= "X-Mailer: git-send-email $gitversion\n";
	}
	if ($in_reply_to) {

		$header .= "In-Reply-To: $in_reply_to\n";
		$header .= "References: $references\n";
	}
	if ($reply_to) {
		$header .= "Reply-To: $reply_to\n";
	}
	if (@xh) {
		$header .= join("\n", @xh) . "\n";
	}
	my $recipients_ref = \@recipients;
	return ($recipients_ref, $to, $date, $gitversion, $cc, $ccline, $header);
}

sub is_outlook {
	my ($host) = @_;
	if ($outlook_id_fix eq 'auto') {
		$outlook_id_fix =
			($host eq 'smtp.office365.com' ||
			 $host eq 'smtp-mail.outlook.com') ? 1 : 0;
	}
	return $outlook_id_fix;
}

# Prepares the email, then asks the user what to do.
#
# If the user chooses to send the email, it's sent and 1 is returned.
# If the user chooses not to send the email, 0 is returned.
# If the user decides they want to make further edits, -1 is returned and the
# caller is expected to call send_message again after the edits are performed.
#
# If an error occurs sending the email, this just dies.

sub send_message {
	my ($recipients_ref, $to, $date, $gitversion, $cc, $ccline, $header) = gen_header();
	my @recipients = @$recipients_ref;

	my @sendmail_parameters = ('-i', @recipients);
	my $raw_from = $sender;
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
			print __ <<EOF ;
    The Cc list above has been expanded by additional
    addresses found in the patch commit message. By default
    send-email prompts before sending whenever this occurs.
    This behavior is controlled by the sendemail.confirm
    configuration setting.

    For additional information, run 'git send-email --help'.
    To retain the current behavior, but squelch this message,
    run 'git config --global sendemail.confirm auto'.

EOF
		}
		# TRANSLATORS: Make sure to include [y] [n] [e] [q] [a] in your
		# translation. The program will only accept English input
		# at this point.
		$_ = ask(__("Send this email? ([y]es|[n]o|[e]dit|[q]uit|[a]ll): "),
		         valid_re => qr/^(?:yes|y|no|n|edit|e|quit|q|all|a)/i,
		         default => $ask_default);
		die __("Send this email reply required") unless defined $_;
		if (/^n/i) {
			# If we are skipping a message, we should make sure that
			# the next message is treated as the successor to the
			# previously sent message, and not the skipped message.
			$message_num--;
			return 0;
		} elsif (/^e/i) {
			# Since the same message will be sent again, we need to
			# decrement the message number to the previous message.
			# Otherwise, the edited message will be treated as a
			# different message sent after the original non-edited
			# message.
			$message_num--;
			return -1;
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
	} elsif (defined $sendmail_cmd || file_name_is_absolute($smtp_server)) {
		my $pid = open my $sm, '|-';
		defined $pid or die $!;
		if (!$pid) {
			if (defined $sendmail_cmd) {
				exec ("sh", "-c", "$sendmail_cmd \"\$@\"", "-", @sendmail_parameters)
					or die $!;
			} else {
				exec ($smtp_server, @sendmail_parameters)
					or die $!;
			}
		}
		print $sm "$header\n$message";
		close $sm or die $!;
	} else {

		if (!defined $smtp_server) {
			die __("The required SMTP server is not properly defined.")
		}

		require Net::SMTP;
		my $use_net_smtp_ssl = version->parse($Net::SMTP::VERSION) < version->parse("2.34");
		$smtp_domain ||= maildomain();

		if ($smtp_encryption eq 'ssl') {
			$smtp_server_port ||= 465; # ssmtp
			require IO::Socket::SSL;

			# Suppress "variable accessed once" warning.
			{
				no warnings 'once';
				$IO::Socket::SSL::DEBUG = 1;
			}

			# Net::SMTP::SSL->new() does not forward any SSL options
			IO::Socket::SSL::set_client_defaults(
				ssl_verify_params());

			if ($use_net_smtp_ssl) {
				require Net::SMTP::SSL;
				$smtp ||= Net::SMTP::SSL->new($smtp_server,
							      Hello => $smtp_domain,
							      Port => $smtp_server_port,
							      Debug => $debug_net_smtp);
			}
			else {
				$smtp ||= Net::SMTP->new($smtp_server,
							 Hello => $smtp_domain,
							 Port => $smtp_server_port,
							 Debug => $debug_net_smtp,
							 SSL => 1);
			}
		}
		elsif (!$smtp) {
			$smtp_server_port ||= 25;
			$smtp ||= Net::SMTP->new($smtp_server,
						 Hello => $smtp_domain,
						 Debug => $debug_net_smtp,
						 Port => $smtp_server_port);
			if ($smtp_encryption eq 'tls' && $smtp) {
				if ($use_net_smtp_ssl) {
					$smtp->command('STARTTLS');
					$smtp->response();
					if ($smtp->code != 220) {
						die sprintf(__("Server does not support STARTTLS! %s"), $smtp->message);
					}
					require Net::SMTP::SSL;
					$smtp = Net::SMTP::SSL->start_SSL($smtp,
									  ssl_verify_params())
						or die sprintf(__("STARTTLS failed! %s"), IO::Socket::SSL::errstr());
				}
				else {
					$smtp->starttls(ssl_verify_params())
						or die sprintf(__("STARTTLS failed! %s"), IO::Socket::SSL::errstr());
				}
				# Send EHLO again to receive fresh
				# supported commands
				$smtp->hello($smtp_domain);
			}
		}

		if (!$smtp) {
			die __("Unable to initialize SMTP properly. Check config and use --smtp-debug."),
			    " VALUES: server=$smtp_server ",
			    "encryption=$smtp_encryption ",
			    "hello=$smtp_domain",
			    defined $smtp_server_port ? " port=$smtp_server_port" : "";
		}

		smtp_auth_maybe or die $smtp->message;

		$smtp->mail( $raw_from ) or die $smtp->message;
		$smtp->to( @recipients ) or die $smtp->message;
		$smtp->data or die $smtp->message;
		$smtp->datasend("$header\n") or die $smtp->message;
		my @lines = split /^/, $message;
		foreach my $line (@lines) {
			$smtp->datasend("$line") or die $smtp->message;
		}
		$smtp->dataend() or die $smtp->message;

		# Outlook discards the Message-ID header we set while sending the email
		# and generates a new random Message-ID. So in order to avoid breaking
		# threads, we simply retrieve the Message-ID from the server response
		# and assign it to the $message_id variable, which will then be
		# assigned to $in_reply_to by the caller when the next message is sent
		# as a response to this message.
		if (is_outlook($smtp_server)) {
			if ($smtp->message =~ /<([^>]+)>/) {
				$message_id = "<$1>";
				$header =~ s/^(Message-ID:\s*).*\n/${1}$message_id\n/m;
				printf __("Outlook reassigned Message-ID to: %s\n"), $message_id if $smtp->debug;
			} else {
				warn __("Warning: Could not retrieve Message-ID from server response.\n");
			}
		}

		$smtp->code =~ /250|200/ or die sprintf(__("Failed to send %s\n"), $subject).$smtp->message;
	}
	if ($quiet) {
		printf($dry_run ? __("Dry-Sent %s") : __("Sent %s"), $subject);
		print "\n";
	} else {
		print($dry_run ? __("Dry-OK. Log says:") : __("OK. Log says:"));
		print "\n";
		if (!defined $sendmail_cmd && !file_name_is_absolute($smtp_server)) {
			print "Server: $smtp_server\n";
			print "MAIL FROM:<$raw_from>\n";
			foreach my $entry (@recipients) {
			    print "RCPT TO:<$entry>\n";
			}
		} else {
			my $sm;
			if (defined $sendmail_cmd) {
				$sm = $sendmail_cmd;
			} else {
				$sm = $smtp_server;
			}

			print "Sendmail: $sm ".join(' ',@sendmail_parameters)."\n";
		}
		print $header, "\n";
		if ($smtp) {
			print __("Result: "), $smtp->code, ' ',
				($smtp->message =~ /\n([^\n]+\n)$/s);
		} else {
			print __("Result: OK");
		}
		print "\n";
	}

	return 1;
}

sub pre_process_file {
	my ($t, $quiet) = @_;

	open my $fh, "<", $t or die sprintf(__("can't open file %s"), $t);

	my $author = undef;
	my $sauthor = undef;
	my $author_encoding;
	my $has_content_type;
	my $body_encoding;
	my $xfer_encoding;
	my $has_mime_version;
	@to = ();
	@cc = ();
	@xh = ();
	my $input_format = undef;
	my @header = ();
	$subject = $initial_subject;
	$message = "";
	$message_num++;
	undef $message_id;
	# Retrieve and unfold header fields.
	my @header_lines = ();
	while(<$fh>) {
		last if /^\s*$/;
		push(@header_lines, $_);
	}
	@header = unfold_headers(@header_lines);
	# Add computed headers, if applicable.
	unless ($no_header_cmd || ! $header_cmd) {
		push @header, invoke_header_cmd($header_cmd, $t);
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
			if (/^Subject:\s+(.*)$/i) {
				$subject = $1;
			}
			elsif (/^From:\s+(.*)$/i) {
				($author, $author_encoding) = unquote_rfc2047($1);
				$sauthor = sanitize_address($author);
				next if $suppress_cc{'author'};
				next if $suppress_cc{'self'} and $sauthor eq $sender;
				printf(__("(mbox) Adding cc: %s from line '%s'\n"),
					$1, $_) unless $quiet;
				push @cc, $1;
			}
			elsif (/^To:\s+(.*)$/i) {
				foreach my $addr (parse_address_line($1)) {
					printf(__("(mbox) Adding to: %s from line '%s'\n"),
						$addr, $_) unless $quiet;
					push @to, $addr;
				}
			}
			elsif (/^Cc:\s+(.*)$/i) {
				foreach my $addr (parse_address_line($1)) {
					my $qaddr = unquote_rfc2047($addr);
					my $saddr = sanitize_address($qaddr);
					if ($saddr eq $sender) {
						next if ($suppress_cc{'self'});
					} else {
						next if ($suppress_cc{'cc'});
					}
					printf(__("(mbox) Adding cc: %s from line '%s'\n"),
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
			elsif (/^MIME-Version/i) {
				$has_mime_version = 1;
				push @xh, $_;
			}
			elsif (/^Message-ID: (.*)/i) {
				$message_id = $1;
			}
			elsif (/^Content-Transfer-Encoding: (.*)/i) {
				$xfer_encoding = $1 if not defined $xfer_encoding;
			}
			elsif (/^In-Reply-To: (.*)/i) {
				if (!$initial_in_reply_to || $thread) {
					$in_reply_to = $1;
				}
			}
			elsif (/^References: (.*)/i) {
				if (!$initial_in_reply_to || $thread) {
					$references = $1;
				}
			}
			elsif (!/^Date:\s/i && /^[-A-Za-z]+:\s+\S/) {
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
				printf(__("(non-mbox) Adding cc: %s from line '%s'\n"),
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
		if (/^([a-z][a-z-]*-by|Cc): (.*)/i) {
			chomp;
			my ($what, $c) = ($1, $2);
			# strip garbage for the address we'll use:
			$c = strip_garbage_one_address($c);
			# sanitize a bit more to decide whether to suppress the address:
			my $sc = sanitize_address($c);
			if ($sc eq $sender) {
				next if ($suppress_cc{'self'});
			} else {
				if ($what =~ /^Signed-off-by$/i) {
					next if $suppress_cc{'sob'};
				} elsif ($what =~ /-by$/i) {
					next if $suppress_cc{'misc-by'};
				} elsif ($what =~ /Cc/i) {
					next if $suppress_cc{'bodycc'};
				}
			}
			if ($c !~ /.+@.+|<.+>/) {
				printf("(body) Ignoring %s from line '%s'\n",
					$what, $_) unless $quiet;
				next;
			}
			push @cc, $sc;
			printf(__("(body) Adding cc: %s from line '%s'\n"),
				$sc, $_) unless $quiet;
		}
	}
	close $fh;

	push @to, recipients_cmd("to-cmd", "to", $to_cmd, $t, $quiet)
		if defined $to_cmd;
	push @cc, recipients_cmd("cc-cmd", "cc", $cc_cmd, $t, $quiet)
		if defined $cc_cmd && !$suppress_cc{'cccmd'};

	if ($broken_encoding{$t} && !$has_content_type) {
		$xfer_encoding = '8bit' if not defined $xfer_encoding;
		$has_content_type = 1;
		push @xh, "Content-Type: text/plain; charset=$auto_8bit_encoding";
		$body_encoding = $auto_8bit_encoding;
	}

	if ($broken_encoding{$t} && !is_rfc2047_quoted($subject)) {
		$subject = quote_subject($subject, $auto_8bit_encoding);
	}

	if (defined $sauthor and $sauthor ne $sender) {
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
				$xfer_encoding = '8bit' if not defined $xfer_encoding;
				$has_content_type = 1;
				push @xh,
				  "Content-Type: text/plain; charset=$author_encoding";
			}
		}
	}
	$xfer_encoding = '8bit' if not defined $xfer_encoding;
	($message, $xfer_encoding) = apply_transfer_encoding(
		$message, $xfer_encoding, $target_xfer_encoding);
	push @xh, "Content-Transfer-Encoding: $xfer_encoding";
	unshift @xh, 'MIME-Version: 1.0' unless $has_mime_version;

	$needs_confirm = (
		$confirm eq "always" or
		($confirm =~ /^(?:auto|cc)$/ && @cc) or
		($confirm =~ /^(?:auto|compose)$/ && $compose && $message_num == 1));
	$needs_confirm = "inform" if ($needs_confirm && $confirm_unconfigured && @cc);

	@to = process_address_list(@to);
	@cc = process_address_list(@cc);

	@to = (@initial_to, @to);
	@cc = (@initial_cc, @cc);

	if ($message_num == 1) {
		if (defined $cover_cc and $cover_cc) {
			@initial_cc = @cc;
		}
		if (defined $cover_to and $cover_to) {
			@initial_to = @to;
		}
	}
}

# Prepares the email, prompts the user, and sends it out
# Returns 0 if an edit was done and the function should be called again, or 1
# on the email being successfully sent out.
sub process_file {
	my ($t) = @_;

        pre_process_file($t, $quiet);

	my $message_was_sent = send_message();
	if ($message_was_sent == -1) {
		do_edit($t);
		return 0;
	}

	# set up for the next message
	if ($thread) {
		if ($message_was_sent &&
		  ($chain_reply_to || !defined $in_reply_to || length($in_reply_to) == 0 ||
		  $message_num == 1)) {
			$in_reply_to = $message_id;
			if (length $references > 0) {
				$references .= "\n $message_id";
			} else {
				$references = "$message_id";
			}
		}
	} elsif (!defined $initial_in_reply_to) {
		# --thread and --in-reply-to manage the "In-Reply-To" header and by
		# extension the "References" header. If these commands are not used, reset
		# the header values to their defaults.
		$in_reply_to = undef;
		$references = '';
	}
	$message_id = undef;
	$num_sent++;
	if (defined $batch_size && $num_sent == $batch_size) {
		$num_sent = 0;
		$smtp->quit if defined $smtp;
		undef $smtp;
		undef $auth;
		sleep($relogin_delay) if defined $relogin_delay;
	}

	return 1;
}

sub initialize_modified_loop_vars {
	$in_reply_to = $initial_in_reply_to;
	$references = $initial_in_reply_to || '';
	$message_num = 0;
}

if ($validate) {
	# FIFOs can only be read once, exclude them from validation.
	my @real_files = ();
	foreach my $f (@files) {
		unless (-p $f) {
			push(@real_files, $f);
		}
	}

	# Run the loop once again to avoid gaps in the counter due to FIFO
	# arguments provided by the user.
	my $num = 1;
	my $num_files = scalar @real_files;
	$ENV{GIT_SENDEMAIL_FILE_TOTAL} = "$num_files";
	initialize_modified_loop_vars();
	foreach my $r (@real_files) {
		$ENV{GIT_SENDEMAIL_FILE_COUNTER} = "$num";
		pre_process_file($r, 1);
		validate_patch($r, $target_xfer_encoding);
		$num += 1;
	}
	delete $ENV{GIT_SENDEMAIL_FILE_COUNTER};
	delete $ENV{GIT_SENDEMAIL_FILE_TOTAL};
}

initialize_modified_loop_vars();
foreach my $t (@files) {
	while (!process_file($t)) {
		# user edited the file
	}
}

# Execute a command and return its output lines as an array.  Blank
# lines which do not appear at the end of the output are reported as
# errors.
sub execute_cmd {
	my ($prefix, $cmd, $file) = @_;
	my @lines = ();
	my $seen_blank_line = 0;
	open my $fh, "-|", "$cmd \Q$file\E"
		or die sprintf(__("(%s) Could not execute '%s'"), $prefix, $cmd);
	while (my $line = <$fh>) {
		die sprintf(__("(%s) Malformed output from '%s'"), $prefix, $cmd)
		    if $seen_blank_line;
		if ($line =~ /^$/) {
			$seen_blank_line = $line =~ /^$/;
			next;
		}
		push @lines, $line;
	}
	close $fh
	    or die sprintf(__("(%s) failed to close pipe to '%s'"), $prefix, $cmd);
	return @lines;
}

# Process headers lines, unfolding multiline headers as defined by RFC
# 2822.
sub unfold_headers {
	my @headers;
	foreach(@_) {
		last if /^\s*$/;
		if (/^\s+\S/ and @headers) {
			chomp($headers[$#headers]);
			s/^\s+/ /;
			$headers[$#headers] .= $_;
		} else {
			push(@headers, $_);
		}
	}
	return @headers;
}

# Invoke the provided CMD with FILE as an argument, which should
# output RFC 2822 email headers. Fold multiline headers and return the
# headers as an array.
sub invoke_header_cmd {
	my ($cmd, $file) = @_;
	my @lines = execute_cmd("header-cmd", $header_cmd, $file);
	return unfold_headers(@lines);
}

# Execute a command (e.g. $to_cmd) to get a list of email addresses
# and return a results array
sub recipients_cmd {
	my ($prefix, $what, $cmd, $file, $quiet) = @_;
	my @lines = ();
	my @addresses = ();

	@lines = execute_cmd($prefix, $cmd, $file);
	for my $address (@lines) {
		$address =~ s/^\s*//g;
		$address =~ s/\s*$//g;
		$address = sanitize_address($address);
		next if ($address eq $sender and $suppress_cc{'self'});
		push @addresses, $address;
		printf(__("(%s) Adding %s: %s from: '%s'\n"),
		       $prefix, $what, $address, $cmd) unless $quiet;
		}
	return @addresses;
}

cleanup_compose_files();

sub cleanup_compose_files {
	unlink($compose_filename, $compose_filename . ".final") if $compose;
}

$smtp->quit if $smtp;

sub apply_transfer_encoding {
	my $message = shift;
	my $from = shift;
	my $to = shift;

	return ($message, $to) if ($from eq $to and $from ne '7bit');

	require MIME::QuotedPrint;
	require MIME::Base64;

	$message = MIME::QuotedPrint::decode($message)
		if ($from eq 'quoted-printable');
	$message = MIME::Base64::decode($message)
		if ($from eq 'base64');

	$to = ($message =~ /(?:.{999,}|\r)/) ? 'quoted-printable' : '8bit'
		if $to eq 'auto';

	die __("cannot send message as 7bit")
		if ($to eq '7bit' and $message =~ /[^[:ascii:]]/);
	return ($message, $to)
		if ($to eq '7bit' or $to eq '8bit');
	return (MIME::QuotedPrint::encode($message, "\n", 0), $to)
		if ($to eq 'quoted-printable');
	return (MIME::Base64::encode($message, "\n"), $to)
		if ($to eq 'base64');
	die __("invalid transfer encoding");
}

sub unique_email_list {
	my %seen;
	my @emails;

	foreach my $entry (@_) {
		my $clean = extract_valid_address_or_die($entry);
		$seen{$clean} ||= 0;
		next if $seen{$clean}++;
		push @emails, $entry;
	}
	return @emails;
}

sub validate_patch {
	my ($fn, $xfer_encoding) = @_;

	if ($repo) {
		my $hook_name = 'sendemail-validate';
		my $hooks_path = $repo->command_oneline('rev-parse', '--git-path', 'hooks');
		require File::Spec;
		my $validate_hook = File::Spec->catfile($hooks_path, $hook_name);
		my $hook_error;
		if (-x $validate_hook) {
			require Cwd;
			my $target = Cwd::abs_path($fn);
			# The hook needs a correct cwd and GIT_DIR.
			my $cwd_save = Cwd::getcwd();
			chdir($repo->wc_path() or $repo->repo_path())
				or die("chdir: $!");
			local $ENV{"GIT_DIR"} = $repo->repo_path();

			my ($recipients_ref, $to, $date, $gitversion, $cc, $ccline, $header) = gen_header();

			require File::Temp;
			my ($header_filehandle, $header_filename) = File::Temp::tempfile(
                            TEMPLATE => ".gitsendemail.header.XXXXXX",
                            DIR => $repo->repo_path(),
                            UNLINK => 1,
                        );
			print $header_filehandle $header;

			my @cmd = ("git", "hook", "run", "--ignore-missing",
				    $hook_name, "--");
			my @cmd_msg = (@cmd, "<patch>", "<header>");
			my @cmd_run = (@cmd, $target, $header_filename);
			$hook_error = system_or_msg(\@cmd_run, undef, "@cmd_msg");
			chdir($cwd_save) or die("chdir: $!");
		}
		if ($hook_error) {
			$hook_error = sprintf(
			    __("fatal: %s: rejected by %s hook\n%s\nwarning: no patches were sent\n"),
			    $fn, $hook_name, $hook_error);
			die $hook_error;
		}
	}

	# Any long lines will be automatically fixed if we use a suitable transfer
	# encoding.
	unless ($xfer_encoding =~ /^(?:auto|quoted-printable|base64)$/) {
		open(my $fh, '<', $fn)
			or die sprintf(__("unable to open %s: %s\n"), $fn, $!);
		while (my $line = <$fh>) {
			if (length($line) > 998) {
				die sprintf(__("fatal: %s:%d is longer than 998 characters\n" .
					       "warning: no patches were sent\n"), $fn, $.);
			}
		}
	}
	return;
}

sub handle_backup {
	my ($last, $lastlen, $file, $known_suffix) = @_;
	my ($suffix, $skip);

	$skip = 0;
	if (defined $last &&
	    ($lastlen < length($file)) &&
	    (substr($file, 0, $lastlen) eq $last) &&
	    ($suffix = substr($file, $lastlen)) !~ /^[a-z0-9]/i) {
		if (defined $known_suffix && $suffix eq $known_suffix) {
			printf(__("Skipping %s with backup suffix '%s'.\n"), $file, $known_suffix);
			$skip = 1;
		} else {
			# TRANSLATORS: please keep "[y|N]" as is.
			my $answer = ask(sprintf(__("Do you really want to send %s? [y|N]: "), $file),
					 valid_re => qr/^(?:y|n)/i,
					 default => 'n');
			$skip = ($answer ne 'y');
			if ($skip) {
				$known_suffix = $suffix;
			}
		}
	}
	return ($skip, $known_suffix);
}

sub handle_backup_files {
	my @file = @_;
	my ($last, $lastlen, $known_suffix, $skip, @result);
	for my $file (@file) {
		($skip, $known_suffix) = handle_backup($last, $lastlen,
						       $file, $known_suffix);
		push @result, $file unless $skip;
		$last = $file;
		$lastlen = length($file);
	}
	return @result;
}

sub file_has_nonascii {
	my $fn = shift;
	open(my $fh, '<', $fn)
		or die sprintf(__("unable to open %s: %s\n"), $fn, $!);
	while (my $line = <$fh>) {
		return 1 if $line =~ /[^[:ascii:]]/;
	}
	return 0;
}

sub body_or_subject_has_nonascii {
	my $fn = shift;
	open(my $fh, '<', $fn)
		or die sprintf(__("unable to open %s: %s\n"), $fn, $!);
	while (my $line = <$fh>) {
		last if $line =~ /^$/;
		return 1 if $line =~ /^Subject.*[^[:ascii:]]/;
	}
	while (my $line = <$fh>) {
		return 1 if $line =~ /[^[:ascii:]]/;
	}
	return 0;
}
