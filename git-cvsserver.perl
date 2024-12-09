#!/usr/bin/perl

####
#### This application is a CVS emulation layer for git.
#### It is intended for clients to connect over SSH.
#### See the documentation for more details.
####
#### Copyright The Open University UK - 2006.
####
#### Authors: Martyn Smith    <martyn@catalyst.net.nz>
####          Martin Langhoff <martin@laptop.org>
####
####
#### Released under the GNU Public License, version 2.
####
####

require v5.26;
use strict;
use warnings;
use bytes;

use Fcntl;
use File::Temp qw/tempdir tempfile/;
use File::Path qw/rmtree/;
use File::Basename;
use Getopt::Long qw(:config require_order no_ignore_case);

my $VERSION = '@GIT_VERSION@';

my $log = GITCVS::log->new();
my $cfg;

my $DATE_LIST = {
    Jan => "01",
    Feb => "02",
    Mar => "03",
    Apr => "04",
    May => "05",
    Jun => "06",
    Jul => "07",
    Aug => "08",
    Sep => "09",
    Oct => "10",
    Nov => "11",
    Dec => "12",
};

# Enable autoflush for STDOUT (otherwise the whole thing falls apart)
$| = 1;

#### Definition and mappings of functions ####

# NOTE: Despite the existence of req_CATCHALL and req_EMPTY unimplemented
#  requests, this list is incomplete.  It is missing many rarer/optional
#  requests.  Perhaps some clients require a claim of support for
#  these specific requests for main functionality to work?
my $methods = {
    'Root'            => \&req_Root,
    'Valid-responses' => \&req_Validresponses,
    'valid-requests'  => \&req_validrequests,
    'Directory'       => \&req_Directory,
    'Sticky'          => \&req_Sticky,
    'Entry'           => \&req_Entry,
    'Modified'        => \&req_Modified,
    'Unchanged'       => \&req_Unchanged,
    'Questionable'    => \&req_Questionable,
    'Argument'        => \&req_Argument,
    'Argumentx'       => \&req_Argument,
    'expand-modules'  => \&req_expandmodules,
    'add'             => \&req_add,
    'remove'          => \&req_remove,
    'co'              => \&req_co,
    'update'          => \&req_update,
    'ci'              => \&req_ci,
    'diff'            => \&req_diff,
    'log'             => \&req_log,
    'rlog'            => \&req_log,
    'tag'             => \&req_CATCHALL,
    'status'          => \&req_status,
    'admin'           => \&req_CATCHALL,
    'history'         => \&req_CATCHALL,
    'watchers'        => \&req_EMPTY,
    'editors'         => \&req_EMPTY,
    'noop'            => \&req_EMPTY,
    'annotate'        => \&req_annotate,
    'Global_option'   => \&req_Globaloption,
};

##############################################


# $state holds all the bits of information the clients sends us that could
# potentially be useful when it comes to actually _doing_ something.
my $state = { prependdir => '' };

# Work is for managing temporary working directory
my $work =
    {
        state => undef,  # undef, 1 (empty), 2 (with stuff)
        workDir => undef,
        index => undef,
        emptyDir => undef,
        tmpDir => undef
    };

$log->info("--------------- STARTING -----------------");

my $usage =
    "usage: git cvsserver [options] [pserver|server] [<directory> ...]\n".
    "    --base-path <path>  : Prepend to requested CVSROOT\n".
    "                          Can be read from GIT_CVSSERVER_BASE_PATH\n".
    "    --strict-paths      : Don't allow recursing into subdirectories\n".
    "    --export-all        : Don't check for gitcvs.enabled in config\n".
    "    --version, -V       : Print version information and exit\n".
    "    -h, -H              : Print usage information and exit\n".
    "\n".
    "<directory> ... is a list of allowed directories. If no directories\n".
    "are given, all are allowed. This is an additional restriction, gitcvs\n".
    "access still needs to be enabled by the gitcvs.enabled config option.\n".
    "Alternately, one directory may be specified in GIT_CVSSERVER_ROOT.\n";

my @opts = ( 'h|H', 'version|V',
	     'base-path=s', 'strict-paths', 'export-all' );
GetOptions( $state, @opts )
    or die $usage;

if ($state->{version}) {
    print "git-cvsserver version $VERSION\n";
    exit;
}
if ($state->{help}) {
    print $usage;
    exit;
}

my $TEMP_DIR = tempdir( CLEANUP => 1 );
$log->debug("Temporary directory is '$TEMP_DIR'");

$state->{method} = 'ext';
if (@ARGV) {
    if ($ARGV[0] eq 'pserver') {
	$state->{method} = 'pserver';
	shift @ARGV;
    } elsif ($ARGV[0] eq 'server') {
	shift @ARGV;
    }
}

# everything else is a directory
$state->{allowed_roots} = [ @ARGV ];

# don't export the whole system unless the users requests it
if ($state->{'export-all'} && !@{$state->{allowed_roots}}) {
    die "--export-all can only be used together with an explicit '<directory>...' list\n";
}

# Environment handling for running under git-shell
if (exists $ENV{GIT_CVSSERVER_BASE_PATH}) {
    if ($state->{'base-path'}) {
	die "Cannot specify base path both ways.\n";
    }
    my $base_path = $ENV{GIT_CVSSERVER_BASE_PATH};
    $state->{'base-path'} = $base_path;
    $log->debug("Picked up base path '$base_path' from environment.\n");
}
if (exists $ENV{GIT_CVSSERVER_ROOT}) {
    if (@{$state->{allowed_roots}}) {
	die "Cannot specify roots both ways: @ARGV\n";
    }
    my $allowed_root = $ENV{GIT_CVSSERVER_ROOT};
    $state->{allowed_roots} = [ $allowed_root ];
    $log->debug("Picked up allowed root '$allowed_root' from environment.\n");
}

# if we are called with a pserver argument,
# deal with the authentication cat before entering the
# main loop
if ($state->{method} eq 'pserver') {
    my $line = <STDIN>; chomp $line;
    unless( $line =~ /^BEGIN (AUTH|VERIFICATION) REQUEST$/) {
       die "E Do not understand $line - expecting BEGIN AUTH REQUEST\n";
    }
    my $request = $1;
    $line = <STDIN>; chomp $line;
    unless (req_Root('root', $line)) { # reuse Root
       print "E Invalid root $line \n";
       exit 1;
    }
    $line = <STDIN>; chomp $line;
    my $user = $line;
    $line = <STDIN>; chomp $line;
    my $password = $line;

    if ($user eq 'anonymous') {
        # "A" will be 1 byte, use length instead in case the
        # encryption method ever changes (yeah, right!)
        if (length($password) > 1 ) {
            print "E Don't supply a password for the `anonymous' user\n";
            print "I HATE YOU\n";
            exit 1;
        }

        # Fall through to LOVE
    } else {
        # Trying to authenticate a user
        if (not exists $cfg->{gitcvs}->{authdb}) {
            print "E the repo config file needs a [gitcvs] section with an 'authdb' parameter set to the filename of the authentication database\n";
            print "I HATE YOU\n";
            exit 1;
        }

        my $authdb = $cfg->{gitcvs}->{authdb};

        unless (-e $authdb) {
            print "E The authentication database specified in [gitcvs.authdb] does not exist\n";
            print "I HATE YOU\n";
            exit 1;
        }

        my $auth_ok;
        open my $passwd, "<", $authdb or die $!;
        while (<$passwd>) {
            if (m{^\Q$user\E:(.*)}) {
                my $hash = crypt(descramble($password), $1);
                if (defined $hash and $hash eq $1) {
                    $auth_ok = 1;
                }
            }
        }
        close $passwd;

        unless ($auth_ok) {
            print "I HATE YOU\n";
            exit 1;
        }

        # Fall through to LOVE
    }

    # For checking whether the user is anonymous on commit
    $state->{user} = $user;

    $line = <STDIN>; chomp $line;
    unless ($line eq "END $request REQUEST") {
       die "E Do not understand $line -- expecting END $request REQUEST\n";
    }
    print "I LOVE YOU\n";
    exit if $request eq 'VERIFICATION'; # cvs login
    # and now back to our regular programme...
}

# Keep going until the client closes the connection
while (<STDIN>)
{
    chomp;

    # Check to see if we've seen this method, and call appropriate function.
    if ( /^([\w-]+)(?:\s+(.*))?$/ and defined($methods->{$1}) )
    {
        # use the $methods hash to call the appropriate sub for this command
        #$log->info("Method : $1");
        &{$methods->{$1}}($1,$2);
    } else {
        # log fatal because we don't understand this function. If this happens
        # we're fairly screwed because we don't know if the client is expecting
        # a response. If it is, the client will hang, we'll hang, and the whole
        # thing will be custard.
        $log->fatal("Don't understand command $_\n");
        die("Unknown command $_");
    }
}

$log->debug("Processing time : user=" . (times)[0] . " system=" . (times)[1]);
$log->info("--------------- FINISH -----------------");

chdir '/';
exit 0;

# Magic catchall method.
#    This is the method that will handle all commands we haven't yet
#    implemented. It simply sends a warning to the log file indicating a
#    command that hasn't been implemented has been invoked.
sub req_CATCHALL
{
    my ( $cmd, $data ) = @_;
    $log->warn("Unhandled command : req_$cmd : $data");
}

# This method invariably succeeds with an empty response.
sub req_EMPTY
{
    print "ok\n";
}

# Root pathname \n
#     Response expected: no. Tell the server which CVSROOT to use. Note that
#     pathname is a local directory and not a fully qualified CVSROOT variable.
#     pathname must already exist; if creating a new root, use the init
#     request, not Root. pathname does not include the hostname of the server,
#     how to access the server, etc.; by the time the CVS protocol is in use,
#     connection, authentication, etc., are already taken care of. The Root
#     request must be sent only once, and it must be sent before any requests
#     other than Valid-responses, valid-requests, UseUnchanged, Set or init.
sub req_Root
{
    my ( $cmd, $data ) = @_;
    $log->debug("req_Root : $data");

    unless ($data =~ m#^/#) {
	print "error 1 Root must be an absolute pathname\n";
	return 0;
    }

    my $cvsroot = $state->{'base-path'} || '';
    $cvsroot =~ s#/+$##;
    $cvsroot .= $data;

    if ($state->{CVSROOT}
	&& ($state->{CVSROOT} ne $cvsroot)) {
	print "error 1 Conflicting roots specified\n";
	return 0;
    }

    $state->{CVSROOT} = $cvsroot;

    $ENV{GIT_DIR} = $state->{CVSROOT} . "/";

    if (@{$state->{allowed_roots}}) {
	my $allowed = 0;
	foreach my $dir (@{$state->{allowed_roots}}) {
	    next unless $dir =~ m#^/#;
	    $dir =~ s#/+$##;
	    if ($state->{'strict-paths'}) {
		if ($ENV{GIT_DIR} =~ m#^\Q$dir\E/?$#) {
		    $allowed = 1;
		    last;
		}
	    } elsif ($ENV{GIT_DIR} =~ m#^\Q$dir\E(/?$|/)#) {
		$allowed = 1;
		last;
	    }
	}

	unless ($allowed) {
	    print "E $ENV{GIT_DIR} does not seem to be a valid GIT repository\n";
	    print "E \n";
	    print "error 1 $ENV{GIT_DIR} is not a valid repository\n";
	    return 0;
	}
    }

    unless (-d $ENV{GIT_DIR} && -e $ENV{GIT_DIR}.'HEAD') {
       print "E $ENV{GIT_DIR} does not seem to be a valid GIT repository\n";
       print "E \n";
       print "error 1 $ENV{GIT_DIR} is not a valid repository\n";
       return 0;
    }

    my @gitvars = safe_pipe_capture(qw(git config -l));
    if ($?) {
       print "E problems executing git-config on the server -- this is not a git repository or the PATH is not set correctly.\n";
        print "E \n";
        print "error 1 - problem executing git-config\n";
       return 0;
    }
    foreach my $line ( @gitvars )
    {
        next unless ( $line =~ /^(gitcvs|extensions)\.(?:(ext|pserver)\.)?([\w-]+)=(.*)$/ );
        unless ($2) {
            $cfg->{$1}{$3} = $4;
        } else {
            $cfg->{$1}{$2}{$3} = $4;
        }
    }

    my $enabled = ($cfg->{gitcvs}{$state->{method}}{enabled}
		   || $cfg->{gitcvs}{enabled});
    unless ($state->{'export-all'} ||
	    ($enabled && $enabled =~ /^\s*(1|true|yes)\s*$/i)) {
        print "E GITCVS emulation needs to be enabled on this repo\n";
        print "E the repo config file needs a [gitcvs] section added, and the parameter 'enabled' set to 1\n";
        print "E \n";
        print "error 1 GITCVS emulation disabled\n";
        return 0;
    }

    my $logfile = $cfg->{gitcvs}{$state->{method}}{logfile} || $cfg->{gitcvs}{logfile};
    if ( $logfile )
    {
        $log->setfile($logfile);
    } else {
        $log->nofile();
    }

    $state->{rawsz} = ($cfg->{'extensions'}{'objectformat'} || 'sha1') eq 'sha256' ? 32 : 20;
    $state->{hexsz} = $state->{rawsz} * 2;

    return 1;
}

# Global_option option \n
#     Response expected: no. Transmit one of the global options `-q', `-Q',
#     `-l', `-t', `-r', or `-n'. option must be one of those strings, no
#     variations (such as combining of options) are allowed. For graceful
#     handling of valid-requests, it is probably better to make new global
#     options separate requests, rather than trying to add them to this
#     request.
sub req_Globaloption
{
    my ( $cmd, $data ) = @_;
    $log->debug("req_Globaloption : $data");
    $state->{globaloptions}{$data} = 1;
}

# Valid-responses request-list \n
#     Response expected: no. Tell the server what responses the client will
#     accept. request-list is a space separated list of tokens.
sub req_Validresponses
{
    my ( $cmd, $data ) = @_;
    $log->debug("req_Validresponses : $data");

    # TODO : re-enable this, currently it's not particularly useful
    #$state->{validresponses} = [ split /\s+/, $data ];
}

# valid-requests \n
#     Response expected: yes. Ask the server to send back a Valid-requests
#     response.
sub req_validrequests
{
    my ( $cmd, $data ) = @_;

    $log->debug("req_validrequests");

    $log->debug("SEND : Valid-requests " . join(" ",sort keys %$methods));
    $log->debug("SEND : ok");

    print "Valid-requests " . join(" ",sort keys %$methods) . "\n";
    print "ok\n";
}

# Directory local-directory \n
#     Additional data: repository \n. Response expected: no. Tell the server
#     what directory to use. The repository should be a directory name from a
#     previous server response. Note that this both gives a default for Entry
#     and Modified and also for ci and the other commands; normal usage is to
#     send Directory for each directory in which there will be an Entry or
#     Modified, and then a final Directory for the original directory, then the
#     command. The local-directory is relative to the top level at which the
#     command is occurring (i.e. the last Directory which is sent before the
#     command); to indicate that top level, `.' should be sent for
#     local-directory.
sub req_Directory
{
    my ( $cmd, $data ) = @_;

    my $repository = <STDIN>;
    chomp $repository;


    $state->{localdir} = $data;
    $state->{repository} = $repository;
    $state->{path} = $repository;
    $state->{path} =~ s/^\Q$state->{CVSROOT}\E\///;
    $state->{module} = $1 if ($state->{path} =~ s/^(.*?)(\/|$)//);
    $state->{path} .= "/" if ( $state->{path} =~ /\S/ );

    $state->{directory} = $state->{localdir};
    $state->{directory} = "" if ( $state->{directory} eq "." );
    $state->{directory} .= "/" if ( $state->{directory} =~ /\S/ );

    if ( (not defined($state->{prependdir}) or $state->{prependdir} eq '') and $state->{localdir} eq "." and $state->{path} =~ /\S/ )
    {
        $log->info("Setting prepend to '$state->{path}'");
        $state->{prependdir} = $state->{path};
        my %entries;
        foreach my $entry ( keys %{$state->{entries}} )
        {
            $entries{$state->{prependdir} . $entry} = $state->{entries}{$entry};
        }
        $state->{entries}=\%entries;

        my %dirMap;
        foreach my $dir ( keys %{$state->{dirMap}} )
        {
            $dirMap{$state->{prependdir} . $dir} = $state->{dirMap}{$dir};
        }
        $state->{dirMap}=\%dirMap;
    }

    if ( defined ( $state->{prependdir} ) )
    {
        $log->debug("Prepending '$state->{prependdir}' to state|directory");
        $state->{directory} = $state->{prependdir} . $state->{directory}
    }

    if ( ! defined($state->{dirMap}{$state->{directory}}) )
    {
        $state->{dirMap}{$state->{directory}} =
            {
                'names' => {}
                #'tagspec' => undef
            };
    }

    $log->debug("req_Directory : localdir=$data repository=$repository path=$state->{path} directory=$state->{directory} module=$state->{module}");
}

# Sticky tagspec \n
#     Response expected: no. Tell the server that the directory most
#     recently specified with Directory has a sticky tag or date
#     tagspec. The first character of tagspec is T for a tag, D for
#     a date, or some other character supplied by a Set-sticky
#     response from a previous request to the server. The remainder
#     of tagspec contains the actual tag or date, again as supplied
#     by Set-sticky.
#          The server should remember Static-directory and Sticky requests
#     for a particular directory; the client need not resend them each
#     time it sends a Directory request for a given directory. However,
#     the server is not obliged to remember them beyond the context
#     of a single command.
sub req_Sticky
{
    my ( $cmd, $tagspec ) = @_;

    my ( $stickyInfo );
    if($tagspec eq "")
    {
        # nothing
    }
    elsif($tagspec=~/^T([^ ]+)\s*$/)
    {
        $stickyInfo = { 'tag' => $1 };
    }
    elsif($tagspec=~/^D([0-9.]+)\s*$/)
    {
        $stickyInfo= { 'date' => $1 };
    }
    else
    {
        die "Unknown tag_or_date format\n";
    }
    $state->{dirMap}{$state->{directory}}{stickyInfo}=$stickyInfo;

    $log->debug("req_Sticky : tagspec=$tagspec repository=$state->{repository}"
                . " path=$state->{path} directory=$state->{directory}"
                . " module=$state->{module}");
}

# Entry entry-line \n
#     Response expected: no. Tell the server what version of a file is on the
#     local machine. The name in entry-line is a name relative to the directory
#     most recently specified with Directory. If the user is operating on only
#     some files in a directory, Entry requests for only those files need be
#     included. If an Entry request is sent without Modified, Is-modified, or
#     Unchanged, it means the file is lost (does not exist in the working
#     directory). If both Entry and one of Modified, Is-modified, or Unchanged
#     are sent for the same file, Entry must be sent first. For a given file,
#     one can send Modified, Is-modified, or Unchanged, but not more than one
#     of these three.
sub req_Entry
{
    my ( $cmd, $data ) = @_;

    #$log->debug("req_Entry : $data");

    my @data = split(/\//, $data, -1);

    $state->{entries}{$state->{directory}.$data[1]} = {
        revision    => $data[2],
        conflict    => $data[3],
        options     => $data[4],
        tag_or_date => $data[5],
    };

    $state->{dirMap}{$state->{directory}}{names}{$data[1]} = 'F';

    $log->info("Received entry line '$data' => '" . $state->{directory} . $data[1] . "'");
}

# Questionable filename \n
#     Response expected: no. Additional data: no. Tell the server to check
#     whether filename should be ignored, and if not, next time the server
#     sends responses, send (in a M response) `?' followed by the directory and
#     filename. filename must not contain `/'; it needs to be a file in the
#     directory named by the most recent Directory request.
sub req_Questionable
{
    my ( $cmd, $data ) = @_;

    $log->debug("req_Questionable : $data");
    $state->{entries}{$state->{directory}.$data}{questionable} = 1;
}

# add \n
#     Response expected: yes. Add a file or directory. This uses any previous
#     Argument, Directory, Entry, or Modified requests, if they have been sent.
#     The last Directory sent specifies the working directory at the time of
#     the operation. To add a directory, send the directory to be added using
#     Directory and Argument requests.
sub req_add
{
    my ( $cmd, $data ) = @_;

    argsplit("add");

    my $updater = GITCVS::updater->new($state->{CVSROOT}, $state->{module}, $log);
    $updater->update();

    my $addcount = 0;

    foreach my $filename ( @{$state->{args}} )
    {
        $filename = filecleanup($filename);

        # no -r, -A, or -D with add
        my $stickyInfo = resolveStickyInfo($filename);

        my $meta = $updater->getmeta($filename,$stickyInfo);
        my $wrev = revparse($filename);

        if ($wrev && $meta && ($wrev=~/^-/))
        {
            # previously removed file, add back
            $log->info("added file $filename was previously removed, send $meta->{revision}");

            print "MT +updated\n";
            print "MT text U \n";
            print "MT fname $filename\n";
            print "MT newline\n";
            print "MT -updated\n";

            unless ( $state->{globaloptions}{-n} )
            {
                my ( $filepart, $dirpart ) = filenamesplit($filename,1);

                print "Created $dirpart\n";
                print $state->{CVSROOT} . "/$state->{module}/$filename\n";

                # this is an "entries" line
                my $kopts = kopts_from_path($filename,"sha1",$meta->{filehash});
                my $entryLine = "/$filepart/$meta->{revision}//$kopts/";
                $entryLine .= getStickyTagOrDate($stickyInfo);
                $log->debug($entryLine);
                print "$entryLine\n";
                # permissions
                $log->debug("SEND : u=$meta->{mode},g=$meta->{mode},o=$meta->{mode}");
                print "u=$meta->{mode},g=$meta->{mode},o=$meta->{mode}\n";
                # transmit file
                transmitfile($meta->{filehash});
            }

            next;
        }

        unless ( defined ( $state->{entries}{$filename}{modified_filename} ) )
        {
            print "E cvs add: nothing known about `$filename'\n";
            next;
        }
        # TODO : check we're not squashing an already existing file
        if ( defined ( $state->{entries}{$filename}{revision} ) )
        {
            print "E cvs add: `$filename' has already been entered\n";
            next;
        }

        my ( $filepart, $dirpart ) = filenamesplit($filename, 1);

        print "E cvs add: scheduling file `$filename' for addition\n";

        print "Checked-in $dirpart\n";
        print "$filename\n";
        my $kopts = kopts_from_path($filename,"file",
                        $state->{entries}{$filename}{modified_filename});
        print "/$filepart/0//$kopts/" .
              getStickyTagOrDate($stickyInfo) . "\n";

        my $requestedKopts = $state->{opt}{k};
        if(defined($requestedKopts))
        {
            $requestedKopts = "-k$requestedKopts";
        }
        else
        {
            $requestedKopts = "";
        }
        if( $kopts ne $requestedKopts )
        {
            $log->warn("Ignoring requested -k='$requestedKopts'"
                        . " for '$filename'; detected -k='$kopts' instead");
            #TODO: Also have option to send warning to user?
        }

        $addcount++;
    }

    if ( $addcount == 1 )
    {
        print "E cvs add: use `cvs commit' to add this file permanently\n";
    }
    elsif ( $addcount > 1 )
    {
        print "E cvs add: use `cvs commit' to add these files permanently\n";
    }

    print "ok\n";
}

# remove \n
#     Response expected: yes. Remove a file. This uses any previous Argument,
#     Directory, Entry, or Modified requests, if they have been sent. The last
#     Directory sent specifies the working directory at the time of the
#     operation. Note that this request does not actually do anything to the
#     repository; the only effect of a successful remove request is to supply
#     the client with a new entries line containing `-' to indicate a removed
#     file. In fact, the client probably could perform this operation without
#     contacting the server, although using remove may cause the server to
#     perform a few more checks. The client sends a subsequent ci request to
#     actually record the removal in the repository.
sub req_remove
{
    my ( $cmd, $data ) = @_;

    argsplit("remove");

    # Grab a handle to the SQLite db and do any necessary updates
    my $updater = GITCVS::updater->new($state->{CVSROOT}, $state->{module}, $log);
    $updater->update();

    #$log->debug("add state : " . Dumper($state));

    my $rmcount = 0;

    foreach my $filename ( @{$state->{args}} )
    {
        $filename = filecleanup($filename);

        if ( defined ( $state->{entries}{$filename}{unchanged} ) or defined ( $state->{entries}{$filename}{modified_filename} ) )
        {
            print "E cvs remove: file `$filename' still in working directory\n";
            next;
        }

        # only from entries
        my $stickyInfo = resolveStickyInfo($filename);

        my $meta = $updater->getmeta($filename,$stickyInfo);
        my $wrev = revparse($filename);

        unless ( defined ( $wrev ) )
        {
            print "E cvs remove: nothing known about `$filename'\n";
            next;
        }

        if ( defined($wrev) and ($wrev=~/^-/) )
        {
            print "E cvs remove: file `$filename' already scheduled for removal\n";
            next;
        }

        unless ( $wrev eq $meta->{revision} )
        {
            # TODO : not sure if the format of this message is quite correct.
            print "E cvs remove: Up to date check failed for `$filename'\n";
            next;
        }


        my ( $filepart, $dirpart ) = filenamesplit($filename, 1);

        print "E cvs remove: scheduling `$filename' for removal\n";

        print "Checked-in $dirpart\n";
        print "$filename\n";
        my $kopts = kopts_from_path($filename,"sha1",$meta->{filehash});
        print "/$filepart/-$wrev//$kopts/" . getStickyTagOrDate($stickyInfo) . "\n";

        $rmcount++;
    }

    if ( $rmcount == 1 )
    {
        print "E cvs remove: use `cvs commit' to remove this file permanently\n";
    }
    elsif ( $rmcount > 1 )
    {
        print "E cvs remove: use `cvs commit' to remove these files permanently\n";
    }

    print "ok\n";
}

# Modified filename \n
#     Response expected: no. Additional data: mode, \n, file transmission. Send
#     the server a copy of one locally modified file. filename is a file within
#     the most recent directory sent with Directory; it must not contain `/'.
#     If the user is operating on only some files in a directory, only those
#     files need to be included. This can also be sent without Entry, if there
#     is no entry for the file.
sub req_Modified
{
    my ( $cmd, $data ) = @_;

    my $mode = <STDIN>;
    defined $mode
        or (print "E end of file reading mode for $data\n"), return;
    chomp $mode;
    my $size = <STDIN>;
    defined $size
        or (print "E end of file reading size of $data\n"), return;
    chomp $size;

    # Grab config information
    my $blocksize = 8192;
    my $bytesleft = $size;
    my $tmp;

    # Get a filehandle/name to write it to
    my ( $fh, $filename ) = tempfile( DIR => $TEMP_DIR );

    # Loop over file data writing out to temporary file.
    while ( $bytesleft )
    {
        $blocksize = $bytesleft if ( $bytesleft < $blocksize );
        read STDIN, $tmp, $blocksize;
        print $fh $tmp;
        $bytesleft -= $blocksize;
    }

    close $fh
        or (print "E failed to write temporary, $filename: $!\n"), return;

    # Ensure we have something sensible for the file mode
    if ( $mode =~ /u=(\w+)/ )
    {
        $mode = $1;
    } else {
        $mode = "rw";
    }

    # Save the file data in $state
    $state->{entries}{$state->{directory}.$data}{modified_filename} = $filename;
    $state->{entries}{$state->{directory}.$data}{modified_mode} = $mode;
    $state->{entries}{$state->{directory}.$data}{modified_hash} = safe_pipe_capture('git','hash-object',$filename);
    $state->{entries}{$state->{directory}.$data}{modified_hash} =~ s/\s.*$//s;

    #$log->debug("req_Modified : file=$data mode=$mode size=$size");
}

# Unchanged filename \n
#     Response expected: no. Tell the server that filename has not been
#     modified in the checked out directory. The filename is a file within the
#     most recent directory sent with Directory; it must not contain `/'.
sub req_Unchanged
{
    my ( $cmd, $data ) = @_;

    $state->{entries}{$state->{directory}.$data}{unchanged} = 1;

    #$log->debug("req_Unchanged : $data");
}

# Argument text \n
#     Response expected: no. Save argument for use in a subsequent command.
#     Arguments accumulate until an argument-using command is given, at which
#     point they are forgotten.
# Argumentx text \n
#     Response expected: no. Append \n followed by text to the current argument
#     being saved.
sub req_Argument
{
    my ( $cmd, $data ) = @_;

    # Argumentx means: append to last Argument (with a newline in front)

    $log->debug("$cmd : $data");

    if ( $cmd eq 'Argumentx') {
        ${$state->{arguments}}[$#{$state->{arguments}}] .= "\n" . $data;
    } else {
        push @{$state->{arguments}}, $data;
    }
}

# expand-modules \n
#     Response expected: yes. Expand the modules which are specified in the
#     arguments. Returns the data in Module-expansion responses. Note that the
#     server can assume that this is checkout or export, not rtag or rdiff; the
#     latter do not access the working directory and thus have no need to
#     expand modules on the client side. Expand may not be the best word for
#     what this request does. It does not necessarily tell you all the files
#     contained in a module, for example. Basically it is a way of telling you
#     which working directories the server needs to know about in order to
#     handle a checkout of the specified modules. For example, suppose that the
#     server has a module defined by
#   aliasmodule -a 1dir
#     That is, one can check out aliasmodule and it will take 1dir in the
#     repository and check it out to 1dir in the working directory. Now suppose
#     the client already has this module checked out and is planning on using
#     the co request to update it. Without using expand-modules, the client
#     would have two bad choices: it could either send information about all
#     working directories under the current directory, which could be
#     unnecessarily slow, or it could be ignorant of the fact that aliasmodule
#     stands for 1dir, and neglect to send information for 1dir, which would
#     lead to incorrect operation. With expand-modules, the client would first
#     ask for the module to be expanded:
sub req_expandmodules
{
    my ( $cmd, $data ) = @_;

    argsplit();

    $log->debug("req_expandmodules : " . ( defined($data) ? $data : "[NULL]" ) );

    unless ( ref $state->{arguments} eq "ARRAY" )
    {
        print "ok\n";
        return;
    }

    foreach my $module ( @{$state->{arguments}} )
    {
        $log->debug("SEND : Module-expansion $module");
        print "Module-expansion $module\n";
    }

    print "ok\n";
    statecleanup();
}

# co \n
#     Response expected: yes. Get files from the repository. This uses any
#     previous Argument, Directory, Entry, or Modified requests, if they have
#     been sent. Arguments to this command are module names; the client cannot
#     know what directories they correspond to except by (1) just sending the
#     co request, and then seeing what directory names the server sends back in
#     its responses, and (2) the expand-modules request.
sub req_co
{
    my ( $cmd, $data ) = @_;

    argsplit("co");

    # Provide list of modules, if -c was used.
    if (exists $state->{opt}{c}) {
        my $showref = safe_pipe_capture(qw(git show-ref --heads));
        for my $line (split '\n', $showref) {
            if ( $line =~ m% refs/heads/(.*)$% ) {
                print "M $1\t$1\n";
            }
        }
        print "ok\n";
        return 1;
    }

    my $stickyInfo = { 'tag' => $state->{opt}{r},
                       'date' => $state->{opt}{D} };

    my $module = $state->{args}[0];
    $state->{module} = $module;
    my $checkout_path = $module;

    # use the user specified directory if we're given it
    $checkout_path = $state->{opt}{d} if ( exists ( $state->{opt}{d} ) );

    $log->debug("req_co : " . ( defined($data) ? $data : "[NULL]" ) );

    $log->info("Checking out module '$module' ($state->{CVSROOT}) to '$checkout_path'");

    $ENV{GIT_DIR} = $state->{CVSROOT} . "/";

    # Grab a handle to the SQLite db and do any necessary updates
    my $updater = GITCVS::updater->new($state->{CVSROOT}, $module, $log);
    $updater->update();

    my $headHash;
    if( defined($stickyInfo) && defined($stickyInfo->{tag}) )
    {
        $headHash = $updater->lookupCommitRef($stickyInfo->{tag});
        if( !defined($headHash) )
        {
            print "error 1 no such tag `$stickyInfo->{tag}'\n";
            cleanupWorkTree();
            exit;
        }
    }

    $checkout_path =~ s|/$||; # get rid of trailing slashes

    my %seendirs = ();
    my $lastdir ='';

    prepDirForOutput(
            ".",
            $state->{CVSROOT} . "/$module",
            $checkout_path,
            \%seendirs,
            'checkout',
            $state->{dirArgs} );

    foreach my $git ( @{$updater->getAnyHead($headHash)} )
    {
        # Don't want to check out deleted files
        next if ( $git->{filehash} eq "deleted" );

        my $fullName = $git->{name};
        ( $git->{name}, $git->{dir} ) = filenamesplit($git->{name});

        unless (exists($seendirs{$git->{dir}})) {
            prepDirForOutput($git->{dir}, $state->{CVSROOT} . "/$module/",
                             $checkout_path, \%seendirs, 'checkout',
                             $state->{dirArgs} );
            $lastdir = $git->{dir};
            $seendirs{$git->{dir}} = 1;
        }

        # modification time of this file
        print "Mod-time $git->{modified}\n";

        # print some information to the client
        if ( defined ( $git->{dir} ) and $git->{dir} ne "./" )
        {
            print "M U $checkout_path/$git->{dir}$git->{name}\n";
        } else {
            print "M U $checkout_path/$git->{name}\n";
        }

       # instruct client we're sending a file to put in this path
       print "Created $checkout_path/" . ( defined ( $git->{dir} ) and $git->{dir} ne "./" ? $git->{dir} . "/" : "" ) . "\n";

       print $state->{CVSROOT} . "/$module/" . ( defined ( $git->{dir} ) and $git->{dir} ne "./" ? $git->{dir} . "/" : "" ) . "$git->{name}\n";

        # this is an "entries" line
        my $kopts = kopts_from_path($fullName,"sha1",$git->{filehash});
        print "/$git->{name}/$git->{revision}//$kopts/" .
                        getStickyTagOrDate($stickyInfo) . "\n";
        # permissions
        print "u=$git->{mode},g=$git->{mode},o=$git->{mode}\n";

        # transmit file
        transmitfile($git->{filehash});
    }

    print "ok\n";

    statecleanup();
}

# used by req_co and req_update to set up directories for files
# recursively handles parents
sub prepDirForOutput
{
    my ($dir, $repodir, $remotedir, $seendirs, $request, $dirArgs) = @_;

    my $parent = dirname($dir);
    $dir       =~ s|/+$||;
    $repodir   =~ s|/+$||;
    $remotedir =~ s|/+$||;
    $parent    =~ s|/+$||;

    if ($parent eq '.' || $parent eq './')
    {
        $parent = '';
    }
    # recurse to announce unseen parents first
    if( length($parent) &&
        !exists($seendirs->{$parent}) &&
        ( $request eq "checkout" ||
          exists($dirArgs->{$parent}) ) )
    {
        prepDirForOutput($parent, $repodir, $remotedir,
                         $seendirs, $request, $dirArgs);
    }
    # Announce that we are going to modify at the parent level
    if ($dir eq '.' || $dir eq './')
    {
        $dir = '';
    }
    if(exists($seendirs->{$dir}))
    {
        return;
    }
    $log->debug("announcedir $dir, $repodir, $remotedir" );
    my($thisRemoteDir,$thisRepoDir);
    if ($dir ne "")
    {
        $thisRepoDir="$repodir/$dir";
        if($remotedir eq ".")
        {
            $thisRemoteDir=$dir;
        }
        else
        {
            $thisRemoteDir="$remotedir/$dir";
        }
    }
    else
    {
        $thisRepoDir=$repodir;
        $thisRemoteDir=$remotedir;
    }
    unless ( $state->{globaloptions}{-Q} || $state->{globaloptions}{-q} )
    {
        print "E cvs $request: Updating $thisRemoteDir\n";
    }

    my ($opt_r)=$state->{opt}{r};
    my $stickyInfo;
    if(exists($state->{opt}{A}))
    {
        # $stickyInfo=undef;
    }
    elsif( defined($opt_r) && $opt_r ne "" )
           # || ( defined($state->{opt}{D}) && $state->{opt}{D} ne "" ) # TODO
    {
        $stickyInfo={ 'tag' => (defined($opt_r)?$opt_r:undef) };

        # TODO: Convert -D value into the form 2011.04.10.04.46.57,
        #   similar to an entry line's sticky date, without the D prefix.
        #   It sometimes (always?) arrives as something more like
        #   '10 Apr 2011 04:46:57 -0000'...
        # $stickyInfo={ 'date' => (defined($stickyDate)?$stickyDate:undef) };
    }
    else
    {
        $stickyInfo=getDirStickyInfo($state->{prependdir} . $dir);
    }

    my $stickyResponse;
    if(defined($stickyInfo))
    {
        $stickyResponse = "Set-sticky $thisRemoteDir/\n" .
                          "$thisRepoDir/\n" .
                          getStickyTagOrDate($stickyInfo) . "\n";
    }
    else
    {
        $stickyResponse = "Clear-sticky $thisRemoteDir/\n" .
                          "$thisRepoDir/\n";
    }

    unless ( $state->{globaloptions}{-n} )
    {
        print $stickyResponse;

        print "Clear-static-directory $thisRemoteDir/\n";
        print "$thisRepoDir/\n";
        print $stickyResponse; # yes, twice
        print "Template $thisRemoteDir/\n";
        print "$thisRepoDir/\n";
        print "0\n";
    }

    $seendirs->{$dir} = 1;

    # FUTURE: This would more accurately emulate CVS by sending
    #   another copy of sticky after processing the files in that
    #   directory.  Or intermediate: perhaps send all sticky's for
    #   $seendirs after processing all files.
}

# update \n
#     Response expected: yes. Actually do a cvs update command. This uses any
#     previous Argument, Directory, Entry, or Modified requests, if they have
#     been sent. The last Directory sent specifies the working directory at the
#     time of the operation. The -I option is not used--files which the client
#     can decide whether to ignore are not mentioned and the client sends the
#     Questionable request for others.
sub req_update
{
    my ( $cmd, $data ) = @_;

    $log->debug("req_update : " . ( defined($data) ? $data : "[NULL]" ));

    argsplit("update");

    #
    # It may just be a client exploring the available heads/modules
    # in that case, list them as top level directories and leave it
    # at that. Eclipse uses this technique to offer you a list of
    # projects (heads in this case) to checkout.
    #
    if ($state->{module} eq '') {
        my $showref = safe_pipe_capture(qw(git show-ref --heads));
        print "E cvs update: Updating .\n";
        for my $line (split '\n', $showref) {
            if ( $line =~ m% refs/heads/(.*)$% ) {
                print "E cvs update: New directory `$1'\n";
            }
        }
        print "ok\n";
        return 1;
    }


    # Grab a handle to the SQLite db and do any necessary updates
    my $updater = GITCVS::updater->new($state->{CVSROOT}, $state->{module}, $log);

    $updater->update();

    argsfromdir($updater);

    #$log->debug("update state : " . Dumper($state));

    my($repoDir);
    $repoDir=$state->{CVSROOT} . "/$state->{module}/$state->{prependdir}";

    my %seendirs = ();

    # foreach file specified on the command line ...
    foreach my $argsFilename ( @{$state->{args}} )
    {
        my $filename;
        $filename = filecleanup($argsFilename);

        $log->debug("Processing file $filename");

        # if we have a -C we should pretend we never saw modified stuff
        if ( exists ( $state->{opt}{C} ) )
        {
            delete $state->{entries}{$filename}{modified_hash};
            delete $state->{entries}{$filename}{modified_filename};
            $state->{entries}{$filename}{unchanged} = 1;
        }

        my $stickyInfo = resolveStickyInfo($filename,
                                           $state->{opt}{r},
                                           $state->{opt}{D},
                                           exists($state->{opt}{A}));
        my $meta = $updater->getmeta($filename, $stickyInfo);

        # If -p was given, "print" the contents of the requested revision.
        if ( exists ( $state->{opt}{p} ) ) {
            if ( defined ( $meta->{revision} ) ) {
                $log->info("Printing '$filename' revision " . $meta->{revision});

                transmitfile($meta->{filehash}, { print => 1 });
            }

            next;
        }

        # Directories:
        prepDirForOutput(
                dirname($argsFilename),
                $repoDir,
                ".",
                \%seendirs,
                "update",
                $state->{dirArgs} );

        my $wrev = revparse($filename);

	if ( ! defined $meta )
	{
	    $meta = {
	        name => $filename,
	        revision => '0',
	        filehash => 'added'
	    };
	    if($wrev ne "0")
	    {
	        $meta->{filehash}='deleted';
	    }
	}

        my $oldmeta = $meta;

        # If the working copy is an old revision, lets get that version too for comparison.
        my $oldWrev=$wrev;
        if(defined($oldWrev))
        {
            $oldWrev=~s/^-//;
            if($oldWrev ne $meta->{revision})
            {
                $oldmeta = $updater->getmeta($filename, $oldWrev);
            }
        }

        #$log->debug("Target revision is $meta->{revision}, current working revision is $wrev");

        # Files are up to date if the working copy and repo copy have the same revision,
        # and the working copy is unmodified _and_ the user hasn't specified -C
        next if ( defined ( $wrev )
                  and defined($meta->{revision})
                  and $wrev eq $meta->{revision}
                  and $state->{entries}{$filename}{unchanged}
                  and not exists ( $state->{opt}{C} ) );

        # If the working copy and repo copy have the same revision,
        # but the working copy is modified, tell the client it's modified
        if ( defined ( $wrev )
             and defined($meta->{revision})
             and $wrev eq $meta->{revision}
             and $wrev ne "0"
             and defined($state->{entries}{$filename}{modified_hash})
             and not exists ( $state->{opt}{C} ) )
        {
            $log->info("Tell the client the file is modified");
            print "MT text M \n";
            print "MT fname $filename\n";
            print "MT newline\n";
            next;
        }

        if ( $meta->{filehash} eq "deleted" && $wrev ne "0" )
        {
            # TODO: If it has been modified in the sandbox, error out
            #   with the appropriate message, rather than deleting a modified
            #   file.

            my ( $filepart, $dirpart ) = filenamesplit($filename,1);

            $log->info("Removing '$filename' from working copy (no longer in the repo)");

            print "E cvs update: `$filename' is no longer in the repository\n";
            # Don't want to actually _DO_ the update if -n specified
            unless ( $state->{globaloptions}{-n} ) {
		print "Removed $dirpart\n";
		print "$filepart\n";
	    }
        }
        elsif ( not defined ( $state->{entries}{$filename}{modified_hash} )
		or $state->{entries}{$filename}{modified_hash} eq $oldmeta->{filehash}
		or $meta->{filehash} eq 'added' )
        {
            # normal update, just send the new revision (either U=Update,
            # or A=Add, or R=Remove)
	    if ( defined($wrev) && ($wrev=~/^-/) )
	    {
	        $log->info("Tell the client the file is scheduled for removal");
		print "MT text R \n";
                print "MT fname $filename\n";
                print "MT newline\n";
		next;
	    }
	    elsif ( (!defined($wrev) || $wrev eq '0') &&
                    (!defined($meta->{revision}) || $meta->{revision} eq '0') )
	    {
	        $log->info("Tell the client the file is scheduled for addition");
		print "MT text A \n";
                print "MT fname $filename\n";
                print "MT newline\n";
		next;

	    }
	    else {
                $log->info("UpdatingX3 '$filename' to ".$meta->{revision});
                print "MT +updated\n";
                print "MT text U \n";
                print "MT fname $filename\n";
                print "MT newline\n";
		print "MT -updated\n";
	    }

            my ( $filepart, $dirpart ) = filenamesplit($filename,1);

	    # Don't want to actually _DO_ the update if -n specified
	    unless ( $state->{globaloptions}{-n} )
	    {
		if ( defined ( $wrev ) )
		{
		    # instruct client we're sending a file to put in this path as a replacement
		    print "Update-existing $dirpart\n";
		    $log->debug("Updating existing file 'Update-existing $dirpart'");
		} else {
		    # instruct client we're sending a file to put in this path as a new file

		    $log->debug("Creating new file 'Created $dirpart'");
		    print "Created $dirpart\n";
		}
		print $state->{CVSROOT} . "/$state->{module}/$filename\n";

		# this is an "entries" line
		my $kopts = kopts_from_path($filename,"sha1",$meta->{filehash});
                my $entriesLine = "/$filepart/$meta->{revision}//$kopts/";
                $entriesLine .= getStickyTagOrDate($stickyInfo);
		$log->debug($entriesLine);
		print "$entriesLine\n";

		# permissions
		$log->debug("SEND : u=$meta->{mode},g=$meta->{mode},o=$meta->{mode}");
		print "u=$meta->{mode},g=$meta->{mode},o=$meta->{mode}\n";

		# transmit file
		transmitfile($meta->{filehash});
	    }
        } else {
            my ( $filepart, $dirpart ) = filenamesplit($meta->{name},1);

            my $mergeDir = setupTmpDir();

            my $file_local = $filepart . ".mine";
            my $mergedFile = "$mergeDir/$file_local";
            system("ln","-s",$state->{entries}{$filename}{modified_filename}, $file_local);
            my $file_old = $filepart . "." . $oldmeta->{revision};
            transmitfile($oldmeta->{filehash}, { targetfile => $file_old });
            my $file_new = $filepart . "." . $meta->{revision};
            transmitfile($meta->{filehash}, { targetfile => $file_new });

            # we need to merge with the local changes ( M=successful merge, C=conflict merge )
            $log->info("Merging $file_local, $file_old, $file_new");
            print "M Merging differences between $oldmeta->{revision} and $meta->{revision} into $filename\n";

            $log->debug("Temporary directory for merge is $mergeDir");

            my $return = system("git", "merge-file", $file_local, $file_old, $file_new);
            $return >>= 8;

            cleanupTmpDir();

            if ( $return == 0 )
            {
                $log->info("Merged successfully");
                print "M M $filename\n";
                $log->debug("Merged $dirpart");

                # Don't want to actually _DO_ the update if -n specified
                unless ( $state->{globaloptions}{-n} )
                {
                    print "Merged $dirpart\n";
                    $log->debug($state->{CVSROOT} . "/$state->{module}/$filename");
                    print $state->{CVSROOT} . "/$state->{module}/$filename\n";
                    my $kopts = kopts_from_path("$dirpart/$filepart",
                                                "file",$mergedFile);
                    $log->debug("/$filepart/$meta->{revision}//$kopts/");
                    my $entriesLine="/$filepart/$meta->{revision}//$kopts/";
                    $entriesLine .= getStickyTagOrDate($stickyInfo);
                    print "$entriesLine\n";
                }
            }
            elsif ( $return == 1 )
            {
                $log->info("Merged with conflicts");
                print "E cvs update: conflicts found in $filename\n";
                print "M C $filename\n";

                # Don't want to actually _DO_ the update if -n specified
                unless ( $state->{globaloptions}{-n} )
                {
                    print "Merged $dirpart\n";
                    print $state->{CVSROOT} . "/$state->{module}/$filename\n";
                    my $kopts = kopts_from_path("$dirpart/$filepart",
                                                "file",$mergedFile);
                    my $entriesLine = "/$filepart/$meta->{revision}/+/$kopts/";
                    $entriesLine .= getStickyTagOrDate($stickyInfo);
                    print "$entriesLine\n";
                }
            }
            else
            {
                $log->warn("Merge failed");
                next;
            }

            # Don't want to actually _DO_ the update if -n specified
            unless ( $state->{globaloptions}{-n} )
            {
                # permissions
                $log->debug("SEND : u=$meta->{mode},g=$meta->{mode},o=$meta->{mode}");
                print "u=$meta->{mode},g=$meta->{mode},o=$meta->{mode}\n";

                # transmit file, format is single integer on a line by itself (file
                # size) followed by the file contents
                # TODO : we should copy files in blocks
                my $data = safe_pipe_capture('cat', $mergedFile);
                $log->debug("File size : " . length($data));
                print length($data) . "\n";
                print $data;
            }
        }

    }

    # prepDirForOutput() any other existing directories unless they already
    # have the right sticky tag:
    unless ( $state->{globaloptions}{n} )
    {
        my $dir;
        foreach $dir (keys(%{$state->{dirMap}}))
        {
            if( ! $seendirs{$dir} &&
                exists($state->{dirArgs}{$dir}) )
            {
                my($oldTag);
                $oldTag=$state->{dirMap}{$dir}{tagspec};

                unless( ( exists($state->{opt}{A}) &&
                          defined($oldTag) ) ||
                          ( defined($state->{opt}{r}) &&
                            ( !defined($oldTag) ||
                              $state->{opt}{r} ne $oldTag ) ) )
                        # TODO?: OR sticky dir is different...
                {
                    next;
                }

                prepDirForOutput(
                        $dir,
                        $repoDir,
                        ".",
                        \%seendirs,
                        'update',
                        $state->{dirArgs} );
            }

            # TODO?: Consider sending a final duplicate Sticky response
            #   to more closely mimic real CVS.
        }
    }

    print "ok\n";
}

sub req_ci
{
    my ( $cmd, $data ) = @_;

    argsplit("ci");

    #$log->debug("State : " . Dumper($state));

    $log->info("req_ci : " . ( defined($data) ? $data : "[NULL]" ));

    if ( $state->{method} eq 'pserver' and $state->{user} eq 'anonymous' )
    {
        print "error 1 anonymous user cannot commit via pserver\n";
        cleanupWorkTree();
        exit;
    }

    if ( -e $state->{CVSROOT} . "/index" )
    {
        $log->warn("file 'index' already exists in the git repository");
        print "error 1 Index already exists in git repo\n";
        cleanupWorkTree();
        exit;
    }

    # Grab a handle to the SQLite db and do any necessary updates
    my $updater = GITCVS::updater->new($state->{CVSROOT}, $state->{module}, $log);
    $updater->update();

    my @committedfiles = ();
    my %oldmeta;
    my $stickyInfo;
    my $branchRef;
    my $parenthash;

    # foreach file specified on the command line ...
    foreach my $filename ( @{$state->{args}} )
    {
        my $committedfile = $filename;
        $filename = filecleanup($filename);

        next unless ( exists $state->{entries}{$filename}{modified_filename} or not $state->{entries}{$filename}{unchanged} );

        #####
        # Figure out which branch and parenthash we are committing
        # to, and setup worktree:

        # should always come from entries:
        my $fileStickyInfo = resolveStickyInfo($filename);
        if( !defined($branchRef) )
        {
            $stickyInfo = $fileStickyInfo;
            if( defined($stickyInfo) &&
                ( defined($stickyInfo->{date}) ||
                  !defined($stickyInfo->{tag}) ) )
            {
                print "error 1 cannot commit with sticky date for file `$filename'\n";
                cleanupWorkTree();
                exit;
            }

            $branchRef = "refs/heads/$state->{module}";
            if ( defined($stickyInfo) && defined($stickyInfo->{tag}) )
            {
                $branchRef = "refs/heads/$stickyInfo->{tag}";
            }

            $parenthash = safe_pipe_capture('git', 'show-ref', '-s', $branchRef);
            chomp $parenthash;
            if ($parenthash !~ /^[0-9a-f]{$state->{hexsz}}$/)
            {
                if ( defined($stickyInfo) && defined($stickyInfo->{tag}) )
                {
                    print "error 1 sticky tag `$stickyInfo->{tag}' for file `$filename' is not a branch\n";
                }
                else
                {
                    print "error 1 pserver cannot find the current HEAD of module";
                }
                cleanupWorkTree();
                exit;
            }

            setupWorkTree($parenthash);

            $log->info("Lockless commit start, basing commit on '$work->{workDir}', index file is '$work->{index}'");

            $log->info("Created index '$work->{index}' for head $state->{module} - exit status $?");
        }
        elsif( !refHashEqual($stickyInfo,$fileStickyInfo) )
        {
            #TODO: We could split the cvs commit into multiple
            #  git commits by distinct stickyTag values, but that
            #  is lowish priority.
            print "error 1 Committing different files to different"
                  . " branches is not currently supported\n";
            cleanupWorkTree();
            exit;
        }

        #####
        # Process this file:

        my $meta = $updater->getmeta($filename,$stickyInfo);
	$oldmeta{$filename} = $meta;

        my $wrev = revparse($filename);

        my ( $filepart, $dirpart ) = filenamesplit($filename);

	# do a checkout of the file if it is part of this tree
        if ($wrev) {
            system('git', 'checkout-index', '-f', '-u', $filename);
            unless ($? == 0) {
                die "Error running git-checkout-index -f -u $filename : $!";
            }
        }

        my $addflag = 0;
        my $rmflag = 0;
        $rmflag = 1 if ( defined($wrev) and ($wrev=~/^-/) );
        $addflag = 1 unless ( -e $filename );

        # Do up to date checking
        unless ( $addflag or $wrev eq $meta->{revision} or
                 ( $rmflag and $wrev eq "-$meta->{revision}" ) )
        {
            # fail everything if an up to date check fails
            print "error 1 Up to date check failed for $filename\n";
            cleanupWorkTree();
            exit;
        }

        push @committedfiles, $committedfile;
        $log->info("Committing $filename");

        system("mkdir","-p",$dirpart) unless ( -d $dirpart );

        unless ( $rmflag )
        {
            $log->debug("rename $state->{entries}{$filename}{modified_filename} $filename");
            rename $state->{entries}{$filename}{modified_filename},$filename;

            # Calculate modes to remove
            my $invmode = "";
            foreach ( qw (r w x) ) { $invmode .= $_ unless ( $state->{entries}{$filename}{modified_mode} =~ /$_/ ); }

            $log->debug("chmod u+" . $state->{entries}{$filename}{modified_mode} . "-" . $invmode . " $filename");
            system("chmod","u+" .  $state->{entries}{$filename}{modified_mode} . "-" . $invmode, $filename);
        }

        if ( $rmflag )
        {
            $log->info("Removing file '$filename'");
            unlink($filename);
            system("git", "update-index", "--remove", $filename);
        }
        elsif ( $addflag )
        {
            $log->info("Adding file '$filename'");
            system("git", "update-index", "--add", $filename);
        } else {
            $log->info("UpdatingX2 file '$filename'");
            system("git", "update-index", $filename);
        }
    }

    unless ( scalar(@committedfiles) > 0 )
    {
        print "E No files to commit\n";
        print "ok\n";
        cleanupWorkTree();
        return;
    }

    my $treehash = safe_pipe_capture(qw(git write-tree));
    chomp $treehash;

    $log->debug("Treehash : $treehash, Parenthash : $parenthash");

    # write our commit message out if we have one ...
    my ( $msg_fh, $msg_filename ) = tempfile( DIR => $TEMP_DIR );
    print $msg_fh $state->{opt}{m};# if ( exists ( $state->{opt}{m} ) );
    if ( defined ( $cfg->{gitcvs}{commitmsgannotation} ) ) {
        if ($cfg->{gitcvs}{commitmsgannotation} !~ /^\s*$/ ) {
            print $msg_fh "\n\n".$cfg->{gitcvs}{commitmsgannotation}."\n"
        }
    } else {
        print $msg_fh "\n\nvia git-CVS emulator\n";
    }
    close $msg_fh;

    my $commithash = safe_pipe_capture('git', 'commit-tree', $treehash, '-p', $parenthash, '-F', $msg_filename);
    chomp($commithash);
    $log->info("Commit hash : $commithash");

    unless ( $commithash =~ /[a-zA-Z0-9]{$state->{hexsz}}/ )
    {
        $log->warn("Commit failed (Invalid commit hash)");
        print "error 1 Commit failed (unknown reason)\n";
        cleanupWorkTree();
        exit;
    }

	### Emulate git-receive-pack by running hooks/update
	my @hook = ( $ENV{GIT_DIR}.'hooks/update', $branchRef,
			$parenthash, $commithash );
	if( -x $hook[0] ) {
		unless( system( @hook ) == 0 )
		{
			$log->warn("Commit failed (update hook declined to update ref)");
			print "error 1 Commit failed (update hook declined)\n";
			cleanupWorkTree();
			exit;
		}
	}

	### Update the ref
	if (system(qw(git update-ref -m), "cvsserver ci",
			$branchRef, $commithash, $parenthash)) {
		$log->warn("update-ref for $state->{module} failed.");
		print "error 1 Cannot commit -- update first\n";
		cleanupWorkTree();
		exit;
	}

	### Emulate git-receive-pack by running hooks/post-receive
	my $hook = $ENV{GIT_DIR}.'hooks/post-receive';
	if( -x $hook ) {
		open(my $pipe, "| $hook") || die "can't fork $!";

		local $SIG{PIPE} = sub { die 'pipe broke' };

		print $pipe "$parenthash $commithash $branchRef\n";

		close $pipe || die "bad pipe: $! $?";
	}

    $updater->update();

	### Then hooks/post-update
	$hook = $ENV{GIT_DIR}.'hooks/post-update';
	if (-x $hook) {
		system($hook, $branchRef);
	}

    # foreach file specified on the command line ...
    foreach my $filename ( @committedfiles )
    {
        $filename = filecleanup($filename);

        my $meta = $updater->getmeta($filename,$stickyInfo);
	unless (defined $meta->{revision}) {
	  $meta->{revision} = "1.1";
	}

        my ( $filepart, $dirpart ) = filenamesplit($filename, 1);

        $log->debug("Checked-in $dirpart : $filename");

	print "M $state->{CVSROOT}/$state->{module}/$filename,v  <--  $dirpart$filepart\n";
        if ( defined $meta->{filehash} && $meta->{filehash} eq "deleted" )
        {
            print "M new revision: delete; previous revision: $oldmeta{$filename}{revision}\n";
            print "Remove-entry $dirpart\n";
            print "$filename\n";
        } else {
            if ($meta->{revision} eq "1.1") {
	        print "M initial revision: 1.1\n";
            } else {
	        print "M new revision: $meta->{revision}; previous revision: $oldmeta{$filename}{revision}\n";
            }
            print "Checked-in $dirpart\n";
            print "$filename\n";
            my $kopts = kopts_from_path($filename,"sha1",$meta->{filehash});
            print "/$filepart/$meta->{revision}//$kopts/" .
                  getStickyTagOrDate($stickyInfo) . "\n";
        }
    }

    cleanupWorkTree();
    print "ok\n";
}

sub req_status
{
    my ( $cmd, $data ) = @_;

    argsplit("status");

    $log->info("req_status : " . ( defined($data) ? $data : "[NULL]" ));
    #$log->debug("status state : " . Dumper($state));

    # Grab a handle to the SQLite db and do any necessary updates
    my $updater;
    $updater = GITCVS::updater->new($state->{CVSROOT}, $state->{module}, $log);
    $updater->update();

    # if no files were specified, we need to work out what files we should
    # be providing status on ...
    argsfromdir($updater);

    # foreach file specified on the command line ...
    foreach my $filename ( @{$state->{args}} )
    {
        $filename = filecleanup($filename);

        if ( exists($state->{opt}{l}) &&
             index($filename, '/', length($state->{prependdir})) >= 0 )
        {
           next;
        }

        my $wrev = revparse($filename);

        my $stickyInfo = resolveStickyInfo($filename);
        my $meta = $updater->getmeta($filename,$stickyInfo);
        my $oldmeta = $meta;

        # If the working copy is an old revision, lets get that
        # version too for comparison.
        if ( defined($wrev) and $wrev ne $meta->{revision} )
        {
            my($rmRev)=$wrev;
            $rmRev=~s/^-//;
            $oldmeta = $updater->getmeta($filename, $rmRev);
        }

        # TODO : All possible statuses aren't yet implemented
        my $status;
        # Files are up to date if the working copy and repo copy have
        # the same revision, and the working copy is unmodified
        if ( defined ( $wrev ) and defined($meta->{revision}) and
             $wrev eq $meta->{revision} and
             ( ( $state->{entries}{$filename}{unchanged} and
                 ( not defined ( $state->{entries}{$filename}{conflict} ) or
                   $state->{entries}{$filename}{conflict} !~ /^\+=/ ) ) or
               ( defined($state->{entries}{$filename}{modified_hash}) and
                 $state->{entries}{$filename}{modified_hash} eq
                        $meta->{filehash} ) ) )
        {
            $status = "Up-to-date"
        }

        # Need checkout if the working copy has a different (usually
        # older) revision than the repo copy, and the working copy is
        # unmodified
        if ( defined ( $wrev ) and defined ( $meta->{revision} ) and
             $meta->{revision} ne $wrev and
             ( $state->{entries}{$filename}{unchanged} or
               ( defined($state->{entries}{$filename}{modified_hash}) and
                 $state->{entries}{$filename}{modified_hash} eq
                                $oldmeta->{filehash} ) ) )
        {
            $status ||= "Needs Checkout";
        }

        # Need checkout if it exists in the repo but doesn't have a working
        # copy
        if ( not defined ( $wrev ) and defined ( $meta->{revision} ) )
        {
            $status ||= "Needs Checkout";
        }

        # Locally modified if working copy and repo copy have the
        # same revision but there are local changes
        if ( defined ( $wrev ) and defined($meta->{revision}) and
             $wrev eq $meta->{revision} and
             $wrev ne "0" and
             $state->{entries}{$filename}{modified_filename} )
        {
            $status ||= "Locally Modified";
        }

        # Needs Merge if working copy revision is different
        # (usually older) than repo copy and there are local changes
        if ( defined ( $wrev ) and defined ( $meta->{revision} ) and
             $meta->{revision} ne $wrev and
             $state->{entries}{$filename}{modified_filename} )
        {
            $status ||= "Needs Merge";
        }

        if ( defined ( $state->{entries}{$filename}{revision} ) and
             ( !defined($meta->{revision}) ||
               $meta->{revision} eq "0" ) )
        {
            $status ||= "Locally Added";
        }
        if ( defined ( $wrev ) and defined ( $meta->{revision} ) and
             $wrev eq "-$meta->{revision}" )
        {
            $status ||= "Locally Removed";
        }
        if ( defined ( $state->{entries}{$filename}{conflict} ) and
             $state->{entries}{$filename}{conflict} =~ /^\+=/ )
        {
            $status ||= "Unresolved Conflict";
        }
        if ( 0 )
        {
            $status ||= "File had conflicts on merge";
        }

        $status ||= "Unknown";

        my ($filepart) = filenamesplit($filename);

        print "M =======" . ( "=" x 60 ) . "\n";
        print "M File: $filepart\tStatus: $status\n";
        if ( defined($state->{entries}{$filename}{revision}) )
        {
            print "M Working revision:\t" .
                  $state->{entries}{$filename}{revision} . "\n";
        } else {
            print "M Working revision:\tNo entry for $filename\n";
        }
        if ( defined($meta->{revision}) )
        {
            print "M Repository revision:\t" .
                   $meta->{revision} .
                   "\t$state->{CVSROOT}/$state->{module}/$filename,v\n";
            my($tagOrDate)=$state->{entries}{$filename}{tag_or_date};
            my($tag)=($tagOrDate=~m/^T(.+)$/);
            if( !defined($tag) )
            {
                $tag="(none)";
            }
            print "M Sticky Tag:\t\t$tag\n";
            my($date)=($tagOrDate=~m/^D(.+)$/);
            if( !defined($date) )
            {
                $date="(none)";
            }
            print "M Sticky Date:\t\t$date\n";
            my($options)=$state->{entries}{$filename}{options};
            if( $options eq "" )
            {
                $options="(none)";
            }
            print "M Sticky Options:\t\t$options\n";
        } else {
            print "M Repository revision:\tNo revision control file\n";
        }
        print "M\n";
    }

    print "ok\n";
}

sub req_diff
{
    my ( $cmd, $data ) = @_;

    argsplit("diff");

    $log->debug("req_diff : " . ( defined($data) ? $data : "[NULL]" ));
    #$log->debug("status state : " . Dumper($state));

    my ($revision1, $revision2);
    if ( defined ( $state->{opt}{r} ) and ref $state->{opt}{r} eq "ARRAY" )
    {
        $revision1 = $state->{opt}{r}[0];
        $revision2 = $state->{opt}{r}[1];
    } else {
        $revision1 = $state->{opt}{r};
    }

    $log->debug("Diffing revisions " .
                ( defined($revision1) ? $revision1 : "[NULL]" ) .
                " and " . ( defined($revision2) ? $revision2 : "[NULL]" ) );

    # Grab a handle to the SQLite db and do any necessary updates
    my $updater;
    $updater = GITCVS::updater->new($state->{CVSROOT}, $state->{module}, $log);
    $updater->update();

    # if no files were specified, we need to work out what files we should
    # be providing status on ...
    argsfromdir($updater);

    my($foundDiff);

    # foreach file specified on the command line ...
    foreach my $argFilename ( @{$state->{args}} )
    {
        my($filename) = filecleanup($argFilename);

        my ( $fh, $file1, $file2, $meta1, $meta2, $filediff );

        my $wrev = revparse($filename);

        # Priority for revision1:
        #  1. First -r (missing file: check -N)
        #  2. wrev from client's Entry line
        #      - missing line/file: check -N
        #      - "0": added file not committed (empty contents for rev1)
        #      - Prefixed with dash (to be removed): check -N

        if ( defined ( $revision1 ) )
        {
            $meta1 = $updater->getmeta($filename, $revision1);
        }
        elsif( defined($wrev) && $wrev ne "0" )
        {
            my($rmRev)=$wrev;
            $rmRev=~s/^-//;
            $meta1 = $updater->getmeta($filename, $rmRev);
        }
        if ( !defined($meta1) ||
             $meta1->{filehash} eq "deleted" )
        {
            if( !exists($state->{opt}{N}) )
            {
                if(!defined($revision1))
                {
                    print "E File $filename at revision $revision1 doesn't exist\n";
                }
                next;
            }
            elsif( !defined($meta1) )
            {
                $meta1 = {
                    name => $filename,
                    revision => '0',
                    filehash => 'deleted'
                };
            }
        }

        # Priority for revision2:
        #  1. Second -r (missing file: check -N)
        #  2. Modified file contents from client
        #  3. wrev from client's Entry line
        #      - missing line/file: check -N
        #      - Prefixed with dash (to be removed): check -N

        # if we have a second -r switch, use it too
        if ( defined ( $revision2 ) )
        {
            $meta2 = $updater->getmeta($filename, $revision2);
        }
        elsif(defined($state->{entries}{$filename}{modified_filename}))
        {
            $file2 = $state->{entries}{$filename}{modified_filename};
	    $meta2 = {
                name => $filename,
	        revision => '0',
	        filehash => 'modified'
            };
        }
        elsif( defined($wrev) && ($wrev!~/^-/) )
        {
            if(!defined($revision1))  # no revision and no modifications:
            {
                next;
            }
            $meta2 = $updater->getmeta($filename, $wrev);
        }
        if(!defined($file2))
        {
            if ( !defined($meta2) ||
                 $meta2->{filehash} eq "deleted" )
            {
                if( !exists($state->{opt}{N}) )
                {
                    if(!defined($revision2))
                    {
                        print "E File $filename at revision $revision2 doesn't exist\n";
                    }
                    next;
                }
                elsif( !defined($meta2) )
                {
	            $meta2 = {
                        name => $filename,
	                revision => '0',
	                filehash => 'deleted'
                    };
                }
            }
        }

        if( $meta1->{filehash} eq $meta2->{filehash} )
        {
            $log->info("unchanged $filename");
            next;
        }

        # Retrieve revision contents:
        ( undef, $file1 ) = tempfile( DIR => $TEMP_DIR, OPEN => 0 );
        transmitfile($meta1->{filehash}, { targetfile => $file1 });

        if(!defined($file2))
        {
            ( undef, $file2 ) = tempfile( DIR => $TEMP_DIR, OPEN => 0 );
            transmitfile($meta2->{filehash}, { targetfile => $file2 });
        }

        # Generate the actual diff:
        print "M Index: $argFilename\n";
        print "M =======" . ( "=" x 60 ) . "\n";
        print "M RCS file: $state->{CVSROOT}/$state->{module}/$filename,v\n";
        if ( defined ( $meta1 ) && $meta1->{revision} ne "0" )
        {
            print "M retrieving revision $meta1->{revision}\n"
        }
        if ( defined ( $meta2 ) && $meta2->{revision} ne "0" )
        {
            print "M retrieving revision $meta2->{revision}\n"
        }
        print "M diff ";
        foreach my $opt ( sort keys %{$state->{opt}} )
        {
            if ( ref $state->{opt}{$opt} eq "ARRAY" )
            {
                foreach my $value ( @{$state->{opt}{$opt}} )
                {
                    print "-$opt $value ";
                }
            } else {
                print "-$opt ";
                if ( defined ( $state->{opt}{$opt} ) )
                {
                    print "$state->{opt}{$opt} "
                }
            }
        }
        print "$argFilename\n";

        $log->info("Diffing $filename -r $meta1->{revision} -r " .
                   ( $meta2->{revision} or "workingcopy" ));

        # TODO: Use --label instead of -L because -L is no longer
        #  documented and may go away someday.  Not sure if there are
        #  versions that only support -L, which would make this change risky?
        #  http://osdir.com/ml/bug-gnu-utils-gnu/2010-12/msg00060.html
        #    ("man diff" should actually document the best migration strategy,
        #  [current behavior, future changes, old compatibility issues
        #  or lack thereof, etc], not just stop mentioning the option...)
        # TODO: Real CVS seems to include a date in the label, before
        #  the revision part, without the keyword "revision".  The following
        #  has minimal changes compared to original versions of
        #  git-cvsserver.perl.  (Mostly tab vs space after filename.)

        my (@diffCmd) = ( 'diff' );
        if ( exists($state->{opt}{N}) )
        {
            push @diffCmd,"-N";
        }
        if ( exists $state->{opt}{u} )
        {
            push @diffCmd,("-u","-L");
            if( $meta1->{filehash} eq "deleted" )
            {
                push @diffCmd,"/dev/null";
            } else {
                push @diffCmd,("$argFilename\trevision $meta1->{revision}");
            }

            if( defined($meta2->{filehash}) )
            {
                if( $meta2->{filehash} eq "deleted" )
                {
                    push @diffCmd,("-L","/dev/null");
                } else {
                    push @diffCmd,("-L",
                                   "$argFilename\trevision $meta2->{revision}");
                }
            } else {
                push @diffCmd,("-L","$argFilename\tworking copy");
            }
        }
        push @diffCmd,($file1,$file2);
        if(!open(DIFF,"-|",@diffCmd))
        {
            $log->warn("Unable to run diff: $!");
        }
        my($diffLine);
        while(defined($diffLine=<DIFF>))
        {
            print "M $diffLine";
            $foundDiff=1;
        }
        close(DIFF);
    }

    if($foundDiff)
    {
        print "error  \n";
    }
    else
    {
        print "ok\n";
    }
}

sub req_log
{
    my ( $cmd, $data ) = @_;

    argsplit("log");

    $log->debug("req_log : " . ( defined($data) ? $data : "[NULL]" ));
    #$log->debug("log state : " . Dumper($state));

    my ( $revFilter );
    if ( defined ( $state->{opt}{r} ) )
    {
        $revFilter = $state->{opt}{r};
    }

    # Grab a handle to the SQLite db and do any necessary updates
    my $updater;
    $updater = GITCVS::updater->new($state->{CVSROOT}, $state->{module}, $log);
    $updater->update();

    # if no files were specified, we need to work out what files we
    # should be providing status on ...
    argsfromdir($updater);

    # foreach file specified on the command line ...
    foreach my $filename ( @{$state->{args}} )
    {
        $filename = filecleanup($filename);

        my $headmeta = $updater->getmeta($filename);

        my ($revisions,$totalrevisions) = $updater->getlog($filename,
                                                           $revFilter);

        next unless ( scalar(@$revisions) );

        print "M \n";
        print "M RCS file: $state->{CVSROOT}/$state->{module}/$filename,v\n";
        print "M Working file: $filename\n";
        print "M head: $headmeta->{revision}\n";
        print "M branch:\n";
        print "M locks: strict\n";
        print "M access list:\n";
        print "M symbolic names:\n";
        print "M keyword substitution: kv\n";
        print "M total revisions: $totalrevisions;\tselected revisions: " .
              scalar(@$revisions) . "\n";
        print "M description:\n";

        foreach my $revision ( @$revisions )
        {
            print "M ----------------------------\n";
            print "M revision $revision->{revision}\n";
            # reformat the date for log output
            if ( $revision->{modified} =~ /(\d+)\s+(\w+)\s+(\d+)\s+(\S+)/ and
                 defined($DATE_LIST->{$2}) )
            {
                $revision->{modified} = sprintf('%04d/%02d/%02d %s',
                                            $3, $DATE_LIST->{$2}, $1, $4 );
            }
            $revision->{author} = cvs_author($revision->{author});
            print "M date: $revision->{modified};" .
                  "  author: $revision->{author};  state: " .
                  ( $revision->{filehash} eq "deleted" ? "dead" : "Exp" ) .
                  ";  lines: +2 -3\n";
            my $commitmessage;
            $commitmessage = $updater->commitmessage($revision->{commithash});
            $commitmessage =~ s/^/M /mg;
            print $commitmessage . "\n";
        }
        print "M =======" . ( "=" x 70 ) . "\n";
    }

    print "ok\n";
}

sub req_annotate
{
    my ( $cmd, $data ) = @_;

    argsplit("annotate");

    $log->info("req_annotate : " . ( defined($data) ? $data : "[NULL]" ));
    #$log->debug("status state : " . Dumper($state));

    # Grab a handle to the SQLite db and do any necessary updates
    my $updater = GITCVS::updater->new($state->{CVSROOT}, $state->{module}, $log);
    $updater->update();

    # if no files were specified, we need to work out what files we should be providing annotate on ...
    argsfromdir($updater);

    # we'll need a temporary checkout dir
    setupWorkTree();

    $log->info("Temp checkoutdir creation successful, basing annotate session work on '$work->{workDir}', index file is '$ENV{GIT_INDEX_FILE}'");

    # foreach file specified on the command line ...
    foreach my $filename ( @{$state->{args}} )
    {
        $filename = filecleanup($filename);

        my $meta = $updater->getmeta($filename);

        next unless ( $meta->{revision} );

	# get all the commits that this file was in
	# in dense format -- aka skip dead revisions
        my $revisions   = $updater->gethistorydense($filename);
	my $lastseenin  = $revisions->[0][2];

	# populate the temporary index based on the latest commit were we saw
	# the file -- but do it cheaply without checking out any files
	# TODO: if we got a revision from the client, use that instead
	# to look up the commithash in sqlite (still good to default to
	# the current head as we do now)
	system("git", "read-tree", $lastseenin);
	unless ($? == 0)
	{
	    print "E error running git-read-tree $lastseenin $ENV{GIT_INDEX_FILE} $!\n";
	    return;
	}
	$log->info("Created index '$ENV{GIT_INDEX_FILE}' with commit $lastseenin - exit status $?");

        # do a checkout of the file
        system('git', 'checkout-index', '-f', '-u', $filename);
        unless ($? == 0) {
            print "E error running git-checkout-index -f -u $filename : $!\n";
            return;
        }

        $log->info("Annotate $filename");

        # Prepare a file with the commits from the linearized
        # history that annotate should know about. This prevents
        # git-jsannotate telling us about commits we are hiding
        # from the client.

        my $a_hints = "$work->{workDir}/.annotate_hints";
        if (!open(ANNOTATEHINTS, '>', $a_hints)) {
            print "E failed to open '$a_hints' for writing: $!\n";
            return;
        }
        for (my $i=0; $i < @$revisions; $i++)
        {
            print ANNOTATEHINTS $revisions->[$i][2];
            if ($i+1 < @$revisions) { # have we got a parent?
                print ANNOTATEHINTS ' ' . $revisions->[$i+1][2];
            }
            print ANNOTATEHINTS "\n";
        }

        print ANNOTATEHINTS "\n";
        close ANNOTATEHINTS
            or (print "E failed to write $a_hints: $!\n"), return;

        my @cmd = (qw(git annotate -l -S), $a_hints, $filename);
        if (!open(ANNOTATE, "-|", @cmd)) {
            print "E error invoking ". join(' ',@cmd) .": $!\n";
            return;
        }
        my $metadata = {};
        print "E Annotations for $filename\n";
        print "E ***************\n";
        while ( <ANNOTATE> )
        {
            if (m/^([a-zA-Z0-9]{$state->{hexsz}})\t\([^\)]*\)(.*)$/i)
            {
                my $commithash = $1;
                my $data = $2;
                unless ( defined ( $metadata->{$commithash} ) )
                {
                    $metadata->{$commithash} = $updater->getmeta($filename, $commithash);
                    $metadata->{$commithash}{author} = cvs_author($metadata->{$commithash}{author});
                    $metadata->{$commithash}{modified} = sprintf("%02d-%s-%02d", $1, $2, $3) if ( $metadata->{$commithash}{modified} =~ /^(\d+)\s(\w+)\s\d\d(\d\d)/ );
                }
                printf("M %-7s      (%-8s %10s): %s\n",
                    $metadata->{$commithash}{revision},
                    $metadata->{$commithash}{author},
                    $metadata->{$commithash}{modified},
                    $data
                );
            } else {
                $log->warn("Error in annotate output! LINE: $_");
                print "E Annotate error \n";
                next;
            }
        }
        close ANNOTATE;
    }

    # done; get out of the tempdir
    cleanupWorkTree();

    print "ok\n";

}

# This method takes the state->{arguments} array and produces two new arrays.
# The first is $state->{args} which is everything before the '--' argument, and
# the second is $state->{files} which is everything after it.
sub argsplit
{
    $state->{args} = [];
    $state->{files} = [];
    $state->{opt} = {};

    return unless( defined($state->{arguments}) and ref $state->{arguments} eq "ARRAY" );

    my $type = shift;

    if ( defined($type) )
    {
        my $opt = {};
        $opt = { A => 0, N => 0, P => 0, R => 0, c => 0, f => 0, l => 0, n => 0, p => 0, s => 0, r => 1, D => 1, d => 1, k => 1, j => 1, } if ( $type eq "co" );
        $opt = { v => 0, l => 0, R => 0 } if ( $type eq "status" );
        $opt = { A => 0, P => 0, C => 0, d => 0, f => 0, l => 0, R => 0, p => 0, k => 1, r => 1, D => 1, j => 1, I => 1, W => 1 } if ( $type eq "update" );
        $opt = { l => 0, R => 0, k => 1, D => 1, D => 1, r => 2, N => 0 } if ( $type eq "diff" );
        $opt = { c => 0, R => 0, l => 0, f => 0, F => 1, m => 1, r => 1 } if ( $type eq "ci" );
        $opt = { k => 1, m => 1 } if ( $type eq "add" );
        $opt = { f => 0, l => 0, R => 0 } if ( $type eq "remove" );
        $opt = { l => 0, b => 0, h => 0, R => 0, t => 0, N => 0, S => 0, r => 1, d => 1, s => 1, w => 1 } if ( $type eq "log" );


        while ( scalar ( @{$state->{arguments}} ) > 0 )
        {
            my $arg = shift @{$state->{arguments}};

            next if ( $arg eq "--" );
            next unless ( $arg =~ /\S/ );

            # if the argument looks like a switch
            if ( $arg =~ /^-(\w)(.*)/ )
            {
                # if it's a switch that takes an argument
                if ( $opt->{$1} )
                {
                    # If this switch has already been provided
                    if ( $opt->{$1} > 1 and exists ( $state->{opt}{$1} ) )
                    {
                        $state->{opt}{$1} = [ $state->{opt}{$1} ];
                        if ( length($2) > 0 )
                        {
                            push @{$state->{opt}{$1}},$2;
                        } else {
                            push @{$state->{opt}{$1}}, shift @{$state->{arguments}};
                        }
                    } else {
                        # if there's extra data in the arg, use that as the argument for the switch
                        if ( length($2) > 0 )
                        {
                            $state->{opt}{$1} = $2;
                        } else {
                            $state->{opt}{$1} = shift @{$state->{arguments}};
                        }
                    }
                } else {
                    $state->{opt}{$1} = undef;
                }
            }
            else
            {
                push @{$state->{args}}, $arg;
            }
        }
    }
    else
    {
        my $mode = 0;

        foreach my $value ( @{$state->{arguments}} )
        {
            if ( $value eq "--" )
            {
                $mode++;
                next;
            }
            push @{$state->{args}}, $value if ( $mode == 0 );
            push @{$state->{files}}, $value if ( $mode == 1 );
        }
    }
}

# Used by argsfromdir
sub expandArg
{
    my ($updater,$outNameMap,$outDirMap,$path,$isDir) = @_;

    my $fullPath = filecleanup($path);

      # Is it a directory?
    if( defined($state->{dirMap}{$fullPath}) ||
        defined($state->{dirMap}{"$fullPath/"}) )
    {
          # It is a directory in the user's sandbox.
        $isDir=1;

        if(defined($state->{entries}{$fullPath}))
        {
            $log->fatal("Inconsistent file/dir type");
            die "Inconsistent file/dir type";
        }
    }
    elsif(defined($state->{entries}{$fullPath}))
    {
          # It is a file in the user's sandbox.
        $isDir=0;
    }
    my($revDirMap,$otherRevDirMap);
    if(!defined($isDir) || $isDir)
    {
          # Resolve version tree for sticky tag:
          # (for now we only want list of files for the version, not
          # particular versions of those files: assume it is a directory
          # for the moment; ignore Entry's stick tag)

          # Order of precedence of sticky tags:
          #    -A       [head]
          #    -r /tag/
          #    [file entry sticky tag, but that is only relevant to files]
          #    [the tag specified in dir req_Sticky]
          #    [the tag specified in a parent dir req_Sticky]
          #    [head]
          # Also, -r may appear twice (for diff).
          #
          # FUTURE: When/if -j (merges) are supported, we also
          #  need to add relevant files from one or two
          #  versions specified with -j.

        if(exists($state->{opt}{A}))
        {
            $revDirMap=$updater->getRevisionDirMap();
        }
        elsif( defined($state->{opt}{r}) and
               ref $state->{opt}{r} eq "ARRAY" )
        {
            $revDirMap=$updater->getRevisionDirMap($state->{opt}{r}[0]);
            $otherRevDirMap=$updater->getRevisionDirMap($state->{opt}{r}[1]);
        }
        elsif(defined($state->{opt}{r}))
        {
            $revDirMap=$updater->getRevisionDirMap($state->{opt}{r});
        }
        else
        {
            my($sticky)=getDirStickyInfo($fullPath);
            $revDirMap=$updater->getRevisionDirMap($sticky->{tag});
        }

          # Is it a directory?
        if( defined($revDirMap->{$fullPath}) ||
            defined($otherRevDirMap->{$fullPath}) )
        {
            $isDir=1;
        }
    }

      # What to do with it?
    if(!$isDir)
    {
        $outNameMap->{$fullPath}=1;
    }
    else
    {
        $outDirMap->{$fullPath}=1;

        if(defined($revDirMap->{$fullPath}))
        {
            addDirMapFiles($updater,$outNameMap,$outDirMap,
                           $revDirMap->{$fullPath});
        }
        if( defined($otherRevDirMap) &&
            defined($otherRevDirMap->{$fullPath}) )
        {
            addDirMapFiles($updater,$outNameMap,$outDirMap,
                           $otherRevDirMap->{$fullPath});
        }
    }
}

# Used by argsfromdir
# Add entries from dirMap to outNameMap.  Also recurse into entries
# that are subdirectories.
sub addDirMapFiles
{
    my($updater,$outNameMap,$outDirMap,$dirMap)=@_;

    my($fullName);
    foreach $fullName (keys(%$dirMap))
    {
        my $cleanName=$fullName;
        if(defined($state->{prependdir}))
        {
            if(!($cleanName=~s/^\Q$state->{prependdir}\E//))
            {
                $log->fatal("internal error stripping prependdir");
                die "internal error stripping prependdir";
            }
        }

        if($dirMap->{$fullName} eq "F")
        {
            $outNameMap->{$cleanName}=1;
        }
        elsif($dirMap->{$fullName} eq "D")
        {
            if(!$state->{opt}{l})
            {
                expandArg($updater,$outNameMap,$outDirMap,$cleanName,1);
            }
        }
        else
        {
            $log->fatal("internal error in addDirMapFiles");
            die "internal error in addDirMapFiles";
        }
    }
}

# This method replaces $state->{args} with a directory-expanded
# list of all relevant filenames (recursively unless -d), based
# on $state->{entries}, and the "current" list of files in
# each directory.  "Current" files as determined by
# either the requested (-r/-A) or "req_Sticky" version of
# that directory.
#    Both the input args and the new output args are relative
# to the cvs-client's CWD, although some of the internal
# computations are relative to the top of the project.
sub argsfromdir
{
    my $updater = shift;

    # Notes about requirements for specific callers:
    #   update # "standard" case (entries; a single -r/-A/default; -l)
    #          # Special case: -d for create missing directories.
    #   diff # 0 or 1 -r's: "standard" case.
    #        # 2 -r's: We could ignore entries (just use the two -r's),
    #        # but it doesn't really matter.
    #   annotate # "standard" case
    #   log # Punting: log -r has a more complex non-"standard"
    #       # meaning, and we don't currently try to support log'ing
    #       # branches at all (need a lot of work to
    #       # support CVS-consistent branch relative version
    #       # numbering).
#HERE: But we still want to expand directories.  Maybe we should
#  essentially force "-A".
    #   status # "standard", except that -r/-A/default are not possible.
    #          # Mostly only used to expand entries only)
    #
    # Don't use argsfromdir at all:
    #   add # Explicit arguments required.  Directory args imply add
    #       # the directory itself, not the files in it.
    #   co  # Obtain list directly.
    #   remove # HERE: TEST: MAYBE client does the recursion for us,
    #          # since it only makes sense to remove stuff already in
    #          # the sandbox?
    #   ci # HERE: Similar to remove...
    #      # Don't try to implement the confusing/weird
    #      # ci -r bug er.."feature".

    if(scalar(@{$state->{args}})==0)
    {
        $state->{args} = [ "." ];
    }
    my %allArgs;
    my %allDirs;
    for my $file (@{$state->{args}})
    {
        expandArg($updater,\%allArgs,\%allDirs,$file);
    }

    # Include any entries from sandbox.  Generally client won't
    # send entries that shouldn't be used.
    foreach my $file (keys %{$state->{entries}})
    {
        $allArgs{remove_prependdir($file)} = 1;
    }

    $state->{dirArgs} = \%allDirs;
    $state->{args} = [
        sort {
                # Sort priority: by directory depth, then actual file name:
            my @piecesA=split('/',$a);
            my @piecesB=split('/',$b);

            my $count=scalar(@piecesA);
            my $tmp=scalar(@piecesB);
            return $count<=>$tmp if($count!=$tmp);

            for($tmp=0;$tmp<$count;$tmp++)
            {
                if($piecesA[$tmp] ne $piecesB[$tmp])
                {
                    return $piecesA[$tmp] cmp $piecesB[$tmp]
                }
            }
            return 0;
        } keys(%allArgs) ];
}

## look up directory sticky tag, of either fullPath or a parent:
sub getDirStickyInfo
{
    my($fullPath)=@_;

    $fullPath=~s%/+$%%;
    while($fullPath ne "" && !defined($state->{dirMap}{"$fullPath/"}))
    {
        $fullPath=~s%/?[^/]*$%%;
    }

    if( !defined($state->{dirMap}{"$fullPath/"}) &&
        ( $fullPath eq "" ||
          $fullPath eq "." ) )
    {
        return $state->{dirMap}{""}{stickyInfo};
    }
    else
    {
        return $state->{dirMap}{"$fullPath/"}{stickyInfo};
    }
}

# Resolve precedence of various ways of specifying which version of
# a file you want.  Returns undef (for default head), or a ref to a hash
# that contains "tag" and/or "date" keys.
sub resolveStickyInfo
{
    my($filename,$stickyTag,$stickyDate,$reset) = @_;

    # Order of precedence of sticky tags:
    #    -A       [head]
    #    -r /tag/
    #    [file entry sticky tag]
    #    [the tag specified in dir req_Sticky]
    #    [the tag specified in a parent dir req_Sticky]
    #    [head]

    my $result;
    if($reset)
    {
        # $result=undef;
    }
    elsif( defined($stickyTag) && $stickyTag ne "" )
           # || ( defined($stickyDate) && $stickyDate ne "" )   # TODO
    {
        $result={ 'tag' => (defined($stickyTag)?$stickyTag:undef) };

        # TODO: Convert -D value into the form 2011.04.10.04.46.57,
        #   similar to an entry line's sticky date, without the D prefix.
        #   It sometimes (always?) arrives as something more like
        #   '10 Apr 2011 04:46:57 -0000'...
        # $result={ 'date' => (defined($stickyDate)?$stickyDate:undef) };
    }
    elsif( defined($state->{entries}{$filename}) &&
           defined($state->{entries}{$filename}{tag_or_date}) &&
           $state->{entries}{$filename}{tag_or_date} ne "" )
    {
        my($tagOrDate)=$state->{entries}{$filename}{tag_or_date};
        if($tagOrDate=~/^T([^ ]+)\s*$/)
        {
            $result = { 'tag' => $1 };
        }
        elsif($tagOrDate=~/^D([0-9.]+)\s*$/)
        {
            $result= { 'date' => $1 };
        }
        else
        {
            die "Unknown tag_or_date format\n";
        }
    }
    else
    {
        $result=getDirStickyInfo($filename);
    }

    return $result;
}

# Convert a stickyInfo (ref to a hash) as returned by resolveStickyInfo into
# a form appropriate for the sticky tag field of an Entries
# line (field index 5, 0-based).
sub getStickyTagOrDate
{
    my($stickyInfo)=@_;

    my $result;
    if(defined($stickyInfo) && defined($stickyInfo->{tag}))
    {
        $result="T$stickyInfo->{tag}";
    }
    # TODO: When/if we actually pick versions by {date} properly,
    #   also handle it here:
    #   "D$stickyInfo->{date}" (example: "D2011.04.13.20.37.07").
    else
    {
        $result="";
    }

    return $result;
}

# This method cleans up the $state variable after a command that uses arguments has run
sub statecleanup
{
    $state->{files} = [];
    $state->{dirArgs} = {};
    $state->{args} = [];
    $state->{arguments} = [];
    $state->{entries} = {};
    $state->{dirMap} = {};
}

# Return working directory CVS revision "1.X" out
# of the working directory "entries" state, for the given filename.
# This is prefixed with a dash if the file is scheduled for removal
# when it is committed.
sub revparse
{
    my $filename = shift;

    return $state->{entries}{$filename}{revision};
}

# This method takes a file hash and does a CVS "file transfer".  Its
# exact behaviour depends on a second, optional hash table argument:
# - If $options->{targetfile}, dump the contents to that file;
# - If $options->{print}, use M/MT to transmit the contents one line
#   at a time;
# - Otherwise, transmit the size of the file, followed by the file
#   contents.
sub transmitfile
{
    my $filehash = shift;
    my $options = shift;

    if ( defined ( $filehash ) and $filehash eq "deleted" )
    {
        $log->warn("filehash is 'deleted'");
        return;
    }

    die "Need filehash" unless ( defined ( $filehash ) and $filehash =~ /^[a-zA-Z0-9]{$state->{hexsz}}$/ );

    my $type = safe_pipe_capture('git', 'cat-file', '-t', $filehash);
    chomp $type;

    die ( "Invalid type '$type' (expected 'blob')" ) unless ( defined ( $type ) and $type eq "blob" );

    my $size = safe_pipe_capture('git', 'cat-file', '-s', $filehash);
    chomp $size;

    $log->debug("transmitfile($filehash) size=$size, type=$type");

    if ( open my $fh, '-|', "git", "cat-file", "blob", $filehash )
    {
        if ( defined ( $options->{targetfile} ) )
        {
            my $targetfile = $options->{targetfile};
            open NEWFILE, ">", $targetfile or die("Couldn't open '$targetfile' for writing : $!");
            print NEWFILE $_ while ( <$fh> );
            close NEWFILE or die("Failed to write '$targetfile': $!");
        } elsif ( defined ( $options->{print} ) && $options->{print} ) {
            while ( <$fh> ) {
                if( /\n\z/ ) {
                    print 'M ', $_;
                } else {
                    print 'MT text ', $_, "\n";
                }
            }
        } else {
            print "$size\n";
            print while ( <$fh> );
        }
        close $fh or die ("Couldn't close filehandle for transmitfile(): $!");
    } else {
        die("Couldn't execute git-cat-file");
    }
}

# This method takes a file name, and returns ( $dirpart, $filepart ) which
# refers to the directory portion and the file portion of the filename
# respectively
sub filenamesplit
{
    my $filename = shift;
    my $fixforlocaldir = shift;

    my ( $filepart, $dirpart ) = ( $filename, "." );
    ( $filepart, $dirpart ) = ( $2, $1 ) if ( $filename =~ /(.*)\/(.*)/ );
    $dirpart .= "/";

    if ( $fixforlocaldir )
    {
        $dirpart =~ s/^$state->{prependdir}//;
    }

    return ( $filepart, $dirpart );
}

# Cleanup various junk in filename (try to canonicalize it), and
# add prependdir to accommodate running CVS client from a
# subdirectory (so the output is relative to top directory of the project).
sub filecleanup
{
    my $filename = shift;

    return undef unless(defined($filename));
    if ( $filename =~ /^\// )
    {
        print "E absolute filenames '$filename' not supported by server\n";
        return undef;
    }

    if($filename eq ".")
    {
        $filename="";
    }
    $filename =~ s/^\.\///g;
    $filename =~ s%/+%/%g;
    $filename = $state->{prependdir} . $filename;
    $filename =~ s%/$%%;
    return $filename;
}

# Remove prependdir from the path, so that it is relative to the directory
# the CVS client was started from, rather than the top of the project.
# Essentially the inverse of filecleanup().
sub remove_prependdir
{
    my($path) = @_;
    if(defined($state->{prependdir}) && $state->{prependdir} ne "")
    {
        my($pre)=$state->{prependdir};
        $pre=~s%/$%%;
        if(!($path=~s%^\Q$pre\E/?%%))
        {
            $log->fatal("internal error missing prependdir");
            die("internal error missing prependdir");
        }
    }
    return $path;
}

sub validateGitDir
{
    if( !defined($state->{CVSROOT}) )
    {
        print "error 1 CVSROOT not specified\n";
        cleanupWorkTree();
        exit;
    }
    if( $ENV{GIT_DIR} ne ($state->{CVSROOT} . '/') )
    {
        print "error 1 Internally inconsistent CVSROOT\n";
        cleanupWorkTree();
        exit;
    }
}

# Setup working directory in a work tree with the requested version
# loaded in the index.
sub setupWorkTree
{
    my ($ver) = @_;

    validateGitDir();

    if( ( defined($work->{state}) && $work->{state} != 1 ) ||
        defined($work->{tmpDir}) )
    {
        $log->warn("Bad work tree state management");
        print "error 1 Internal setup multiple work trees without cleanup\n";
        cleanupWorkTree();
        exit;
    }

    $work->{workDir} = tempdir ( DIR => $TEMP_DIR );

    if( !defined($work->{index}) )
    {
        (undef, $work->{index}) = tempfile ( DIR => $TEMP_DIR, OPEN => 0 );
    }

    chdir $work->{workDir} or
        die "Unable to chdir to $work->{workDir}\n";

    $log->info("Setting up GIT_WORK_TREE as '.' in '$work->{workDir}', index file is '$work->{index}'");

    $ENV{GIT_WORK_TREE} = ".";
    $ENV{GIT_INDEX_FILE} = $work->{index};
    $work->{state} = 2;

    if($ver)
    {
        system("git","read-tree",$ver);
        unless ($? == 0)
        {
            $log->warn("Error running git-read-tree");
            die "Error running git-read-tree $ver in $work->{workDir} $!\n";
        }
    }
    # else # req_annotate reads tree for each file
}

# Ensure current directory is in some kind of working directory,
# with a recent version loaded in the index.
sub ensureWorkTree
{
    if( defined($work->{tmpDir}) )
    {
        $log->warn("Bad work tree state management [ensureWorkTree()]");
        print "error 1 Internal setup multiple dirs without cleanup\n";
        cleanupWorkTree();
        exit;
    }
    if( $work->{state} )
    {
        return;
    }

    validateGitDir();

    if( !defined($work->{emptyDir}) )
    {
        $work->{emptyDir} = tempdir ( DIR => $TEMP_DIR, OPEN => 0);
    }
    chdir $work->{emptyDir} or
        die "Unable to chdir to $work->{emptyDir}\n";

    my $ver = safe_pipe_capture('git', 'show-ref', '-s', "refs/heads/$state->{module}");
    chomp $ver;
    if ($ver !~ /^[0-9a-f]{$state->{hexsz}}$/)
    {
        $log->warn("Error from git show-ref -s refs/head$state->{module}");
        print "error 1 cannot find the current HEAD of module";
        cleanupWorkTree();
        exit;
    }

    if( !defined($work->{index}) )
    {
        (undef, $work->{index}) = tempfile ( DIR => $TEMP_DIR, OPEN => 0 );
    }

    $ENV{GIT_WORK_TREE} = ".";
    $ENV{GIT_INDEX_FILE} = $work->{index};
    $work->{state} = 1;

    system("git","read-tree",$ver);
    unless ($? == 0)
    {
        die "Error running git-read-tree $ver $!\n";
    }
}

# Cleanup working directory that is not needed any longer.
sub cleanupWorkTree
{
    if( ! $work->{state} )
    {
        return;
    }

    chdir "/" or die "Unable to chdir '/'\n";

    if( defined($work->{workDir}) )
    {
        rmtree( $work->{workDir} );
        undef $work->{workDir};
    }
    undef $work->{state};
}

# Setup a temporary directory (not a working tree), typically for
# merging dirty state as in req_update.
sub setupTmpDir
{
    $work->{tmpDir} = tempdir ( DIR => $TEMP_DIR );
    chdir $work->{tmpDir} or die "Unable to chdir $work->{tmpDir}\n";

    return $work->{tmpDir};
}

# Clean up a previously setupTmpDir.  Restore previous work tree if
# appropriate.
sub cleanupTmpDir
{
    if ( !defined($work->{tmpDir}) )
    {
        $log->warn("cleanup tmpdir that has not been setup");
        die "Cleanup tmpDir that has not been setup\n";
    }
    if( defined($work->{state}) )
    {
        if( $work->{state} == 1 )
        {
            chdir $work->{emptyDir} or
                die "Unable to chdir to $work->{emptyDir}\n";
        }
        elsif( $work->{state} == 2 )
        {
            chdir $work->{workDir} or
                die "Unable to chdir to $work->{emptyDir}\n";
        }
        else
        {
            $log->warn("Inconsistent work dir state");
            die "Inconsistent work dir state\n";
        }
    }
    else
    {
        chdir "/" or die "Unable to chdir '/'\n";
    }
}

# Given a path, this function returns a string containing the kopts
# that should go into that path's Entries line.  For example, a binary
# file should get -kb.
sub kopts_from_path
{
    my ($path, $srcType, $name) = @_;

    if ( defined ( $cfg->{gitcvs}{usecrlfattr} ) and
         $cfg->{gitcvs}{usecrlfattr} =~ /\s*(1|true|yes)\s*$/i )
    {
        my ($val) = check_attr( "text", $path );
        if ( $val eq "unspecified" )
        {
            $val = check_attr( "crlf", $path );
        }
        if ( $val eq "unset" )
        {
            return "-kb"
        }
        elsif ( check_attr( "eol", $path ) ne "unspecified" ||
                $val eq "set" || $val eq "input" )
        {
            return "";
        }
        else
        {
            $log->info("Unrecognized check_attr crlf $path : $val");
        }
    }

    if ( defined ( $cfg->{gitcvs}{allbinary} ) )
    {
        if( ($cfg->{gitcvs}{allbinary} =~ /^\s*(1|true|yes)\s*$/i) )
        {
            return "-kb";
        }
        elsif( ($cfg->{gitcvs}{allbinary} =~ /^\s*guess\s*$/i) )
        {
            if( is_binary($srcType,$name) )
            {
                $log->debug("... as binary");
                return "-kb";
            }
            else
            {
                $log->debug("... as text");
            }
        }
    }
    # Return "" to give no special treatment to any path
    return "";
}

sub check_attr
{
    my ($attr,$path) = @_;
    ensureWorkTree();
    if ( open my $fh, '-|', "git", "check-attr", $attr, "--", $path )
    {
        my $val = <$fh>;
        close $fh;
        $val =~ s/.*: ([^:\r\n]*)\s*$/$1/;
        return $val;
    }
    else
    {
        return undef;
    }
}

# This should have the same heuristics as convert.c:is_binary() and related.
# Note that the bare CR test is done by callers in convert.c.
sub is_binary
{
    my ($srcType,$name) = @_;
    $log->debug("is_binary($srcType,$name)");

    # Minimize amount of interpreted code run in the inner per-character
    # loop for large files, by totalling each character value and
    # then analyzing the totals.
    my @counts;
    my $i;
    for($i=0;$i<256;$i++)
    {
        $counts[$i]=0;
    }

    my $fh = open_blob_or_die($srcType,$name);
    my $line;
    while( defined($line=<$fh>) )
    {
        # Any '\0' and bare CR are considered binary.
        if( $line =~ /\0|(\r[^\n])/ )
        {
            close($fh);
            return 1;
        }

        # Count up each character in the line:
        my $len=length($line);
        for($i=0;$i<$len;$i++)
        {
            $counts[ord(substr($line,$i,1))]++;
        }
    }
    close $fh;

    # Don't count CR and LF as either printable/nonprintable
    $counts[ord("\n")]=0;
    $counts[ord("\r")]=0;

    # Categorize individual character count into printable and nonprintable:
    my $printable=0;
    my $nonprintable=0;
    for($i=0;$i<256;$i++)
    {
        if( $i < 32 &&
            $i != ord("\b") &&
            $i != ord("\t") &&
            $i != 033 &&       # ESC
            $i != 014 )        # FF
        {
            $nonprintable+=$counts[$i];
        }
        elsif( $i==127 )  # DEL
        {
            $nonprintable+=$counts[$i];
        }
        else
        {
            $printable+=$counts[$i];
        }
    }

    return ($printable >> 7) < $nonprintable;
}

# Returns open file handle.  Possible invocations:
#  - open_blob_or_die("file",$filename);
#  - open_blob_or_die("sha1",$filehash);
sub open_blob_or_die
{
    my ($srcType,$name) = @_;
    my ($fh);
    if( $srcType eq "file" )
    {
        if( !open $fh,"<",$name )
        {
            $log->warn("Unable to open file $name: $!");
            die "Unable to open file $name: $!\n";
        }
    }
    elsif( $srcType eq "sha1" )
    {
        unless ( defined ( $name ) and $name =~ /^[a-zA-Z0-9]{$state->{hexsz}}$/ )
        {
            $log->warn("Need filehash");
            die "Need filehash\n";
        }

        my $type = safe_pipe_capture('git', 'cat-file', '-t', $name);
        chomp $type;

        unless ( defined ( $type ) and $type eq "blob" )
        {
            $log->warn("Invalid type '$type' for '$name'");
            die ( "Invalid type '$type' (expected 'blob')" )
        }

        my $size = safe_pipe_capture('git', 'cat-file', '-s', $name);
        chomp $size;

        $log->debug("open_blob_or_die($name) size=$size, type=$type");

        unless( open $fh, '-|', "git", "cat-file", "blob", $name )
        {
            $log->warn("Unable to open sha1 $name");
            die "Unable to open sha1 $name\n";
        }
    }
    else
    {
        $log->warn("Unknown type of blob source: $srcType");
        die "Unknown type of blob source: $srcType\n";
    }
    return $fh;
}

# Generate a CVS author name from Git author information, by taking the local
# part of the email address and replacing characters not in the Portable
# Filename Character Set (see IEEE Std 1003.1-2001, 3.276) by underscores. CVS
# Login names are Unix login names, which should be restricted to this
# character set.
sub cvs_author
{
    my $author_line = shift;
    (my $author) = $author_line =~ /<([^@>]*)/;

    $author =~ s/[^-a-zA-Z0-9_.]/_/g;
    $author =~ s/^-/_/;

    $author;
}


sub descramble
{
    # This table is from src/scramble.c in the CVS source
    my @SHIFTS = (
        0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15,
        16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,
        114,120, 53, 79, 96,109, 72,108, 70, 64, 76, 67,116, 74, 68, 87,
        111, 52, 75,119, 49, 34, 82, 81, 95, 65,112, 86,118,110,122,105,
        41, 57, 83, 43, 46,102, 40, 89, 38,103, 45, 50, 42,123, 91, 35,
        125, 55, 54, 66,124,126, 59, 47, 92, 71,115, 78, 88,107,106, 56,
        36,121,117,104,101,100, 69, 73, 99, 63, 94, 93, 39, 37, 61, 48,
        58,113, 32, 90, 44, 98, 60, 51, 33, 97, 62, 77, 84, 80, 85,223,
        225,216,187,166,229,189,222,188,141,249,148,200,184,136,248,190,
        199,170,181,204,138,232,218,183,255,234,220,247,213,203,226,193,
        174,172,228,252,217,201,131,230,197,211,145,238,161,179,160,212,
        207,221,254,173,202,146,224,151,140,196,205,130,135,133,143,246,
        192,159,244,239,185,168,215,144,139,165,180,157,147,186,214,176,
        227,231,219,169,175,156,206,198,129,164,150,210,154,177,134,127,
        182,128,158,208,162,132,167,209,149,241,153,251,237,236,171,195,
        243,233,253,240,194,250,191,155,142,137,245,235,163,242,178,152
    );
    my ($str) = @_;

    # This should never happen, the same password format (A) has been
    # used by CVS since the beginning of time
    {
        my $fmt = substr($str, 0, 1);
        die "invalid password format `$fmt'" unless $fmt eq 'A';
    }

    my @str = unpack "C*", substr($str, 1);
    my $ret = join '', map { chr $SHIFTS[$_] } @str;
    return $ret;
}

# Test if the (deep) values of two references to a hash are the same.
sub refHashEqual
{
    my($v1,$v2) = @_;

    my $out;
    if(!defined($v1))
    {
        if(!defined($v2))
        {
            $out=1;
        }
    }
    elsif( !defined($v2) ||
           scalar(keys(%{$v1})) != scalar(keys(%{$v2})) )
    {
        # $out=undef;
    }
    else
    {
        $out=1;

        my $key;
        foreach $key (keys(%{$v1}))
        {
            if( !exists($v2->{$key}) ||
                defined($v1->{$key}) ne defined($v2->{$key}) ||
                ( defined($v1->{$key}) &&
                  $v1->{$key} ne $v2->{$key} ) )
            {
               $out=undef;
               last;
            }
        }
    }

    return $out;
}

# an alternative to `command` that allows input to be passed as an array
# to work around shell problems with weird characters in arguments

sub safe_pipe_capture {

    my @output;

    if (my $pid = open my $child, '-|') {
        @output = (<$child>);
        close $child or die join(' ',@_).": $! $?";
    } else {
        exec(@_) or die "$! $?"; # exec() can fail the executable can't be found
    }
    return wantarray ? @output : join('',@output);
}


package GITCVS::log;

####
#### Copyright The Open University UK - 2006.
####
#### Authors: Martyn Smith    <martyn@catalyst.net.nz>
####          Martin Langhoff <martin@laptop.org>
####
####

use strict;
use warnings;

=head1 NAME

GITCVS::log

=head1 DESCRIPTION

This module provides very crude logging with a similar interface to
Log::Log4perl

=head1 METHODS

=cut

=head2 new

Creates a new log object, optionally you can specify a filename here to
indicate the file to log to. If no log file is specified, you can specify one
later with method setfile, or indicate you no longer want logging with method
nofile.

Until one of these methods is called, all log calls will buffer messages ready
to write out.

=cut
sub new
{
    my $class = shift;
    my $filename = shift;

    my $self = {};

    bless $self, $class;

    if ( defined ( $filename ) )
    {
        open $self->{fh}, ">>", $filename or die("Couldn't open '$filename' for writing : $!");
    }

    return $self;
}

=head2 setfile

This methods takes a filename, and attempts to open that file as the log file.
If successful, all buffered data is written out to the file, and any further
logging is written directly to the file.

=cut
sub setfile
{
    my $self = shift;
    my $filename = shift;

    if ( defined ( $filename ) )
    {
        open $self->{fh}, ">>", $filename or die("Couldn't open '$filename' for writing : $!");
    }

    return unless ( defined ( $self->{buffer} ) and ref $self->{buffer} eq "ARRAY" );

    while ( my $line = shift @{$self->{buffer}} )
    {
        print {$self->{fh}} $line;
    }
}

=head2 nofile

This method indicates no logging is going to be used. It flushes any entries in
the internal buffer, and sets a flag to ensure no further data is put there.

=cut
sub nofile
{
    my $self = shift;

    $self->{nolog} = 1;

    return unless ( defined ( $self->{buffer} ) and ref $self->{buffer} eq "ARRAY" );

    $self->{buffer} = [];
}

=head2 _logopen

Internal method. Returns true if the log file is open, false otherwise.

=cut
sub _logopen
{
    my $self = shift;

    return 1 if ( defined ( $self->{fh} ) and ref $self->{fh} eq "GLOB" );
    return 0;
}

=head2 debug info warn fatal

These four methods are wrappers to _log. They provide the actual interface for
logging data.

=cut
sub debug { my $self = shift; $self->_log("debug", @_); }
sub info  { my $self = shift; $self->_log("info" , @_); }
sub warn  { my $self = shift; $self->_log("warn" , @_); }
sub fatal { my $self = shift; $self->_log("fatal", @_); }

=head2 _log

This is an internal method called by the logging functions. It generates a
timestamp and pushes the logged line either to file, or internal buffer.

=cut
sub _log
{
    my $self = shift;
    my $level = shift;

    return if ( $self->{nolog} );

    my @time = localtime;
    my $timestring = sprintf("%4d-%02d-%02d %02d:%02d:%02d : %-5s",
        $time[5] + 1900,
        $time[4] + 1,
        $time[3],
        $time[2],
        $time[1],
        $time[0],
        uc $level,
    );

    if ( $self->_logopen )
    {
        print {$self->{fh}} $timestring . " - " . join(" ",@_) . "\n";
    } else {
        push @{$self->{buffer}}, $timestring . " - " . join(" ",@_) . "\n";
    }
}

=head2 DESTROY

This method simply closes the file handle if one is open

=cut
sub DESTROY
{
    my $self = shift;

    if ( $self->_logopen )
    {
        close $self->{fh};
    }
}

package GITCVS::updater;

####
#### Copyright The Open University UK - 2006.
####
#### Authors: Martyn Smith    <martyn@catalyst.net.nz>
####          Martin Langhoff <martin@laptop.org>
####
####

use strict;
use warnings;
use DBI;
our $_use_fsync;

# n.b. consider using Git.pm
sub use_fsync {
    if (!defined($_use_fsync)) {
        my $x = $ENV{GIT_TEST_FSYNC};
        if (defined $x) {
            local $ENV{GIT_CONFIG};
            delete $ENV{GIT_CONFIG};
            my $v = ::safe_pipe_capture('git', '-c', "test.fsync=$x",
                                        qw(config --type=bool test.fsync));
            $_use_fsync = defined($v) ? ($v eq "true\n") : 1;
        }
    }
    $_use_fsync;
}

=head1 METHODS

=cut

=head2 new

=cut
sub new
{
    my $class = shift;
    my $config = shift;
    my $module = shift;
    my $log = shift;

    die "Need to specify a git repository" unless ( defined($config) and -d $config );
    die "Need to specify a module" unless ( defined($module) );

    $class = ref($class) || $class;

    my $self = {};

    bless $self, $class;

    $self->{valid_tables} = {'revision' => 1,
                             'revision_ix1' => 1,
                             'revision_ix2' => 1,
                             'head' => 1,
                             'head_ix1' => 1,
                             'properties' => 1,
                             'commitmsgs' => 1};

    $self->{module} = $module;
    $self->{git_path} = $config . "/";

    $self->{log} = $log;

    die "Git repo '$self->{git_path}' doesn't exist" unless ( -d $self->{git_path} );

    # Stores full sha1's for various branch/tag names, abbreviations, etc:
    $self->{commitRefCache} = {};

    $self->{dbdriver} = $cfg->{gitcvs}{$state->{method}}{dbdriver} ||
        $cfg->{gitcvs}{dbdriver} || "SQLite";
    $self->{dbname} = $cfg->{gitcvs}{$state->{method}}{dbname} ||
        $cfg->{gitcvs}{dbname} || "%Ggitcvs.%m.sqlite";
    $self->{dbuser} = $cfg->{gitcvs}{$state->{method}}{dbuser} ||
        $cfg->{gitcvs}{dbuser} || "";
    $self->{dbpass} = $cfg->{gitcvs}{$state->{method}}{dbpass} ||
        $cfg->{gitcvs}{dbpass} || "";
    $self->{dbtablenameprefix} = $cfg->{gitcvs}{$state->{method}}{dbtablenameprefix} ||
        $cfg->{gitcvs}{dbtablenameprefix} || "";
    my %mapping = ( m => $module,
                    a => $state->{method},
                    u => getlogin || getpwuid($<) || $<,
                    G => $self->{git_path},
                    g => mangle_dirname($self->{git_path}),
                    );
    $self->{dbname} =~ s/%([mauGg])/$mapping{$1}/eg;
    $self->{dbuser} =~ s/%([mauGg])/$mapping{$1}/eg;
    $self->{dbtablenameprefix} =~ s/%([mauGg])/$mapping{$1}/eg;
    $self->{dbtablenameprefix} = mangle_tablename($self->{dbtablenameprefix});

    die "Invalid char ':' in dbdriver" if $self->{dbdriver} =~ /:/;
    die "Invalid char ';' in dbname" if $self->{dbname} =~ /;/;
    $self->{dbh} = DBI->connect("dbi:$self->{dbdriver}:dbname=$self->{dbname}",
                                $self->{dbuser},
                                $self->{dbpass});
    die "Error connecting to database\n" unless defined $self->{dbh};
    if ($self->{dbdriver} eq 'SQLite' && !use_fsync()) {
        $self->{dbh}->do('PRAGMA synchronous = OFF');
    }

    $self->{tables} = {};
    foreach my $table ( keys %{$self->{dbh}->table_info(undef,undef,undef,'TABLE')->fetchall_hashref('TABLE_NAME')} )
    {
        $self->{tables}{$table} = 1;
    }

    # Construct the revision table if required
    # The revision table stores an entry for each file, each time that file
    # changes.
    #   numberOfRecords = O( numCommits * averageNumChangedFilesPerCommit )
    # This is not sufficient to support "-r {commithash}" for any
    # files except files that were modified by that commit (also,
    # some places in the code ignore/effectively strip out -r in
    # some cases, before it gets passed to getmeta()).
    # The "filehash" field typically has a git blob hash, but can also
    # be set to "dead" to indicate that the given version of the file
    # should not exist in the sandbox.
    unless ( $self->{tables}{$self->tablename("revision")} )
    {
        my $tablename = $self->tablename("revision");
        my $ix1name = $self->tablename("revision_ix1");
        my $ix2name = $self->tablename("revision_ix2");
        $self->{dbh}->do("
            CREATE TABLE $tablename (
                name       TEXT NOT NULL,
                revision   INTEGER NOT NULL,
                filehash   TEXT NOT NULL,
                commithash TEXT NOT NULL,
                author     TEXT NOT NULL,
                modified   TEXT NOT NULL,
                mode       TEXT NOT NULL
            )
        ");
        $self->{dbh}->do("
            CREATE INDEX $ix1name
            ON $tablename (name,revision)
        ");
        $self->{dbh}->do("
            CREATE INDEX $ix2name
            ON $tablename (name,commithash)
        ");
    }

    # Construct the head table if required
    # The head table (along with the "last_commit" entry in the property
    # table) is the persisted working state of the "sub update" subroutine.
    # All of it's data is read entirely first, and completely recreated
    # last, every time "sub update" runs.
    # This is also used by "sub getmeta" when it is asked for the latest
    # version of a file (as opposed to some specific version).
    # Another way of thinking about it is as a single slice out of
    # "revisions", giving just the most recent revision information for
    # each file.
    unless ( $self->{tables}{$self->tablename("head")} )
    {
        my $tablename = $self->tablename("head");
        my $ix1name = $self->tablename("head_ix1");
        $self->{dbh}->do("
            CREATE TABLE $tablename (
                name       TEXT NOT NULL,
                revision   INTEGER NOT NULL,
                filehash   TEXT NOT NULL,
                commithash TEXT NOT NULL,
                author     TEXT NOT NULL,
                modified   TEXT NOT NULL,
                mode       TEXT NOT NULL
            )
        ");
        $self->{dbh}->do("
            CREATE INDEX $ix1name
            ON $tablename (name)
        ");
    }

    # Construct the properties table if required
    #  - "last_commit" - Used by "sub update".
    unless ( $self->{tables}{$self->tablename("properties")} )
    {
        my $tablename = $self->tablename("properties");
        $self->{dbh}->do("
            CREATE TABLE $tablename (
                key        TEXT NOT NULL PRIMARY KEY,
                value      TEXT
            )
        ");
    }

    # Construct the commitmsgs table if required
    # The commitmsgs table is only used for merge commits, since
    # "sub update" will only keep one branch of parents.  Shortlogs
    # for ignored commits (i.e. not on the chosen branch) will be used
    # to construct a replacement "collapsed" merge commit message,
    # which will be stored in this table.  See also "sub commitmessage".
    unless ( $self->{tables}{$self->tablename("commitmsgs")} )
    {
        my $tablename = $self->tablename("commitmsgs");
        $self->{dbh}->do("
            CREATE TABLE $tablename (
                key        TEXT NOT NULL PRIMARY KEY,
                value      TEXT
            )
        ");
    }

    return $self;
}

=head2 tablename

=cut
sub tablename
{
    my $self = shift;
    my $name = shift;

    if (exists $self->{valid_tables}{$name}) {
        return $self->{dbtablenameprefix} . $name;
    } else {
        return undef;
    }
}

=head2 update

Bring the database up to date with the latest changes from
the git repository.

Internal working state is read out of the "head" table and the
"last_commit" property, then it updates "revisions" based on that, and
finally it writes the new internal state back to the "head" table
so it can be used as a starting point the next time update is called.

=cut
sub update
{
    my $self = shift;

    # first lets get the commit list
    $ENV{GIT_DIR} = $self->{git_path};

    my $commitsha1 = ::safe_pipe_capture('git', 'rev-parse', $self->{module});
    chomp $commitsha1;

    my $commitinfo = ::safe_pipe_capture('git', 'cat-file', 'commit', $self->{module});
    unless ( $commitinfo =~ /tree\s+[a-zA-Z0-9]{$state->{hexsz}}/ )
    {
        die("Invalid module '$self->{module}'");
    }


    my $git_log;
    my $lastcommit = $self->_get_prop("last_commit");

    if (defined $lastcommit && $lastcommit eq $commitsha1) { # up-to-date
         # invalidate the gethead cache
         $self->clearCommitRefCaches();
         return 1;
    }

    # Start exclusive lock here...
    $self->{dbh}->begin_work() or die "Cannot lock database for BEGIN";

    # TODO: log processing is memory bound
    # if we can parse into a 2nd file that is in reverse order
    # we can probably do something really efficient
    my @git_log_params = ('--pretty', '--parents', '--topo-order');

    if (defined $lastcommit) {
        push @git_log_params, "$lastcommit..$self->{module}";
    } else {
        push @git_log_params, $self->{module};
    }
    # git-rev-list is the backend / plumbing version of git-log
    open(my $gitLogPipe, '-|', 'git', 'rev-list', @git_log_params)
                or die "Cannot call git-rev-list: $!";
    my @commits=readCommits($gitLogPipe);
    close $gitLogPipe;

    # Now all the commits are in the @commits bucket
    # ordered by time DESC. for each commit that needs processing,
    # determine whether it's following the last head we've seen or if
    # it's on its own branch, grab a file list, and add whatever's changed
    # NOTE: $lastcommit refers to the last commit from previous run
    #       $lastpicked is the last commit we picked in this run
    my $lastpicked;
    my $head = {};
    if (defined $lastcommit) {
        $lastpicked = $lastcommit;
    }

    my $committotal = scalar(@commits);
    my $commitcount = 0;

    # Load the head table into $head (for cached lookups during the update process)
    foreach my $file ( @{$self->gethead(1)} )
    {
        $head->{$file->{name}} = $file;
    }

    foreach my $commit ( @commits )
    {
        $self->{log}->debug("GITCVS::updater - Processing commit $commit->{hash} (" . (++$commitcount) . " of $committotal)");
        if (defined $lastpicked)
        {
            if (!in_array($lastpicked, @{$commit->{parents}}))
            {
                # skip, we'll see this delta
                # as part of a merge later
                # warn "skipping off-track  $commit->{hash}\n";
                next;
            } elsif (@{$commit->{parents}} > 1) {
                # it is a merge commit, for each parent that is
                # not $lastpicked (not given a CVS revision number),
                # see if we can get a log
                # from the merge-base to that parent to put it
                # in the message as a merge summary.
                my @parents = @{$commit->{parents}};
                foreach my $parent (@parents) {
                    if ($parent eq $lastpicked) {
                        next;
                    }
                    # git-merge-base can potentially (but rarely) throw
                    # several candidate merge bases. let's assume
                    # that the first one is the best one.
		    my $base = eval {
			    ::safe_pipe_capture('git', 'merge-base',
						 $lastpicked, $parent);
		    };
		    # The two branches may not be related at all,
		    # in which case merge base simply fails to find
		    # any, but that's Ok.
		    next if ($@);

                    chomp $base;
                    if ($base) {
                        my @merged;
                        # print "want to log between  $base $parent \n";
                        open(GITLOG, '-|', 'git', 'log', '--pretty=medium', "$base..$parent")
			  or die "Cannot call git-log: $!";
                        my $mergedhash;
                        while (<GITLOG>) {
                            chomp;
                            if (!defined $mergedhash) {
                                if (m/^commit\s+(.+)$/) {
                                    $mergedhash = $1;
                                } else {
                                    next;
                                }
                            } else {
                                # grab the first line that looks non-rfc822
                                # aka has content after leading space
                                if (m/^\s+(\S.*)$/) {
                                    my $title = $1;
                                    $title = substr($title,0,100); # truncate
                                    unshift @merged, "$mergedhash $title";
                                    undef $mergedhash;
                                }
                            }
                        }
                        close GITLOG;
                        if (@merged) {
                            $commit->{mergemsg} = $commit->{message};
                            $commit->{mergemsg} .= "\nSummary of merged commits:\n\n";
                            foreach my $summary (@merged) {
                                $commit->{mergemsg} .= "\t$summary\n";
                            }
                            $commit->{mergemsg} .= "\n\n";
                            # print "Message for $commit->{hash} \n$commit->{mergemsg}";
                        }
                    }
                }
            }
        }

        # convert the date to CVS-happy format
        my $cvsDate = convertToCvsDate($commit->{date});

        if ( defined ( $lastpicked ) )
        {
            my $filepipe = open(FILELIST, '-|', 'git', 'diff-tree', '-z', '-r', $lastpicked, $commit->{hash}) or die("Cannot call git-diff-tree : $!");
	    local ($/) = "\0";
            while ( <FILELIST> )
            {
		chomp;
                unless ( /^:\d{6}\s+([0-7]{6})\s+[a-f0-9]{$state->{hexsz}}\s+([a-f0-9]{$state->{hexsz}})\s+(\w)$/o )
                {
                    die("Couldn't process git-diff-tree line : $_");
                }
		my ($mode, $hash, $change) = ($1, $2, $3);
		my $name = <FILELIST>;
		chomp($name);

                # $log->debug("File mode=$mode, hash=$hash, change=$change, name=$name");

                my $dbMode = convertToDbMode($mode);

                if ( $change eq "D" )
                {
                    #$log->debug("DELETE   $name");
                    $head->{$name} = {
                        name => $name,
                        revision => $head->{$name}{revision} + 1,
                        filehash => "deleted",
                        commithash => $commit->{hash},
                        modified => $cvsDate,
                        author => $commit->{author},
                        mode => $dbMode,
                    };
                    $self->insert_rev($name, $head->{$name}{revision}, $hash, $commit->{hash}, $cvsDate, $commit->{author}, $dbMode);
                }
                elsif ( $change eq "M" || $change eq "T" )
                {
                    #$log->debug("MODIFIED $name");
                    $head->{$name} = {
                        name => $name,
                        revision => $head->{$name}{revision} + 1,
                        filehash => $hash,
                        commithash => $commit->{hash},
                        modified => $cvsDate,
                        author => $commit->{author},
                        mode => $dbMode,
                    };
                    $self->insert_rev($name, $head->{$name}{revision}, $hash, $commit->{hash}, $cvsDate, $commit->{author}, $dbMode);
                }
                elsif ( $change eq "A" )
                {
                    #$log->debug("ADDED    $name");
                    $head->{$name} = {
                        name => $name,
                        revision => $head->{$name}{revision} ? $head->{$name}{revision}+1 : 1,
                        filehash => $hash,
                        commithash => $commit->{hash},
                        modified => $cvsDate,
                        author => $commit->{author},
                        mode => $dbMode,
                    };
                    $self->insert_rev($name, $head->{$name}{revision}, $hash, $commit->{hash}, $cvsDate, $commit->{author}, $dbMode);
                }
                else
                {
                    $log->warn("UNKNOWN FILE CHANGE mode=$mode, hash=$hash, change=$change, name=$name");
                    die;
                }
            }
            close FILELIST;
        } else {
            # this is used to detect files removed from the repo
            my $seen_files = {};

            my $filepipe = open(FILELIST, '-|', 'git', 'ls-tree', '-z', '-r', $commit->{hash}) or die("Cannot call git-ls-tree : $!");
	    local $/ = "\0";
            while ( <FILELIST> )
            {
		chomp;
                unless ( /^(\d+)\s+(\w+)\s+([a-zA-Z0-9]+)\t(.*)$/o )
                {
                    die("Couldn't process git-ls-tree line : $_");
                }

                my ( $mode, $git_type, $git_hash, $git_filename ) = ( $1, $2, $3, $4 );

                $seen_files->{$git_filename} = 1;

                my ( $oldhash, $oldrevision, $oldmode ) = (
                    $head->{$git_filename}{filehash},
                    $head->{$git_filename}{revision},
                    $head->{$git_filename}{mode}
                );

                my $dbMode = convertToDbMode($mode);

                # unless the file exists with the same hash, we need to update it ...
                unless ( defined($oldhash) and $oldhash eq $git_hash and defined($oldmode) and $oldmode eq $dbMode )
                {
                    my $newrevision = ( $oldrevision or 0 ) + 1;

                    $head->{$git_filename} = {
                        name => $git_filename,
                        revision => $newrevision,
                        filehash => $git_hash,
                        commithash => $commit->{hash},
                        modified => $cvsDate,
                        author => $commit->{author},
                        mode => $dbMode,
                    };


                    $self->insert_rev($git_filename, $newrevision, $git_hash, $commit->{hash}, $cvsDate, $commit->{author}, $dbMode);
                }
            }
            close FILELIST;

            # Detect deleted files
            foreach my $file ( sort keys %$head )
            {
                unless ( exists $seen_files->{$file} or $head->{$file}{filehash} eq "deleted" )
                {
                    $head->{$file}{revision}++;
                    $head->{$file}{filehash} = "deleted";
                    $head->{$file}{commithash} = $commit->{hash};
                    $head->{$file}{modified} = $cvsDate;
                    $head->{$file}{author} = $commit->{author};

                    $self->insert_rev($file, $head->{$file}{revision}, $head->{$file}{filehash}, $commit->{hash}, $cvsDate, $commit->{author}, $head->{$file}{mode});
                }
            }
            # END : "Detect deleted files"
        }


        if (exists $commit->{mergemsg})
        {
            $self->insert_mergelog($commit->{hash}, $commit->{mergemsg});
        }

        $lastpicked = $commit->{hash};

        $self->_set_prop("last_commit", $commit->{hash});
    }

    $self->delete_head();
    foreach my $file ( sort keys %$head )
    {
        $self->insert_head(
            $file,
            $head->{$file}{revision},
            $head->{$file}{filehash},
            $head->{$file}{commithash},
            $head->{$file}{modified},
            $head->{$file}{author},
            $head->{$file}{mode},
        );
    }
    # invalidate the gethead cache
    $self->clearCommitRefCaches();


    # Ending exclusive lock here
    $self->{dbh}->commit() or die "Failed to commit changes to SQLite";
}

sub readCommits
{
    my $pipeHandle = shift;
    my @commits;

    my %commit = ();

    while ( <$pipeHandle> )
    {
        chomp;
        if (m/^commit\s+(.*)$/) {
            # on ^commit lines put the just seen commit in the stack
            # and prime things for the next one
            if (keys %commit) {
                my %copy = %commit;
                unshift @commits, \%copy;
                %commit = ();
            }
            my @parents = split(m/\s+/, $1);
            $commit{hash} = shift @parents;
            $commit{parents} = \@parents;
        } elsif (m/^(\w+?):\s+(.*)$/ && !exists($commit{message})) {
            # on rfc822-like lines seen before we see any message,
            # lowercase the entry and put it in the hash as key-value
            $commit{lc($1)} = $2;
        } else {
            # message lines - skip initial empty line
            # and trim whitespace
            if (!exists($commit{message}) && m/^\s*$/) {
                # define it to mark the end of headers
                $commit{message} = '';
                next;
            }
            s/^\s+//; s/\s+$//; # trim ws
            $commit{message} .= $_ . "\n";
        }
    }

    unshift @commits, \%commit if ( keys %commit );

    return @commits;
}

sub convertToCvsDate
{
    my $date = shift;
    # Convert from: "git rev-list --pretty" formatted date
    # Convert to: "the format specified by RFC822 as modified by RFC1123."
    # Example: 26 May 1997 13:01:40 -0400
    if( $date =~ /^\w+\s+(\w+)\s+(\d+)\s+(\d+:\d+:\d+)\s+(\d+)\s+([+-]\d+)$/ )
    {
        $date = "$2 $1 $4 $3 $5";
    }

    return $date;
}

sub convertToDbMode
{
    my $mode = shift;

    # NOTE: The CVS protocol uses a string similar "u=rw,g=rw,o=rw",
    #  but the database "mode" column historically (and currently)
    #  only stores the "rw" (for user) part of the string.
    #    FUTURE: It might make more sense to persist the raw
    #  octal mode (or perhaps the final full CVS form) instead of
    #  this half-converted form, but it isn't currently worth the
    #  backwards compatibility headaches.

    $mode=~/^\d{3}(\d)\d\d$/;
    my $userBits=$1;

    my $dbMode = "";
    $dbMode .= "r" if ( $userBits & 4 );
    $dbMode .= "w" if ( $userBits & 2 );
    $dbMode .= "x" if ( $userBits & 1 );
    $dbMode = "rw" if ( $dbMode eq "" );

    return $dbMode;
}

sub insert_rev
{
    my $self = shift;
    my $name = shift;
    my $revision = shift;
    my $filehash = shift;
    my $commithash = shift;
    my $modified = shift;
    my $author = shift;
    my $mode = shift;
    my $tablename = $self->tablename("revision");

    my $insert_rev = $self->{dbh}->prepare_cached("INSERT INTO $tablename (name, revision, filehash, commithash, modified, author, mode) VALUES (?,?,?,?,?,?,?)",{},1);
    $insert_rev->execute($name, $revision, $filehash, $commithash, $modified, $author, $mode);
}

sub insert_mergelog
{
    my $self = shift;
    my $key = shift;
    my $value = shift;
    my $tablename = $self->tablename("commitmsgs");

    my $insert_mergelog = $self->{dbh}->prepare_cached("INSERT INTO $tablename (key, value) VALUES (?,?)",{},1);
    $insert_mergelog->execute($key, $value);
}

sub delete_head
{
    my $self = shift;
    my $tablename = $self->tablename("head");

    my $delete_head = $self->{dbh}->prepare_cached("DELETE FROM $tablename",{},1);
    $delete_head->execute();
}

sub insert_head
{
    my $self = shift;
    my $name = shift;
    my $revision = shift;
    my $filehash = shift;
    my $commithash = shift;
    my $modified = shift;
    my $author = shift;
    my $mode = shift;
    my $tablename = $self->tablename("head");

    my $insert_head = $self->{dbh}->prepare_cached("INSERT INTO $tablename (name, revision, filehash, commithash, modified, author, mode) VALUES (?,?,?,?,?,?,?)",{},1);
    $insert_head->execute($name, $revision, $filehash, $commithash, $modified, $author, $mode);
}

sub _get_prop
{
    my $self = shift;
    my $key = shift;
    my $tablename = $self->tablename("properties");

    my $db_query = $self->{dbh}->prepare_cached("SELECT value FROM $tablename WHERE key=?",{},1);
    $db_query->execute($key);
    my ( $value ) = $db_query->fetchrow_array;

    return $value;
}

sub _set_prop
{
    my $self = shift;
    my $key = shift;
    my $value = shift;
    my $tablename = $self->tablename("properties");

    my $db_query = $self->{dbh}->prepare_cached("UPDATE $tablename SET value=? WHERE key=?",{},1);
    $db_query->execute($value, $key);

    unless ( $db_query->rows )
    {
        $db_query = $self->{dbh}->prepare_cached("INSERT INTO $tablename (key, value) VALUES (?,?)",{},1);
        $db_query->execute($key, $value);
    }

    return $value;
}

=head2 gethead

=cut

sub gethead
{
    my $self = shift;
    my $intRev = shift;
    my $tablename = $self->tablename("head");

    return $self->{gethead_cache} if ( defined ( $self->{gethead_cache} ) );

    my $db_query = $self->{dbh}->prepare_cached("SELECT name, filehash, mode, revision, modified, commithash, author FROM $tablename ORDER BY name ASC",{},1);
    $db_query->execute();

    my $tree = [];
    while ( my $file = $db_query->fetchrow_hashref )
    {
        if(!$intRev)
        {
            $file->{revision} = "1.$file->{revision}"
        }
        push @$tree, $file;
    }

    $self->{gethead_cache} = $tree;

    return $tree;
}

=head2 getAnyHead

Returns a reference to an array of getmeta structures, one
per file in the specified tree hash.

=cut

sub getAnyHead
{
    my ($self,$hash) = @_;

    if(!defined($hash))
    {
        return $self->gethead();
    }

    my @files;
    {
        open(my $filePipe, '-|', 'git', 'ls-tree', '-z', '-r', $hash)
                or die("Cannot call git-ls-tree : $!");
        local $/ = "\0";
        @files=<$filePipe>;
        close $filePipe;
    }

    my $tree=[];
    my($line);
    foreach $line (@files)
    {
        $line=~s/\0$//;
        unless ( $line=~/^(\d+)\s+(\w+)\s+([a-zA-Z0-9]+)\t(.*)$/o )
        {
            die("Couldn't process git-ls-tree line : $_");
        }

        my($mode, $git_type, $git_hash, $git_filename) = ($1, $2, $3, $4);
        push @$tree, $self->getMetaFromCommithash($git_filename,$hash);
    }

    return $tree;
}

=head2 getRevisionDirMap

A "revision dir map" contains all the plain-file filenames associated
with a particular revision (tree-ish), organized by directory:

  $type = $out->{$dir}{$fullName}

The type of each is "F" (for ordinary file) or "D" (for directory,
for which the map $out->{$fullName} will also exist).

=cut

sub getRevisionDirMap
{
    my ($self,$ver)=@_;

    if(!defined($self->{revisionDirMapCache}))
    {
        $self->{revisionDirMapCache}={};
    }

        # Get file list (previously cached results are dependent on HEAD,
        # but are early in each case):
    my $cacheKey;
    my (@fileList);
    if( !defined($ver) || $ver eq "" )
    {
        $cacheKey="";
        if( defined($self->{revisionDirMapCache}{$cacheKey}) )
        {
            return $self->{revisionDirMapCache}{$cacheKey};
        }

        my @head = @{$self->gethead()};
        foreach my $file ( @head )
        {
            next if ( $file->{filehash} eq "deleted" );

            push @fileList,$file->{name};
        }
    }
    else
    {
        my ($hash)=$self->lookupCommitRef($ver);
        if( !defined($hash) )
        {
            return undef;
        }

        $cacheKey=$hash;
        if( defined($self->{revisionDirMapCache}{$cacheKey}) )
        {
            return $self->{revisionDirMapCache}{$cacheKey};
        }

        open(my $filePipe, '-|', 'git', 'ls-tree', '-z', '-r', $hash)
                or die("Cannot call git-ls-tree : $!");
        local $/ = "\0";
        while ( <$filePipe> )
        {
            chomp;
            unless ( /^(\d+)\s+(\w+)\s+([a-zA-Z0-9]+)\t(.*)$/o )
            {
                die("Couldn't process git-ls-tree line : $_");
            }

            my($mode, $git_type, $git_hash, $git_filename) = ($1, $2, $3, $4);

            push @fileList, $git_filename;
        }
        close $filePipe;
    }

        # Convert to normalized form:
    my %revMap;
    my $file;
    foreach $file (@fileList)
    {
        my($dir) = ($file=~m%^(?:(.*)/)?([^/]*)$%);
        $dir='' if(!defined($dir));

            # parent directories:
            # ... create empty dir maps for parent dirs:
        my($td)=$dir;
        while(!defined($revMap{$td}))
        {
            $revMap{$td}={};

            my($tp)=($td=~m%^(?:(.*)/)?([^/]*)$%);
            $tp='' if(!defined($tp));
            $td=$tp;
        }
            # ... add children to parent maps (now that they exist):
        $td=$dir;
        while($td ne "")
        {
            my($tp)=($td=~m%^(?:(.*)/)?([^/]*)$%);
            $tp='' if(!defined($tp));

            if(defined($revMap{$tp}{$td}))
            {
                if($revMap{$tp}{$td} ne 'D')
                {
                    die "Weird file/directory inconsistency in $cacheKey";
                }
                last;   # loop exit
            }
            $revMap{$tp}{$td}='D';

            $td=$tp;
        }

            # file
        $revMap{$dir}{$file}='F';
    }

        # Save in cache:
    $self->{revisionDirMapCache}{$cacheKey}=\%revMap;
    return $self->{revisionDirMapCache}{$cacheKey};
}

=head2 getlog

See also gethistorydense().

=cut

sub getlog
{
    my $self = shift;
    my $filename = shift;
    my $revFilter = shift;

    my $tablename = $self->tablename("revision");

    # Filters:
    # TODO: date, state, or by specific logins filters?
    # TODO: Handle comma-separated list of revFilter items, each item
    #   can be a range [only case currently handled] or individual
    #   rev or branch or "branch.".
    # TODO: Adjust $db_query WHERE clause based on revFilter, instead of
    #   manually filtering the results of the query?
    my ( $minrev, $maxrev );
    if( defined($revFilter) and
        $state->{opt}{r} =~ /^(1.(\d+))?(::?)(1.(\d.+))?$/ )
    {
        my $control = $3;
        $minrev = $2;
        $maxrev = $5;
        $minrev++ if ( defined($minrev) and $control eq "::" );
    }

    my $db_query = $self->{dbh}->prepare_cached("SELECT name, filehash, author, mode, revision, modified, commithash FROM $tablename WHERE name=? ORDER BY revision DESC",{},1);
    $db_query->execute($filename);

    my $totalRevs=0;
    my $tree = [];
    while ( my $file = $db_query->fetchrow_hashref )
    {
        $totalRevs++;
        if( defined($minrev) and $file->{revision} < $minrev )
        {
            next;
        }
        if( defined($maxrev) and $file->{revision} > $maxrev )
        {
            next;
        }

        $file->{revision} = "1." . $file->{revision};
        push @$tree, $file;
    }

    return ($tree,$totalRevs);
}

=head2 getmeta

This function takes a filename (with path) argument and returns a hashref of
metadata for that file.

There are several ways $revision can be specified:

   - A reference to hash that contains a "tag" that is the
     actual revision (one of the below).  TODO: Also allow it to
     specify a "date" in the hash.
   - undef, to refer to the latest version on the main branch.
   - Full CVS client revision number (mapped to integer in DB, without the
     "1." prefix),
   - Complex CVS-compatible "special" revision number for
     non-linear history (see comment below)
   - git commit sha1 hash
   - branch or tag name

=cut

sub getmeta
{
    my $self = shift;
    my $filename = shift;
    my $revision = shift;
    my $tablename_rev = $self->tablename("revision");
    my $tablename_head = $self->tablename("head");

    if ( ref($revision) eq "HASH" )
    {
        $revision = $revision->{tag};
    }

    # Overview of CVS revision numbers:
    #
    # General CVS numbering scheme:
    #   - Basic mainline branch numbers: "1.1", "1.2", "1.3", etc.
    #   - Result of "cvs checkin -r" (possible, but not really
    #     recommended): "2.1", "2.2", etc
    #   - Branch tag: "1.2.0.n", where "1.2" is revision it was branched
    #     from, "0" is a magic placeholder that identifies it as a
    #     branch tag instead of a version tag, and n is 2 times the
    #     branch number off of "1.2", starting with "2".
    #   - Version on a branch: "1.2.n.x", where "1.2" is branch-from, "n"
    #     is branch number off of "1.2" (like n above), and "x" is
    #     the version number on the branch.
    #   - Branches can branch off of branches: "1.3.2.7.4.1" (even number
    #     of components).
    #   - Odd "n"s are used by "vendor branches" that result
    #     from "cvs import".  Vendor branches have additional
    #     strangeness in the sense that the main rcs "head" of the main
    #     branch will (temporarily until first normal commit) point
    #     to the version on the vendor branch, rather than the actual
    #     main branch.  (FUTURE: This may provide an opportunity
    #     to use "strange" revision numbers for fast-forward-merged
    #     branch tip when CVS client is asking for the main branch.)
    #
    # git-cvsserver CVS-compatible special numbering schemes:
    #   - Currently git-cvsserver only tries to be identical to CVS for
    #     simple "1.x" numbers on the "main" branch (as identified
    #     by the module name that was originally cvs checkout'ed).
    #   - The database only stores the "x" part, for historical reasons.
    #     But most of the rest of the cvsserver preserves
    #     and thinks using the full revision number.
    #   - To handle non-linear history, it uses a version of the form
    #     "2.1.1.2000.b.b.b."..., where the 2.1.1.2000 is to help uniquely
    #     identify this as a special revision number, and there are
    #     20 b's that together encode the sha1 git commit from which
    #     this version of this file originated.  Each b is
    #     the numerical value of the corresponding byte plus
    #     100.
    #      - "plus 100" avoids "0"s, and also reduces the
    #        likelihood of a collision in the case that someone someday
    #        writes an import tool that tries to preserve original
    #        CVS revision numbers, and the original CVS data had done
    #        lots of branches off of branches and other strangeness to
    #        end up with a real version number that just happens to look
    #        like this special revision number form.  Also, if needed
    #        there are several ways to extend/identify alternative encodings
    #        within the "2.1.1.2000" part if necessary.
    #      - Unlike real CVS revisions, you can't really reconstruct what
    #        relation a revision of this form has to other revisions.
    #   - FUTURE: TODO: Rework database somehow to make up and remember
    #     fully-CVS-compatible branches and branch version numbers.

    my $meta;
    if ( defined($revision) )
    {
        if ( $revision =~ /^1\.(\d+)$/ )
        {
            my ($intRev) = $1;
            my $db_query;
            $db_query = $self->{dbh}->prepare_cached(
                "SELECT * FROM $tablename_rev WHERE name=? AND revision=?",
                {},1);
            $db_query->execute($filename, $intRev);
            $meta = $db_query->fetchrow_hashref;
        }
        elsif ( $revision =~ /^2\.1\.1\.2000(\.[1-3][0-9][0-9]){$state->{rawsz}}$/ )
        {
            my ($commitHash)=($revision=~/^2\.1\.1\.2000(.*)$/);
            $commitHash=~s/\.([0-9]+)/sprintf("%02x",$1-100)/eg;
            if($commitHash=~/^[0-9a-f]{$state->{hexsz}}$/)
            {
                return $self->getMetaFromCommithash($filename,$commitHash);
            }

            # error recovery: fall back on head version below
            print "E Failed to find $filename version=$revision or commit=$commitHash\n";
            $log->warning("failed get $revision with commithash=$commitHash");
            undef $revision;
        }
        elsif ( $revision =~ /^[0-9a-f]{$state->{hexsz}}$/ )
        {
            # Try DB first.  This is mostly only useful for req_annotate(),
            # which only calls this for stuff that should already be in
            # the DB.  It is fairly likely to be a waste of time
            # in most other cases [unless the file happened to be
            # modified in $revision specifically], but
            # it is probably in the noise compared to how long
            # getMetaFromCommithash() will take.
            my $db_query;
            $db_query = $self->{dbh}->prepare_cached(
                "SELECT * FROM $tablename_rev WHERE name=? AND commithash=?",
                {},1);
            $db_query->execute($filename, $revision);
            $meta = $db_query->fetchrow_hashref;

            if(! $meta)
            {
                my($revCommit)=$self->lookupCommitRef($revision);
                if($revCommit=~/^[0-9a-f]{$state->{hexsz}}$/)
                {
                    return $self->getMetaFromCommithash($filename,$revCommit);
                }

                # error recovery: nothing found:
                print "E Failed to find $filename version=$revision\n";
                $log->warning("failed get $revision");
                return $meta;
            }
        }
        else
        {
            my($revCommit)=$self->lookupCommitRef($revision);
            if($revCommit=~/^[0-9a-f]{$state->{hexsz}}$/)
            {
                return $self->getMetaFromCommithash($filename,$revCommit);
            }

            # error recovery: fall back on head version below
            print "E Failed to find $filename version=$revision\n";
            $log->warning("failed get $revision");
            undef $revision;  # Allow fallback
        }
    }

    if(!defined($revision))
    {
        my $db_query;
        $db_query = $self->{dbh}->prepare_cached(
                "SELECT * FROM $tablename_head WHERE name=?",{},1);
        $db_query->execute($filename);
        $meta = $db_query->fetchrow_hashref;
    }

    if($meta)
    {
        $meta->{revision} = "1.$meta->{revision}";
    }
    return $meta;
}

sub getMetaFromCommithash
{
    my $self = shift;
    my $filename = shift;
    my $revCommit = shift;

    # NOTE: This function doesn't scale well (lots of forks), especially
    #   if you have many files that have not been modified for many commits
    #   (each git-rev-parse redoes a lot of work for each file
    #   that theoretically could be done in parallel by smarter
    #   graph traversal).
    #
    # TODO: Possible optimization strategies:
    #   - Solve the issue of assigning and remembering "real" CVS
    #     revision numbers for branches, and ensure the
    #     data structure can do this efficiently.  Perhaps something
    #     similar to "git notes", and carefully structured to take
    #     advantage same-sha1-is-same-contents, to roll the same
    #     unmodified subdirectory data onto multiple commits?
    #   - Write and use a C tool that is like git-blame, but
    #     operates on multiple files with file granularity, instead
    #     of one file with line granularity.  Cache
    #     most-recently-modified in $self->{commitRefCache}{$revCommit}.
    #     Try to be intelligent about how many files we do with
    #     one fork (perhaps one directory at a time, without recursion,
    #     and/or include directory as one line item, recurse from here
    #     instead of in C tool?).
    #   - Perhaps we could ask the DB for (filename,fileHash),
    #     and just guess that it is correct (that the file hadn't
    #     changed between $revCommit and the found commit, then
    #     changed back, confusing anything trying to interpret
    #     history).  Probably need to add another index to revisions
    #     DB table for this.
    #   - NOTE: Trying to store all (commit,file) keys in DB [to
    #     find "lastModfiedCommit] (instead of
    #     just files that changed in each commit as we do now) is
    #     probably not practical from a disk space perspective.

        # Does the file exist in $revCommit?
    # TODO: Include file hash in dirmap cache.
    my($dirMap)=$self->getRevisionDirMap($revCommit);
    my($dir,$file)=($filename=~m%^(?:(.*)/)?([^/]*$)%);
    if(!defined($dir))
    {
        $dir="";
    }
    if( !defined($dirMap->{$dir}) ||
        !defined($dirMap->{$dir}{$filename}) )
    {
        my($fileHash)="deleted";

        my($retVal)={};
        $retVal->{name}=$filename;
        $retVal->{filehash}=$fileHash;

            # not needed and difficult to compute:
        $retVal->{revision}="0";  # $revision;
        $retVal->{commithash}=$revCommit;
        #$retVal->{author}=$commit->{author};
        #$retVal->{modified}=convertToCvsDate($commit->{date});
        #$retVal->{mode}=convertToDbMode($mode);

        return $retVal;
    }

    my($fileHash) = ::safe_pipe_capture("git","rev-parse","$revCommit:$filename");
    chomp $fileHash;
    if(!($fileHash=~/^[0-9a-f]{$state->{hexsz}}$/))
    {
        die "Invalid fileHash '$fileHash' looking up"
                    ." '$revCommit:$filename'\n";
    }

    # information about most recent commit to modify $filename:
    open(my $gitLogPipe, '-|', 'git', 'rev-list',
         '--max-count=1', '--pretty', '--parents',
         $revCommit, '--', $filename)
                or die "Cannot call git-rev-list: $!";
    my @commits=readCommits($gitLogPipe);
    close $gitLogPipe;
    if(scalar(@commits)!=1)
    {
        die "Can't find most recent commit changing $filename\n";
    }
    my($commit)=$commits[0];
    if( !defined($commit) || !defined($commit->{hash}) )
    {
        return undef;
    }

    # does this (commit,file) have a real assigned CVS revision number?
    my $tablename_rev = $self->tablename("revision");
    my $db_query;
    $db_query = $self->{dbh}->prepare_cached(
        "SELECT * FROM $tablename_rev WHERE name=? AND commithash=?",
        {},1);
    $db_query->execute($filename, $commit->{hash});
    my($meta)=$db_query->fetchrow_hashref;
    if($meta)
    {
        $meta->{revision} = "1.$meta->{revision}";
        return $meta;
    }

    # fall back on special revision number
    my($revision)=$commit->{hash};
    $revision=~s/(..)/'.' . (hex($1)+100)/eg;
    $revision="2.1.1.2000$revision";

    # meta data about $filename:
    open(my $filePipe, '-|', 'git', 'ls-tree', '-z',
                $commit->{hash}, '--', $filename)
            or die("Cannot call git-ls-tree : $!");
    local $/ = "\0";
    my $line;
    $line=<$filePipe>;
    if(defined(<$filePipe>))
    {
        die "Expected only a single file for git-ls-tree $filename\n";
    }
    close $filePipe;

    chomp $line;
    unless ( $line=~m/^(\d+)\s+(\w+)\s+([a-zA-Z0-9]+)\t(.*)$/o )
    {
        die("Couldn't process git-ls-tree line : $line\n");
    }
    my ( $mode, $git_type, $git_hash, $git_filename ) = ( $1, $2, $3, $4 );

    # save result:
    my($retVal)={};
    $retVal->{name}=$filename;
    $retVal->{revision}=$revision;
    $retVal->{filehash}=$fileHash;
    $retVal->{commithash}=$revCommit;
    $retVal->{author}=$commit->{author};
    $retVal->{modified}=convertToCvsDate($commit->{date});
    $retVal->{mode}=convertToDbMode($mode);

    return $retVal;
}

=head2 lookupCommitRef

Convert tag/branch/abbreviation/etc into a commit sha1 hash.  Caches
the result so looking it up again is fast.

=cut

sub lookupCommitRef
{
    my $self = shift;
    my $ref = shift;

    my $commitHash = $self->{commitRefCache}{$ref};
    if(defined($commitHash))
    {
        return $commitHash;
    }

    $commitHash = ::safe_pipe_capture("git","rev-parse","--verify","--quiet",
				      $self->unescapeRefName($ref));
    $commitHash=~s/\s*$//;
    if(!($commitHash=~/^[0-9a-f]{$state->{hexsz}}$/))
    {
        $commitHash=undef;
    }

    if( defined($commitHash) )
    {
        my $type = ::safe_pipe_capture("git","cat-file","-t",$commitHash);
        if( ! ($type=~/^commit\s*$/ ) )
        {
            $commitHash=undef;
        }
    }
    if(defined($commitHash))
    {
        $self->{commitRefCache}{$ref}=$commitHash;
    }
    return $commitHash;
}

=head2 clearCommitRefCaches

Clears cached commit cache (sha1's for various tags/abbeviations/etc),
and related caches.

=cut

sub clearCommitRefCaches
{
    my $self = shift;
    $self->{commitRefCache} = {};
    $self->{revisionDirMapCache} = undef;
    $self->{gethead_cache} = undef;
}

=head2 commitmessage

this function takes a commithash and returns the commit message for that commit

=cut
sub commitmessage
{
    my $self = shift;
    my $commithash = shift;
    my $tablename = $self->tablename("commitmsgs");

    die("Need commithash") unless ( defined($commithash) and $commithash =~ /^[a-zA-Z0-9]{$state->{hexsz}}$/ );

    my $db_query;
    $db_query = $self->{dbh}->prepare_cached("SELECT value FROM $tablename WHERE key=?",{},1);
    $db_query->execute($commithash);

    my ( $message ) = $db_query->fetchrow_array;

    if ( defined ( $message ) )
    {
        $message .= " " if ( $message =~ /\n$/ );
        return $message;
    }

    my @lines = ::safe_pipe_capture("git", "cat-file", "commit", $commithash);
    shift @lines while ( $lines[0] =~ /\S/ );
    $message = join("",@lines);
    $message .= " " if ( $message =~ /\n$/ );
    return $message;
}

=head2 gethistorydense

This function takes a filename (with path) argument and returns an arrayofarrays
containing revision,filehash,commithash ordered by revision descending.

This version of gethistory skips deleted entries -- so it is useful for annotate.
The 'dense' part is a reference to a '--dense' option available for git-rev-list
and other git tools that depend on it.

See also getlog().

=cut
sub gethistorydense
{
    my $self = shift;
    my $filename = shift;
    my $tablename = $self->tablename("revision");

    my $db_query;
    $db_query = $self->{dbh}->prepare_cached("SELECT revision, filehash, commithash FROM $tablename WHERE name=? AND filehash!='deleted' ORDER BY revision DESC",{},1);
    $db_query->execute($filename);

    my $result = $db_query->fetchall_arrayref;

    my $i;
    for($i=0 ; $i<scalar(@$result) ; $i++)
    {
        $result->[$i][0]="1." . $result->[$i][0];
    }

    return $result;
}

=head2 escapeRefName

Apply an escape mechanism to compensate for characters that
git ref names can have that CVS tags can not.

=cut
sub escapeRefName
{
    my($self,$refName)=@_;

    # CVS officially only allows [-_A-Za-z0-9] in tag names (or in
    # many contexts it can also be a CVS revision number).
    #
    # Git tags commonly use '/' and '.' as well, but also handle
    # anything else just in case:
    #
    #   = "_-s-"  For '/'.
    #   = "_-p-"  For '.'.
    #   = "_-u-"  For underscore, in case someone wants a literal "_-" in
    #     a tag name.
    #   = "_-xx-" Where "xx" is the hexadecimal representation of the
    #     desired ASCII character byte. (for anything else)

    if(! $refName=~/^[1-9][0-9]*(\.[1-9][0-9]*)*$/)
    {
        $refName=~s/_-/_-u--/g;
        $refName=~s/\./_-p-/g;
        $refName=~s%/%_-s-%g;
        $refName=~s/[^-_a-zA-Z0-9]/sprintf("_-%02x-",$1)/eg;
    }
}

=head2 unescapeRefName

Undo an escape mechanism to compensate for characters that
git ref names can have that CVS tags can not.

=cut
sub unescapeRefName
{
    my($self,$refName)=@_;

    # see escapeRefName() for description of escape mechanism.

    $refName=~s/_-([spu]|[0-9a-f][0-9a-f])-/unescapeRefNameChar($1)/eg;

    # allowed tag names
    # TODO: Perhaps use git check-ref-format, with an in-process cache of
    #  validated names?
    if( !( $refName=~m%^[^-][-a-zA-Z0-9_/.]*$% ) ||
        ( $refName=~m%[/.]$% ) ||
        ( $refName=~/\.lock$/ ) ||
        ( $refName=~m%\.\.|/\.|[[\\:?*~]|\@\{% ) )  # matching }
    {
        # Error:
        $log->warn("illegal refName: $refName");
        $refName=undef;
    }
    return $refName;
}

sub unescapeRefNameChar
{
    my($char)=@_;

    if($char eq "s")
    {
        $char="/";
    }
    elsif($char eq "p")
    {
        $char=".";
    }
    elsif($char eq "u")
    {
        $char="_";
    }
    elsif($char=~/^[0-9a-f][0-9a-f]$/)
    {
        $char=chr(hex($char));
    }
    else
    {
        # Error case: Maybe it has come straight from user, and
        # wasn't supposed to be escaped?  Restore it the way we got it:
        $char="_-$char-";
    }

    return $char;
}

=head2 in_array()

from Array::PAT - mimics the in_array() function
found in PHP. Yuck but works for small arrays.

=cut
sub in_array
{
    my ($check, @array) = @_;
    my $retval = 0;
    foreach my $test (@array){
        if($check eq $test){
            $retval =  1;
        }
    }
    return $retval;
}

=head2 mangle_dirname

create a string from a directory name that is suitable to use as
part of a filename, mainly by converting all chars except \w.- to _

=cut
sub mangle_dirname {
    my $dirname = shift;
    return unless defined $dirname;

    $dirname =~ s/[^\w.-]/_/g;

    return $dirname;
}

=head2 mangle_tablename

create a string from a that is suitable to use as part of an SQL table
name, mainly by converting all chars except \w to _

=cut
sub mangle_tablename {
    my $tablename = shift;
    return unless defined $tablename;

    $tablename =~ s/[^\w_]/_/g;

    return $tablename;
}

1;
