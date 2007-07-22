#!/usr/bin/perl

####
#### This application is a CVS emulation layer for git.
#### It is intended for clients to connect over SSH.
#### See the documentation for more details.
####
#### Copyright The Open University UK - 2006.
####
#### Authors: Martyn Smith    <martyn@catalyst.net.nz>
####          Martin Langhoff <martin@catalyst.net.nz>
####
####
#### Released under the GNU Public License, version 2.
####
####

use strict;
use warnings;
use bytes;

use Fcntl;
use File::Temp qw/tempdir tempfile/;
use File::Basename;
use Getopt::Long qw(:config require_order no_ignore_case);

my $VERSION = '@@GIT_VERSION@@';

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

my $methods = {
    'Root'            => \&req_Root,
    'Valid-responses' => \&req_Validresponses,
    'valid-requests'  => \&req_validrequests,
    'Directory'       => \&req_Directory,
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
    'watchers'        => \&req_CATCHALL,
    'editors'         => \&req_CATCHALL,
    'annotate'        => \&req_annotate,
    'Global_option'   => \&req_Globaloption,
    #'annotate'        => \&req_CATCHALL,
};

##############################################


# $state holds all the bits of information the clients sends us that could
# potentially be useful when it comes to actually _doing_ something.
my $state = { prependdir => '' };
$log->info("--------------- STARTING -----------------");

my $usage =
    "Usage: git-cvsserver [options] [pserver|server] [<directory> ...]\n".
    "    --base-path <path>  : Prepend to requested CVSROOT\n".
    "    --strict-paths      : Don't allow recursing into subdirectories\n".
    "    --export-all        : Don't check for gitcvs.enabled in config\n".
    "    --version, -V       : Print version information and exit\n".
    "    --help, -h, -H      : Print usage information and exit\n".
    "\n".
    "<directory> ... is a list of allowed directories. If no directories\n".
    "are given, all are allowed. This is an additional restriction, gitcvs\n".
    "access still needs to be enabled by the gitcvs.enabled config option.\n";

my @opts = ( 'help|h|H', 'version|V',
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
    die "--export-all can only be used together with an explicit whitelist\n";
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
    req_Root('root', $line) # reuse Root
       or die "E Invalid root $line \n";
    $line = <STDIN>; chomp $line;
    unless ($line eq 'anonymous') {
       print "E Only anonymous user allowed via pserver\n";
       print "I HATE YOU\n";
       exit 1;
    }
    $line = <STDIN>; chomp $line;    # validate the password?
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

# Magic catchall method.
#    This is the method that will handle all commands we haven't yet
#    implemented. It simply sends a warning to the log file indicating a
#    command that hasn't been implemented has been invoked.
sub req_CATCHALL
{
    my ( $cmd, $data ) = @_;
    $log->warn("Unhandled command : req_$cmd : $data");
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

    my @gitvars = `git-config -l`;
    if ($?) {
       print "E problems executing git-config on the server -- this is not a git repository or the PATH is not set correctly.\n";
        print "E \n";
        print "error 1 - problem executing git-config\n";
       return 0;
    }
    foreach my $line ( @gitvars )
    {
        next unless ( $line =~ /^(gitcvs)\.(?:(ext|pserver)\.)?([\w-]+)=(.*)$/ );
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

    $log->debug("SEND : Valid-requests " . join(" ",keys %$methods));
    $log->debug("SEND : ok");

    print "Valid-requests " . join(" ",keys %$methods) . "\n";
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
    $state->{path} =~ s/^$state->{CVSROOT}\///;
    $state->{module} = $1 if ($state->{path} =~ s/^(.*?)(\/|$)//);
    $state->{path} .= "/" if ( $state->{path} =~ /\S/ );

    $state->{directory} = $state->{localdir};
    $state->{directory} = "" if ( $state->{directory} eq "." );
    $state->{directory} .= "/" if ( $state->{directory} =~ /\S/ );

    if ( (not defined($state->{prependdir}) or $state->{prependdir} eq '') and $state->{localdir} eq "." and $state->{path} =~ /\S/ )
    {
        $log->info("Setting prepend to '$state->{path}'");
        $state->{prependdir} = $state->{path};
        foreach my $entry ( keys %{$state->{entries}} )
        {
            $state->{entries}{$state->{prependdir} . $entry} = $state->{entries}{$entry};
            delete $state->{entries}{$entry};
        }
    }

    if ( defined ( $state->{prependdir} ) )
    {
        $log->debug("Prepending '$state->{prependdir}' to state|directory");
        $state->{directory} = $state->{prependdir} . $state->{directory}
    }
    $log->debug("req_Directory : localdir=$data repository=$repository path=$state->{path} directory=$state->{directory} module=$state->{module}");
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

    my @data = split(/\//, $data);

    $state->{entries}{$state->{directory}.$data[1]} = {
        revision    => $data[2],
        conflict    => $data[3],
        options     => $data[4],
        tag_or_date => $data[5],
    };

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

    argsfromdir($updater);

    my $addcount = 0;

    foreach my $filename ( @{$state->{args}} )
    {
        $filename = filecleanup($filename);

        my $meta = $updater->getmeta($filename);
        my $wrev = revparse($filename);

        if ($wrev && $meta && ($wrev < 0))
        {
            # previously removed file, add back
            $log->info("added file $filename was previously removed, send 1.$meta->{revision}");

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
                my $kopts = kopts_from_path($filepart);
                $log->debug("/$filepart/1.$meta->{revision}//$kopts/");
                print "/$filepart/1.$meta->{revision}//$kopts/\n";
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
        my $kopts = kopts_from_path($filepart);
        print "/$filepart/0//$kopts/\n";

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

        my $meta = $updater->getmeta($filename);
        my $wrev = revparse($filename);

        unless ( defined ( $wrev ) )
        {
            print "E cvs remove: nothing known about `$filename'\n";
            next;
        }

        if ( defined($wrev) and $wrev < 0 )
        {
            print "E cvs remove: file `$filename' already scheduled for removal\n";
            next;
        }

        unless ( $wrev == $meta->{revision} )
        {
            # TODO : not sure if the format of this message is quite correct.
            print "E cvs remove: Up to date check failed for `$filename'\n";
            next;
        }


        my ( $filepart, $dirpart ) = filenamesplit($filename, 1);

        print "E cvs remove: scheduling `$filename' for removal\n";

        print "Checked-in $dirpart\n";
        print "$filename\n";
        my $kopts = kopts_from_path($filepart);
        print "/$filepart/-1.$wrev//$kopts/\n";

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
    $state->{entries}{$state->{directory}.$data}{modified_hash} = `git-hash-object $filename`;
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

    my $module = $state->{args}[0];
    my $checkout_path = $module;

    # use the user specified directory if we're given it
    $checkout_path = $state->{opt}{d} if ( exists ( $state->{opt}{d} ) );

    $log->debug("req_co : " . ( defined($data) ? $data : "[NULL]" ) );

    $log->info("Checking out module '$module' ($state->{CVSROOT}) to '$checkout_path'");

    $ENV{GIT_DIR} = $state->{CVSROOT} . "/";

    # Grab a handle to the SQLite db and do any necessary updates
    my $updater = GITCVS::updater->new($state->{CVSROOT}, $module, $log);
    $updater->update();

    $checkout_path =~ s|/$||; # get rid of trailing slashes

    # Eclipse seems to need the Clear-sticky command
    # to prepare the 'Entries' file for the new directory.
    print "Clear-sticky $checkout_path/\n";
    print $state->{CVSROOT} . "/$module/\n";
    print "Clear-static-directory $checkout_path/\n";
    print $state->{CVSROOT} . "/$module/\n";
    print "Clear-sticky $checkout_path/\n"; # yes, twice
    print $state->{CVSROOT} . "/$module/\n";
    print "Template $checkout_path/\n";
    print $state->{CVSROOT} . "/$module/\n";
    print "0\n";

    # instruct the client that we're checking out to $checkout_path
    print "E cvs checkout: Updating $checkout_path\n";

    my %seendirs = ();
    my $lastdir ='';

    # recursive
    sub prepdir {
       my ($dir, $repodir, $remotedir, $seendirs) = @_;
       my $parent = dirname($dir);
       $dir       =~ s|/+$||;
       $repodir   =~ s|/+$||;
       $remotedir =~ s|/+$||;
       $parent    =~ s|/+$||;
       $log->debug("announcedir $dir, $repodir, $remotedir" );

       if ($parent eq '.' || $parent eq './') {
           $parent = '';
       }
       # recurse to announce unseen parents first
       if (length($parent) && !exists($seendirs->{$parent})) {
           prepdir($parent, $repodir, $remotedir, $seendirs);
       }
       # Announce that we are going to modify at the parent level
       if ($parent) {
           print "E cvs checkout: Updating $remotedir/$parent\n";
       } else {
           print "E cvs checkout: Updating $remotedir\n";
       }
       print "Clear-sticky $remotedir/$parent/\n";
       print "$repodir/$parent/\n";

       print "Clear-static-directory $remotedir/$dir/\n";
       print "$repodir/$dir/\n";
       print "Clear-sticky $remotedir/$parent/\n"; # yes, twice
       print "$repodir/$parent/\n";
       print "Template $remotedir/$dir/\n";
       print "$repodir/$dir/\n";
       print "0\n";

       $seendirs->{$dir} = 1;
    }

    foreach my $git ( @{$updater->gethead} )
    {
        # Don't want to check out deleted files
        next if ( $git->{filehash} eq "deleted" );

        ( $git->{name}, $git->{dir} ) = filenamesplit($git->{name});

       if (length($git->{dir}) && $git->{dir} ne './'
           && $git->{dir} ne $lastdir ) {
           unless (exists($seendirs{$git->{dir}})) {
               prepdir($git->{dir}, $state->{CVSROOT} . "/$module/",
                       $checkout_path, \%seendirs);
               $lastdir = $git->{dir};
               $seendirs{$git->{dir}} = 1;
           }
           print "E cvs checkout: Updating /$checkout_path/$git->{dir}\n";
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
        my $kopts = kopts_from_path($git->{name});
        print "/$git->{name}/1.$git->{revision}//$kopts/\n";
        # permissions
        print "u=$git->{mode},g=$git->{mode},o=$git->{mode}\n";

        # transmit file
        transmitfile($git->{filehash});
    }

    print "ok\n";

    statecleanup();
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
	my $heads_dir = $state->{CVSROOT} . '/refs/heads';
	if (!opendir HEADS, $heads_dir) {
	    print "E [server aborted]: Failed to open directory, "
	      . "$heads_dir: $!\nerror\n";
	    return 0;
	}
        print "E cvs update: Updating .\n";
	while (my $head = readdir(HEADS)) {
	    if (-f $state->{CVSROOT} . '/refs/heads/' . $head) {
	        print "E cvs update: New directory `$head'\n";
	    }
	}
	closedir HEADS;
	print "ok\n";
	return 1;
    }


    # Grab a handle to the SQLite db and do any necessary updates
    my $updater = GITCVS::updater->new($state->{CVSROOT}, $state->{module}, $log);

    $updater->update();

    argsfromdir($updater);

    #$log->debug("update state : " . Dumper($state));

    # foreach file specified on the command line ...
    foreach my $filename ( @{$state->{args}} )
    {
        $filename = filecleanup($filename);

        $log->debug("Processing file $filename");

        # if we have a -C we should pretend we never saw modified stuff
        if ( exists ( $state->{opt}{C} ) )
        {
            delete $state->{entries}{$filename}{modified_hash};
            delete $state->{entries}{$filename}{modified_filename};
            $state->{entries}{$filename}{unchanged} = 1;
        }

        my $meta;
        if ( defined($state->{opt}{r}) and $state->{opt}{r} =~ /^1\.(\d+)/ )
        {
            $meta = $updater->getmeta($filename, $1);
        } else {
            $meta = $updater->getmeta($filename);
        }

	if ( ! defined $meta )
	{
	    $meta = {
	        name => $filename,
	        revision => 0,
	        filehash => 'added'
	    };
	}

        my $oldmeta = $meta;

        my $wrev = revparse($filename);

        # If the working copy is an old revision, lets get that version too for comparison.
        if ( defined($wrev) and $wrev != $meta->{revision} )
        {
            $oldmeta = $updater->getmeta($filename, $wrev);
        }

        #$log->debug("Target revision is $meta->{revision}, current working revision is $wrev");

        # Files are up to date if the working copy and repo copy have the same revision,
        # and the working copy is unmodified _and_ the user hasn't specified -C
        next if ( defined ( $wrev )
                  and defined($meta->{revision})
                  and $wrev == $meta->{revision}
                  and $state->{entries}{$filename}{unchanged}
                  and not exists ( $state->{opt}{C} ) );

        # If the working copy and repo copy have the same revision,
        # but the working copy is modified, tell the client it's modified
        if ( defined ( $wrev )
             and defined($meta->{revision})
             and $wrev == $meta->{revision}
             and defined($state->{entries}{$filename}{modified_hash})
             and not exists ( $state->{opt}{C} ) )
        {
            $log->info("Tell the client the file is modified");
            print "MT text M \n";
            print "MT fname $filename\n";
            print "MT newline\n";
            next;
        }

        if ( $meta->{filehash} eq "deleted" )
        {
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
	    if ( defined($wrev) && $wrev < 0 )
	    {
	        $log->info("Tell the client the file is scheduled for removal");
		print "MT text R \n";
                print "MT fname $filename\n";
                print "MT newline\n";
		next;
	    }
	    elsif ( (!defined($wrev) || $wrev == 0) && (!defined($meta->{revision}) || $meta->{revision} == 0) )
	    {
	        $log->info("Tell the client the file is scheduled for addition");
		print "MT text A \n";
                print "MT fname $filename\n";
                print "MT newline\n";
		next;

	    }
	    else {
                $log->info("Updating '$filename' to ".$meta->{revision});
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
		    print "Clear-static-directory $dirpart\n";
		    print $state->{CVSROOT} . "/$state->{module}/$dirpart\n";
		    print "Clear-sticky $dirpart\n";
		    print $state->{CVSROOT} . "/$state->{module}/$dirpart\n";

		    $log->debug("Creating new file 'Created $dirpart'");
		    print "Created $dirpart\n";
		}
		print $state->{CVSROOT} . "/$state->{module}/$filename\n";

		# this is an "entries" line
		my $kopts = kopts_from_path($filepart);
		$log->debug("/$filepart/1.$meta->{revision}//$kopts/");
		print "/$filepart/1.$meta->{revision}//$kopts/\n";

		# permissions
		$log->debug("SEND : u=$meta->{mode},g=$meta->{mode},o=$meta->{mode}");
		print "u=$meta->{mode},g=$meta->{mode},o=$meta->{mode}\n";

		# transmit file
		transmitfile($meta->{filehash});
	    }
        } else {
            $log->info("Updating '$filename'");
            my ( $filepart, $dirpart ) = filenamesplit($meta->{name},1);

            my $dir = tempdir( DIR => $TEMP_DIR, CLEANUP => 1 ) . "/";

            chdir $dir;
            my $file_local = $filepart . ".mine";
            system("ln","-s",$state->{entries}{$filename}{modified_filename}, $file_local);
            my $file_old = $filepart . "." . $oldmeta->{revision};
            transmitfile($oldmeta->{filehash}, $file_old);
            my $file_new = $filepart . "." . $meta->{revision};
            transmitfile($meta->{filehash}, $file_new);

            # we need to merge with the local changes ( M=successful merge, C=conflict merge )
            $log->info("Merging $file_local, $file_old, $file_new");
            print "M Merging differences between 1.$oldmeta->{revision} and 1.$meta->{revision} into $filename\n";

            $log->debug("Temporary directory for merge is $dir");

            my $return = system("git", "merge-file", $file_local, $file_old, $file_new);
            $return >>= 8;

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
                    my $kopts = kopts_from_path($filepart);
                    $log->debug("/$filepart/1.$meta->{revision}//$kopts/");
                    print "/$filepart/1.$meta->{revision}//$kopts/\n";
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
                    my $kopts = kopts_from_path($filepart);
                    print "/$filepart/1.$meta->{revision}/+/$kopts/\n";
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
                my $data = `cat $file_local`;
                $log->debug("File size : " . length($data));
                print length($data) . "\n";
                print $data;
            }

            chdir "/";
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

    if ( $state->{method} eq 'pserver')
    {
        print "error 1 pserver access cannot commit\n";
        exit;
    }

    if ( -e $state->{CVSROOT} . "/index" )
    {
        $log->warn("file 'index' already exists in the git repository");
        print "error 1 Index already exists in git repo\n";
        exit;
    }

    # Grab a handle to the SQLite db and do any necessary updates
    my $updater = GITCVS::updater->new($state->{CVSROOT}, $state->{module}, $log);
    $updater->update();

    my $tmpdir = tempdir ( DIR => $TEMP_DIR );
    my ( undef, $file_index ) = tempfile ( DIR => $TEMP_DIR, OPEN => 0 );
    $log->info("Lockless commit start, basing commit on '$tmpdir', index file is '$file_index'");

    $ENV{GIT_DIR} = $state->{CVSROOT} . "/";
    $ENV{GIT_INDEX_FILE} = $file_index;

    # Remember where the head was at the beginning.
    my $parenthash = `git show-ref -s refs/heads/$state->{module}`;
    chomp $parenthash;
    if ($parenthash !~ /^[0-9a-f]{40}$/) {
	    print "error 1 pserver cannot find the current HEAD of module";
	    exit;
    }

    chdir $tmpdir;

    # populate the temporary index based
    system("git-read-tree", $parenthash);
    unless ($? == 0)
    {
	die "Error running git-read-tree $state->{module} $file_index $!";
    }
    $log->info("Created index '$file_index' with for head $state->{module} - exit status $?");

    my @committedfiles = ();
    my %oldmeta;

    # foreach file specified on the command line ...
    foreach my $filename ( @{$state->{args}} )
    {
        my $committedfile = $filename;
        $filename = filecleanup($filename);

        next unless ( exists $state->{entries}{$filename}{modified_filename} or not $state->{entries}{$filename}{unchanged} );

        my $meta = $updater->getmeta($filename);
	$oldmeta{$filename} = $meta;

        my $wrev = revparse($filename);

        my ( $filepart, $dirpart ) = filenamesplit($filename);

        # do a checkout of the file if it part of this tree
        if ($wrev) {
            system('git-checkout-index', '-f', '-u', $filename);
            unless ($? == 0) {
                die "Error running git-checkout-index -f -u $filename : $!";
            }
        }

        my $addflag = 0;
        my $rmflag = 0;
        $rmflag = 1 if ( defined($wrev) and $wrev < 0 );
        $addflag = 1 unless ( -e $filename );

        # Do up to date checking
        unless ( $addflag or $wrev == $meta->{revision} or ( $rmflag and -$wrev == $meta->{revision} ) )
        {
            # fail everything if an up to date check fails
            print "error 1 Up to date check failed for $filename\n";
            chdir "/";
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
            system("git-update-index", "--remove", $filename);
        }
        elsif ( $addflag )
        {
            $log->info("Adding file '$filename'");
            system("git-update-index", "--add", $filename);
        } else {
            $log->info("Updating file '$filename'");
            system("git-update-index", $filename);
        }
    }

    unless ( scalar(@committedfiles) > 0 )
    {
        print "E No files to commit\n";
        print "ok\n";
        chdir "/";
        return;
    }

    my $treehash = `git-write-tree`;
    chomp $treehash;

    $log->debug("Treehash : $treehash, Parenthash : $parenthash");

    # write our commit message out if we have one ...
    my ( $msg_fh, $msg_filename ) = tempfile( DIR => $TEMP_DIR );
    print $msg_fh $state->{opt}{m};# if ( exists ( $state->{opt}{m} ) );
    print $msg_fh "\n\nvia git-CVS emulator\n";
    close $msg_fh;

    my $commithash = `git-commit-tree $treehash -p $parenthash < $msg_filename`;
    chomp($commithash);
    $log->info("Commit hash : $commithash");

    unless ( $commithash =~ /[a-zA-Z0-9]{40}/ )
    {
        $log->warn("Commit failed (Invalid commit hash)");
        print "error 1 Commit failed (unknown reason)\n";
        chdir "/";
        exit;
    }

	# Check that this is allowed, just as we would with a receive-pack
	my @cmd = ( $ENV{GIT_DIR}.'hooks/update', "refs/heads/$state->{module}",
			$parenthash, $commithash );
	if( -x $cmd[0] ) {
		unless( system( @cmd ) == 0 )
		{
			$log->warn("Commit failed (update hook declined to update ref)");
			print "error 1 Commit failed (update hook declined)\n";
			chdir "/";
			exit;
		}
	}

	if (system(qw(git update-ref -m), "cvsserver ci",
			"refs/heads/$state->{module}", $commithash, $parenthash)) {
		$log->warn("update-ref for $state->{module} failed.");
		print "error 1 Cannot commit -- update first\n";
		exit;
	}

    $updater->update();

    # foreach file specified on the command line ...
    foreach my $filename ( @committedfiles )
    {
        $filename = filecleanup($filename);

        my $meta = $updater->getmeta($filename);
	unless (defined $meta->{revision}) {
	  $meta->{revision} = 1;
	}

        my ( $filepart, $dirpart ) = filenamesplit($filename, 1);

        $log->debug("Checked-in $dirpart : $filename");

	print "M $state->{CVSROOT}/$state->{module}/$filename,v  <--  $dirpart$filepart\n";
        if ( defined $meta->{filehash} && $meta->{filehash} eq "deleted" )
        {
            print "M new revision: delete; previous revision: 1.$oldmeta{$filename}{revision}\n";
            print "Remove-entry $dirpart\n";
            print "$filename\n";
        } else {
            if ($meta->{revision} == 1) {
	        print "M initial revision: 1.1\n";
            } else {
	        print "M new revision: 1.$meta->{revision}; previous revision: 1.$oldmeta{$filename}{revision}\n";
            }
            print "Checked-in $dirpart\n";
            print "$filename\n";
            my $kopts = kopts_from_path($filepart);
            print "/$filepart/1.$meta->{revision}//$kopts/\n";
        }
    }

    chdir "/";
    print "ok\n";
}

sub req_status
{
    my ( $cmd, $data ) = @_;

    argsplit("status");

    $log->info("req_status : " . ( defined($data) ? $data : "[NULL]" ));
    #$log->debug("status state : " . Dumper($state));

    # Grab a handle to the SQLite db and do any necessary updates
    my $updater = GITCVS::updater->new($state->{CVSROOT}, $state->{module}, $log);
    $updater->update();

    # if no files were specified, we need to work out what files we should be providing status on ...
    argsfromdir($updater);

    # foreach file specified on the command line ...
    foreach my $filename ( @{$state->{args}} )
    {
        $filename = filecleanup($filename);

        my $meta = $updater->getmeta($filename);
        my $oldmeta = $meta;

        my $wrev = revparse($filename);

        # If the working copy is an old revision, lets get that version too for comparison.
        if ( defined($wrev) and $wrev != $meta->{revision} )
        {
            $oldmeta = $updater->getmeta($filename, $wrev);
        }

        # TODO : All possible statuses aren't yet implemented
        my $status;
        # Files are up to date if the working copy and repo copy have the same revision, and the working copy is unmodified
        $status = "Up-to-date" if ( defined ( $wrev ) and defined($meta->{revision}) and $wrev == $meta->{revision}
                                    and
                                    ( ( $state->{entries}{$filename}{unchanged} and ( not defined ( $state->{entries}{$filename}{conflict} ) or $state->{entries}{$filename}{conflict} !~ /^\+=/ ) )
                                      or ( defined($state->{entries}{$filename}{modified_hash}) and $state->{entries}{$filename}{modified_hash} eq $meta->{filehash} ) )
                                   );

        # Need checkout if the working copy has an older revision than the repo copy, and the working copy is unmodified
        $status ||= "Needs Checkout" if ( defined ( $wrev ) and defined ( $meta->{revision} ) and $meta->{revision} > $wrev
                                          and
                                          ( $state->{entries}{$filename}{unchanged}
                                            or ( defined($state->{entries}{$filename}{modified_hash}) and $state->{entries}{$filename}{modified_hash} eq $oldmeta->{filehash} ) )
                                        );

        # Need checkout if it exists in the repo but doesn't have a working copy
        $status ||= "Needs Checkout" if ( not defined ( $wrev ) and defined ( $meta->{revision} ) );

        # Locally modified if working copy and repo copy have the same revision but there are local changes
        $status ||= "Locally Modified" if ( defined ( $wrev ) and defined($meta->{revision}) and $wrev == $meta->{revision} and $state->{entries}{$filename}{modified_filename} );

        # Needs Merge if working copy revision is less than repo copy and there are local changes
        $status ||= "Needs Merge" if ( defined ( $wrev ) and defined ( $meta->{revision} ) and $meta->{revision} > $wrev and $state->{entries}{$filename}{modified_filename} );

        $status ||= "Locally Added" if ( defined ( $state->{entries}{$filename}{revision} ) and not defined ( $meta->{revision} ) );
        $status ||= "Locally Removed" if ( defined ( $wrev ) and defined ( $meta->{revision} ) and -$wrev == $meta->{revision} );
        $status ||= "Unresolved Conflict" if ( defined ( $state->{entries}{$filename}{conflict} ) and $state->{entries}{$filename}{conflict} =~ /^\+=/ );
        $status ||= "File had conflicts on merge" if ( 0 );

        $status ||= "Unknown";

        print "M ===================================================================\n";
        print "M File: $filename\tStatus: $status\n";
        if ( defined($state->{entries}{$filename}{revision}) )
        {
            print "M Working revision:\t" . $state->{entries}{$filename}{revision} . "\n";
        } else {
            print "M Working revision:\tNo entry for $filename\n";
        }
        if ( defined($meta->{revision}) )
        {
            print "M Repository revision:\t1." . $meta->{revision} . "\t$state->{CVSROOT}/$state->{module}/$filename,v\n";
            print "M Sticky Tag:\t\t(none)\n";
            print "M Sticky Date:\t\t(none)\n";
            print "M Sticky Options:\t\t(none)\n";
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

    $revision1 =~ s/^1\.// if ( defined ( $revision1 ) );
    $revision2 =~ s/^1\.// if ( defined ( $revision2 ) );

    $log->debug("Diffing revisions " . ( defined($revision1) ? $revision1 : "[NULL]" ) . " and " . ( defined($revision2) ? $revision2 : "[NULL]" ) );

    # Grab a handle to the SQLite db and do any necessary updates
    my $updater = GITCVS::updater->new($state->{CVSROOT}, $state->{module}, $log);
    $updater->update();

    # if no files were specified, we need to work out what files we should be providing status on ...
    argsfromdir($updater);

    # foreach file specified on the command line ...
    foreach my $filename ( @{$state->{args}} )
    {
        $filename = filecleanup($filename);

        my ( $fh, $file1, $file2, $meta1, $meta2, $filediff );

        my $wrev = revparse($filename);

        # We need _something_ to diff against
        next unless ( defined ( $wrev ) );

        # if we have a -r switch, use it
        if ( defined ( $revision1 ) )
        {
            ( undef, $file1 ) = tempfile( DIR => $TEMP_DIR, OPEN => 0 );
            $meta1 = $updater->getmeta($filename, $revision1);
            unless ( defined ( $meta1 ) and $meta1->{filehash} ne "deleted" )
            {
                print "E File $filename at revision 1.$revision1 doesn't exist\n";
                next;
            }
            transmitfile($meta1->{filehash}, $file1);
        }
        # otherwise we just use the working copy revision
        else
        {
            ( undef, $file1 ) = tempfile( DIR => $TEMP_DIR, OPEN => 0 );
            $meta1 = $updater->getmeta($filename, $wrev);
            transmitfile($meta1->{filehash}, $file1);
        }

        # if we have a second -r switch, use it too
        if ( defined ( $revision2 ) )
        {
            ( undef, $file2 ) = tempfile( DIR => $TEMP_DIR, OPEN => 0 );
            $meta2 = $updater->getmeta($filename, $revision2);

            unless ( defined ( $meta2 ) and $meta2->{filehash} ne "deleted" )
            {
                print "E File $filename at revision 1.$revision2 doesn't exist\n";
                next;
            }

            transmitfile($meta2->{filehash}, $file2);
        }
        # otherwise we just use the working copy
        else
        {
            $file2 = $state->{entries}{$filename}{modified_filename};
        }

        # if we have been given -r, and we don't have a $file2 yet, lets get one
        if ( defined ( $revision1 ) and not defined ( $file2 ) )
        {
            ( undef, $file2 ) = tempfile( DIR => $TEMP_DIR, OPEN => 0 );
            $meta2 = $updater->getmeta($filename, $wrev);
            transmitfile($meta2->{filehash}, $file2);
        }

        # We need to have retrieved something useful
        next unless ( defined ( $meta1 ) );

        # Files to date if the working copy and repo copy have the same revision, and the working copy is unmodified
        next if ( not defined ( $meta2 ) and $wrev == $meta1->{revision}
                  and
                   ( ( $state->{entries}{$filename}{unchanged} and ( not defined ( $state->{entries}{$filename}{conflict} ) or $state->{entries}{$filename}{conflict} !~ /^\+=/ ) )
                     or ( defined($state->{entries}{$filename}{modified_hash}) and $state->{entries}{$filename}{modified_hash} eq $meta1->{filehash} ) )
                  );

        # Apparently we only show diffs for locally modified files
        next unless ( defined($meta2) or defined ( $state->{entries}{$filename}{modified_filename} ) );

        print "M Index: $filename\n";
        print "M ===================================================================\n";
        print "M RCS file: $state->{CVSROOT}/$state->{module}/$filename,v\n";
        print "M retrieving revision 1.$meta1->{revision}\n" if ( defined ( $meta1 ) );
        print "M retrieving revision 1.$meta2->{revision}\n" if ( defined ( $meta2 ) );
        print "M diff ";
        foreach my $opt ( keys %{$state->{opt}} )
        {
            if ( ref $state->{opt}{$opt} eq "ARRAY" )
            {
                foreach my $value ( @{$state->{opt}{$opt}} )
                {
                    print "-$opt $value ";
                }
            } else {
                print "-$opt ";
                print "$state->{opt}{$opt} " if ( defined ( $state->{opt}{$opt} ) );
            }
        }
        print "$filename\n";

        $log->info("Diffing $filename -r $meta1->{revision} -r " . ( $meta2->{revision} or "workingcopy" ));

        ( $fh, $filediff ) = tempfile ( DIR => $TEMP_DIR );

        if ( exists $state->{opt}{u} )
        {
            system("diff -u -L '$filename revision 1.$meta1->{revision}' -L '$filename " . ( defined($meta2->{revision}) ? "revision 1.$meta2->{revision}" : "working copy" ) . "' $file1 $file2 > $filediff");
        } else {
            system("diff $file1 $file2 > $filediff");
        }

        while ( <$fh> )
        {
            print "M $_";
        }
        close $fh;
    }

    print "ok\n";
}

sub req_log
{
    my ( $cmd, $data ) = @_;

    argsplit("log");

    $log->debug("req_log : " . ( defined($data) ? $data : "[NULL]" ));
    #$log->debug("log state : " . Dumper($state));

    my ( $minrev, $maxrev );
    if ( defined ( $state->{opt}{r} ) and $state->{opt}{r} =~ /([\d.]+)?(::?)([\d.]+)?/ )
    {
        my $control = $2;
        $minrev = $1;
        $maxrev = $3;
        $minrev =~ s/^1\.// if ( defined ( $minrev ) );
        $maxrev =~ s/^1\.// if ( defined ( $maxrev ) );
        $minrev++ if ( defined($minrev) and $control eq "::" );
    }

    # Grab a handle to the SQLite db and do any necessary updates
    my $updater = GITCVS::updater->new($state->{CVSROOT}, $state->{module}, $log);
    $updater->update();

    # if no files were specified, we need to work out what files we should be providing status on ...
    argsfromdir($updater);

    # foreach file specified on the command line ...
    foreach my $filename ( @{$state->{args}} )
    {
        $filename = filecleanup($filename);

        my $headmeta = $updater->getmeta($filename);

        my $revisions = $updater->getlog($filename);
        my $totalrevisions = scalar(@$revisions);

        if ( defined ( $minrev ) )
        {
            $log->debug("Removing revisions less than $minrev");
            while ( scalar(@$revisions) > 0 and $revisions->[-1]{revision} < $minrev )
            {
                pop @$revisions;
            }
        }
        if ( defined ( $maxrev ) )
        {
            $log->debug("Removing revisions greater than $maxrev");
            while ( scalar(@$revisions) > 0 and $revisions->[0]{revision} > $maxrev )
            {
                shift @$revisions;
            }
        }

        next unless ( scalar(@$revisions) );

        print "M \n";
        print "M RCS file: $state->{CVSROOT}/$state->{module}/$filename,v\n";
        print "M Working file: $filename\n";
        print "M head: 1.$headmeta->{revision}\n";
        print "M branch:\n";
        print "M locks: strict\n";
        print "M access list:\n";
        print "M symbolic names:\n";
        print "M keyword substitution: kv\n";
        print "M total revisions: $totalrevisions;\tselected revisions: " . scalar(@$revisions) . "\n";
        print "M description:\n";

        foreach my $revision ( @$revisions )
        {
            print "M ----------------------------\n";
            print "M revision 1.$revision->{revision}\n";
            # reformat the date for log output
            $revision->{modified} = sprintf('%04d/%02d/%02d %s', $3, $DATE_LIST->{$2}, $1, $4 ) if ( $revision->{modified} =~ /(\d+)\s+(\w+)\s+(\d+)\s+(\S+)/ and defined($DATE_LIST->{$2}) );
            $revision->{author} =~ s/\s+.*//;
            $revision->{author} =~ s/^(.{8}).*/$1/;
            print "M date: $revision->{modified};  author: $revision->{author};  state: " . ( $revision->{filehash} eq "deleted" ? "dead" : "Exp" ) . ";  lines: +2 -3\n";
            my $commitmessage = $updater->commitmessage($revision->{commithash});
            $commitmessage =~ s/^/M /mg;
            print $commitmessage . "\n";
        }
        print "M =============================================================================\n";
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
    my $tmpdir = tempdir ( DIR => $TEMP_DIR );
    my ( undef, $file_index ) = tempfile ( DIR => $TEMP_DIR, OPEN => 0 );
    $log->info("Temp checkoutdir creation successful, basing annotate session work on '$tmpdir', index file is '$file_index'");

    $ENV{GIT_DIR} = $state->{CVSROOT} . "/";
    $ENV{GIT_INDEX_FILE} = $file_index;

    chdir $tmpdir;

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
	system("git-read-tree", $lastseenin);
	unless ($? == 0)
	{
	    print "E error running git-read-tree $lastseenin $file_index $!\n";
	    return;
	}
	$log->info("Created index '$file_index' with commit $lastseenin - exit status $?");

        # do a checkout of the file
        system('git-checkout-index', '-f', '-u', $filename);
        unless ($? == 0) {
            print "E error running git-checkout-index -f -u $filename : $!\n";
            return;
        }

        $log->info("Annotate $filename");

        # Prepare a file with the commits from the linearized
        # history that annotate should know about. This prevents
        # git-jsannotate telling us about commits we are hiding
        # from the client.

        my $a_hints = "$tmpdir/.annotate_hints";
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

        my @cmd = (qw(git-annotate -l -S), $a_hints, $filename);
        if (!open(ANNOTATE, "-|", @cmd)) {
            print "E error invoking ". join(' ',@cmd) .": $!\n";
            return;
        }
        my $metadata = {};
        print "E Annotations for $filename\n";
        print "E ***************\n";
        while ( <ANNOTATE> )
        {
            if (m/^([a-zA-Z0-9]{40})\t\([^\)]*\)(.*)$/i)
            {
                my $commithash = $1;
                my $data = $2;
                unless ( defined ( $metadata->{$commithash} ) )
                {
                    $metadata->{$commithash} = $updater->getmeta($filename, $commithash);
                    $metadata->{$commithash}{author} =~ s/\s+.*//;
                    $metadata->{$commithash}{author} =~ s/^(.{8}).*/$1/;
                    $metadata->{$commithash}{modified} = sprintf("%02d-%s-%02d", $1, $2, $3) if ( $metadata->{$commithash}{modified} =~ /^(\d+)\s(\w+)\s\d\d(\d\d)/ );
                }
                printf("M 1.%-5d      (%-8s %10s): %s\n",
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
    chdir "/";

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
        $opt = { l => 0, R => 0, k => 1, D => 1, D => 1, r => 2 } if ( $type eq "diff" );
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

# This method uses $state->{directory} to populate $state->{args} with a list of filenames
sub argsfromdir
{
    my $updater = shift;

    $state->{args} = [] if ( scalar(@{$state->{args}}) == 1 and $state->{args}[0] eq "." );

    return if ( scalar ( @{$state->{args}} ) > 1 );

    my @gethead = @{$updater->gethead};

    # push added files
    foreach my $file (keys %{$state->{entries}}) {
	if ( exists $state->{entries}{$file}{revision} &&
		$state->{entries}{$file}{revision} == 0 )
	{
	    push @gethead, { name => $file, filehash => 'added' };
	}
    }

    if ( scalar(@{$state->{args}}) == 1 )
    {
        my $arg = $state->{args}[0];
        $arg .= $state->{prependdir} if ( defined ( $state->{prependdir} ) );

        $log->info("Only one arg specified, checking for directory expansion on '$arg'");

        foreach my $file ( @gethead )
        {
            next if ( $file->{filehash} eq "deleted" and not defined ( $state->{entries}{$file->{name}} ) );
            next unless ( $file->{name} =~ /^$arg\// or $file->{name} eq $arg  );
            push @{$state->{args}}, $file->{name};
        }

        shift @{$state->{args}} if ( scalar(@{$state->{args}}) > 1 );
    } else {
        $log->info("Only one arg specified, populating file list automatically");

        $state->{args} = [];

        foreach my $file ( @gethead )
        {
            next if ( $file->{filehash} eq "deleted" and not defined ( $state->{entries}{$file->{name}} ) );
            next unless ( $file->{name} =~ s/^$state->{prependdir}// );
            push @{$state->{args}}, $file->{name};
        }
    }
}

# This method cleans up the $state variable after a command that uses arguments has run
sub statecleanup
{
    $state->{files} = [];
    $state->{args} = [];
    $state->{arguments} = [];
    $state->{entries} = {};
}

sub revparse
{
    my $filename = shift;

    return undef unless ( defined ( $state->{entries}{$filename}{revision} ) );

    return $1 if ( $state->{entries}{$filename}{revision} =~ /^1\.(\d+)/ );
    return -$1 if ( $state->{entries}{$filename}{revision} =~ /^-1\.(\d+)/ );

    return undef;
}

# This method takes a file hash and does a CVS "file transfer" which transmits the
# size of the file, and then the file contents.
# If a second argument $targetfile is given, the file is instead written out to
# a file by the name of $targetfile
sub transmitfile
{
    my $filehash = shift;
    my $targetfile = shift;

    if ( defined ( $filehash ) and $filehash eq "deleted" )
    {
        $log->warn("filehash is 'deleted'");
        return;
    }

    die "Need filehash" unless ( defined ( $filehash ) and $filehash =~ /^[a-zA-Z0-9]{40}$/ );

    my $type = `git-cat-file -t $filehash`;
    chomp $type;

    die ( "Invalid type '$type' (expected 'blob')" ) unless ( defined ( $type ) and $type eq "blob" );

    my $size = `git-cat-file -s $filehash`;
    chomp $size;

    $log->debug("transmitfile($filehash) size=$size, type=$type");

    if ( open my $fh, '-|', "git-cat-file", "blob", $filehash )
    {
        if ( defined ( $targetfile ) )
        {
            open NEWFILE, ">", $targetfile or die("Couldn't open '$targetfile' for writing : $!");
            print NEWFILE $_ while ( <$fh> );
            close NEWFILE or die("Failed to write '$targetfile': $!");
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

sub filecleanup
{
    my $filename = shift;

    return undef unless(defined($filename));
    if ( $filename =~ /^\// )
    {
        print "E absolute filenames '$filename' not supported by server\n";
        return undef;
    }

    $filename =~ s/^\.\///g;
    $filename = $state->{prependdir} . $filename;
    return $filename;
}

# Given a path, this function returns a string containing the kopts
# that should go into that path's Entries line.  For example, a binary
# file should get -kb.
sub kopts_from_path
{
	my ($path) = @_;

	# Once it exists, the git attributes system should be used to look up
	# what attributes apply to this path.

	# Until then, take the setting from the config file
    unless ( defined ( $cfg->{gitcvs}{allbinary} ) and $cfg->{gitcvs}{allbinary} =~ /^\s*(1|true|yes)\s*$/i )
    {
		# Return "" to give no special treatment to any path
		return "";
    } else {
		# Alternatively, to have all files treated as if they are binary (which
		# is more like git itself), always return the "-kb" option
		return "-kb";
    }
}

package GITCVS::log;

####
#### Copyright The Open University UK - 2006.
####
#### Authors: Martyn Smith    <martyn@catalyst.net.nz>
####          Martin Langhoff <martin@catalyst.net.nz>
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
####          Martin Langhoff <martin@catalyst.net.nz>
####
####

use strict;
use warnings;
use DBI;

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

    $self->{module} = $module;
    $self->{git_path} = $config . "/";

    $self->{log} = $log;

    die "Git repo '$self->{git_path}' doesn't exist" unless ( -d $self->{git_path} );

    $self->{dbdriver} = $cfg->{gitcvs}{$state->{method}}{dbdriver} ||
        $cfg->{gitcvs}{dbdriver} || "SQLite";
    $self->{dbname} = $cfg->{gitcvs}{$state->{method}}{dbname} ||
        $cfg->{gitcvs}{dbname} || "%Ggitcvs.%m.sqlite";
    $self->{dbuser} = $cfg->{gitcvs}{$state->{method}}{dbuser} ||
        $cfg->{gitcvs}{dbuser} || "";
    $self->{dbpass} = $cfg->{gitcvs}{$state->{method}}{dbpass} ||
        $cfg->{gitcvs}{dbpass} || "";
    my %mapping = ( m => $module,
                    a => $state->{method},
                    u => getlogin || getpwuid($<) || $<,
                    G => $self->{git_path},
                    g => mangle_dirname($self->{git_path}),
                    );
    $self->{dbname} =~ s/%([mauGg])/$mapping{$1}/eg;
    $self->{dbuser} =~ s/%([mauGg])/$mapping{$1}/eg;

    die "Invalid char ':' in dbdriver" if $self->{dbdriver} =~ /:/;
    die "Invalid char ';' in dbname" if $self->{dbname} =~ /;/;
    $self->{dbh} = DBI->connect("dbi:$self->{dbdriver}:dbname=$self->{dbname}",
                                $self->{dbuser},
                                $self->{dbpass});
    die "Error connecting to database\n" unless defined $self->{dbh};

    $self->{tables} = {};
    foreach my $table ( keys %{$self->{dbh}->table_info(undef,undef,undef,'TABLE')->fetchall_hashref('TABLE_NAME')} )
    {
        $self->{tables}{$table} = 1;
    }

    # Construct the revision table if required
    unless ( $self->{tables}{revision} )
    {
        $self->{dbh}->do("
            CREATE TABLE revision (
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
            CREATE INDEX revision_ix1
            ON revision (name,revision)
        ");
        $self->{dbh}->do("
            CREATE INDEX revision_ix2
            ON revision (name,commithash)
        ");
    }

    # Construct the head table if required
    unless ( $self->{tables}{head} )
    {
        $self->{dbh}->do("
            CREATE TABLE head (
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
            CREATE INDEX head_ix1
            ON head (name)
        ");
    }

    # Construct the properties table if required
    unless ( $self->{tables}{properties} )
    {
        $self->{dbh}->do("
            CREATE TABLE properties (
                key        TEXT NOT NULL PRIMARY KEY,
                value      TEXT
            )
        ");
    }

    # Construct the commitmsgs table if required
    unless ( $self->{tables}{commitmsgs} )
    {
        $self->{dbh}->do("
            CREATE TABLE commitmsgs (
                key        TEXT NOT NULL PRIMARY KEY,
                value      TEXT
            )
        ");
    }

    return $self;
}

=head2 update

=cut
sub update
{
    my $self = shift;

    # first lets get the commit list
    $ENV{GIT_DIR} = $self->{git_path};

    my $commitsha1 = `git rev-parse $self->{module}`;
    chomp $commitsha1;

    my $commitinfo = `git cat-file commit $self->{module} 2>&1`;
    unless ( $commitinfo =~ /tree\s+[a-zA-Z0-9]{40}/ )
    {
        die("Invalid module '$self->{module}'");
    }


    my $git_log;
    my $lastcommit = $self->_get_prop("last_commit");

    if (defined $lastcommit && $lastcommit eq $commitsha1) { # up-to-date
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
    open(GITLOG, '-|', 'git-rev-list', @git_log_params) or die "Cannot call git-rev-list: $!";

    my @commits;

    my %commit = ();

    while ( <GITLOG> )
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
    close GITLOG;

    unshift @commits, \%commit if ( keys %commit );

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
    foreach my $file ( @{$self->gethead()} )
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
                # not $lastpicked, see if we can get a log
                # from the merge-base to that parent to put it
                # in the message as a merge summary.
                my @parents = @{$commit->{parents}};
                foreach my $parent (@parents) {
                    # git-merge-base can potentially (but rarely) throw
                    # several candidate merge bases. let's assume
                    # that the first one is the best one.
                    if ($parent eq $lastpicked) {
                        next;
                    }
                    my $base = safe_pipe_capture('git-merge-base',
						 $lastpicked, $parent);
                    chomp $base;
                    if ($base) {
                        my @merged;
                        # print "want to log between  $base $parent \n";
                        open(GITLOG, '-|', 'git-log', "$base..$parent")
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
        $commit->{date} = "$2 $1 $4 $3 $5" if ( $commit->{date} =~ /^\w+\s+(\w+)\s+(\d+)\s+(\d+:\d+:\d+)\s+(\d+)\s+([+-]\d+)$/ );

        if ( defined ( $lastpicked ) )
        {
            my $filepipe = open(FILELIST, '-|', 'git-diff-tree', '-z', '-r', $lastpicked, $commit->{hash}) or die("Cannot call git-diff-tree : $!");
	    local ($/) = "\0";
            while ( <FILELIST> )
            {
		chomp;
                unless ( /^:\d{6}\s+\d{3}(\d)\d{2}\s+[a-zA-Z0-9]{40}\s+([a-zA-Z0-9]{40})\s+(\w)$/o )
                {
                    die("Couldn't process git-diff-tree line : $_");
                }
		my ($mode, $hash, $change) = ($1, $2, $3);
		my $name = <FILELIST>;
		chomp($name);

                # $log->debug("File mode=$mode, hash=$hash, change=$change, name=$name");

                my $git_perms = "";
                $git_perms .= "r" if ( $mode & 4 );
                $git_perms .= "w" if ( $mode & 2 );
                $git_perms .= "x" if ( $mode & 1 );
                $git_perms = "rw" if ( $git_perms eq "" );

                if ( $change eq "D" )
                {
                    #$log->debug("DELETE   $name");
                    $head->{$name} = {
                        name => $name,
                        revision => $head->{$name}{revision} + 1,
                        filehash => "deleted",
                        commithash => $commit->{hash},
                        modified => $commit->{date},
                        author => $commit->{author},
                        mode => $git_perms,
                    };
                    $self->insert_rev($name, $head->{$name}{revision}, $hash, $commit->{hash}, $commit->{date}, $commit->{author}, $git_perms);
                }
                elsif ( $change eq "M" )
                {
                    #$log->debug("MODIFIED $name");
                    $head->{$name} = {
                        name => $name,
                        revision => $head->{$name}{revision} + 1,
                        filehash => $hash,
                        commithash => $commit->{hash},
                        modified => $commit->{date},
                        author => $commit->{author},
                        mode => $git_perms,
                    };
                    $self->insert_rev($name, $head->{$name}{revision}, $hash, $commit->{hash}, $commit->{date}, $commit->{author}, $git_perms);
                }
                elsif ( $change eq "A" )
                {
                    #$log->debug("ADDED    $name");
                    $head->{$name} = {
                        name => $name,
                        revision => $head->{$name}{revision} ? $head->{$name}{revision}+1 : 1,
                        filehash => $hash,
                        commithash => $commit->{hash},
                        modified => $commit->{date},
                        author => $commit->{author},
                        mode => $git_perms,
                    };
                    $self->insert_rev($name, $head->{$name}{revision}, $hash, $commit->{hash}, $commit->{date}, $commit->{author}, $git_perms);
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

            my $filepipe = open(FILELIST, '-|', 'git-ls-tree', '-z', '-r', $commit->{hash}) or die("Cannot call git-ls-tree : $!");
	    local $/ = "\0";
            while ( <FILELIST> )
            {
		chomp;
                unless ( /^(\d+)\s+(\w+)\s+([a-zA-Z0-9]+)\t(.*)$/o )
                {
                    die("Couldn't process git-ls-tree line : $_");
                }

                my ( $git_perms, $git_type, $git_hash, $git_filename ) = ( $1, $2, $3, $4 );

                $seen_files->{$git_filename} = 1;

                my ( $oldhash, $oldrevision, $oldmode ) = (
                    $head->{$git_filename}{filehash},
                    $head->{$git_filename}{revision},
                    $head->{$git_filename}{mode}
                );

                if ( $git_perms =~ /^\d\d\d(\d)\d\d/o )
                {
                    $git_perms = "";
                    $git_perms .= "r" if ( $1 & 4 );
                    $git_perms .= "w" if ( $1 & 2 );
                    $git_perms .= "x" if ( $1 & 1 );
                } else {
                    $git_perms = "rw";
                }

                # unless the file exists with the same hash, we need to update it ...
                unless ( defined($oldhash) and $oldhash eq $git_hash and defined($oldmode) and $oldmode eq $git_perms )
                {
                    my $newrevision = ( $oldrevision or 0 ) + 1;

                    $head->{$git_filename} = {
                        name => $git_filename,
                        revision => $newrevision,
                        filehash => $git_hash,
                        commithash => $commit->{hash},
                        modified => $commit->{date},
                        author => $commit->{author},
                        mode => $git_perms,
                    };


                    $self->insert_rev($git_filename, $newrevision, $git_hash, $commit->{hash}, $commit->{date}, $commit->{author}, $git_perms);
                }
            }
            close FILELIST;

            # Detect deleted files
            foreach my $file ( keys %$head )
            {
                unless ( exists $seen_files->{$file} or $head->{$file}{filehash} eq "deleted" )
                {
                    $head->{$file}{revision}++;
                    $head->{$file}{filehash} = "deleted";
                    $head->{$file}{commithash} = $commit->{hash};
                    $head->{$file}{modified} = $commit->{date};
                    $head->{$file}{author} = $commit->{author};

                    $self->insert_rev($file, $head->{$file}{revision}, $head->{$file}{filehash}, $commit->{hash}, $commit->{date}, $commit->{author}, $head->{$file}{mode});
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
    foreach my $file ( keys %$head )
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
    $self->{gethead_cache} = undef;


    # Ending exclusive lock here
    $self->{dbh}->commit() or die "Failed to commit changes to SQLite";
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

    my $insert_rev = $self->{dbh}->prepare_cached("INSERT INTO revision (name, revision, filehash, commithash, modified, author, mode) VALUES (?,?,?,?,?,?,?)",{},1);
    $insert_rev->execute($name, $revision, $filehash, $commithash, $modified, $author, $mode);
}

sub insert_mergelog
{
    my $self = shift;
    my $key = shift;
    my $value = shift;

    my $insert_mergelog = $self->{dbh}->prepare_cached("INSERT INTO commitmsgs (key, value) VALUES (?,?)",{},1);
    $insert_mergelog->execute($key, $value);
}

sub delete_head
{
    my $self = shift;

    my $delete_head = $self->{dbh}->prepare_cached("DELETE FROM head",{},1);
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

    my $insert_head = $self->{dbh}->prepare_cached("INSERT INTO head (name, revision, filehash, commithash, modified, author, mode) VALUES (?,?,?,?,?,?,?)",{},1);
    $insert_head->execute($name, $revision, $filehash, $commithash, $modified, $author, $mode);
}

sub _headrev
{
    my $self = shift;
    my $filename = shift;

    my $db_query = $self->{dbh}->prepare_cached("SELECT filehash, revision, mode FROM head WHERE name=?",{},1);
    $db_query->execute($filename);
    my ( $hash, $revision, $mode ) = $db_query->fetchrow_array;

    return ( $hash, $revision, $mode );
}

sub _get_prop
{
    my $self = shift;
    my $key = shift;

    my $db_query = $self->{dbh}->prepare_cached("SELECT value FROM properties WHERE key=?",{},1);
    $db_query->execute($key);
    my ( $value ) = $db_query->fetchrow_array;

    return $value;
}

sub _set_prop
{
    my $self = shift;
    my $key = shift;
    my $value = shift;

    my $db_query = $self->{dbh}->prepare_cached("UPDATE properties SET value=? WHERE key=?",{},1);
    $db_query->execute($value, $key);

    unless ( $db_query->rows )
    {
        $db_query = $self->{dbh}->prepare_cached("INSERT INTO properties (key, value) VALUES (?,?)",{},1);
        $db_query->execute($key, $value);
    }

    return $value;
}

=head2 gethead

=cut

sub gethead
{
    my $self = shift;

    return $self->{gethead_cache} if ( defined ( $self->{gethead_cache} ) );

    my $db_query = $self->{dbh}->prepare_cached("SELECT name, filehash, mode, revision, modified, commithash, author FROM head ORDER BY name ASC",{},1);
    $db_query->execute();

    my $tree = [];
    while ( my $file = $db_query->fetchrow_hashref )
    {
        push @$tree, $file;
    }

    $self->{gethead_cache} = $tree;

    return $tree;
}

=head2 getlog

=cut

sub getlog
{
    my $self = shift;
    my $filename = shift;

    my $db_query = $self->{dbh}->prepare_cached("SELECT name, filehash, author, mode, revision, modified, commithash FROM revision WHERE name=? ORDER BY revision DESC",{},1);
    $db_query->execute($filename);

    my $tree = [];
    while ( my $file = $db_query->fetchrow_hashref )
    {
        push @$tree, $file;
    }

    return $tree;
}

=head2 getmeta

This function takes a filename (with path) argument and returns a hashref of
metadata for that file.

=cut

sub getmeta
{
    my $self = shift;
    my $filename = shift;
    my $revision = shift;

    my $db_query;
    if ( defined($revision) and $revision =~ /^\d+$/ )
    {
        $db_query = $self->{dbh}->prepare_cached("SELECT * FROM revision WHERE name=? AND revision=?",{},1);
        $db_query->execute($filename, $revision);
    }
    elsif ( defined($revision) and $revision =~ /^[a-zA-Z0-9]{40}$/ )
    {
        $db_query = $self->{dbh}->prepare_cached("SELECT * FROM revision WHERE name=? AND commithash=?",{},1);
        $db_query->execute($filename, $revision);
    } else {
        $db_query = $self->{dbh}->prepare_cached("SELECT * FROM head WHERE name=?",{},1);
        $db_query->execute($filename);
    }

    return $db_query->fetchrow_hashref;
}

=head2 commitmessage

this function takes a commithash and returns the commit message for that commit

=cut
sub commitmessage
{
    my $self = shift;
    my $commithash = shift;

    die("Need commithash") unless ( defined($commithash) and $commithash =~ /^[a-zA-Z0-9]{40}$/ );

    my $db_query;
    $db_query = $self->{dbh}->prepare_cached("SELECT value FROM commitmsgs WHERE key=?",{},1);
    $db_query->execute($commithash);

    my ( $message ) = $db_query->fetchrow_array;

    if ( defined ( $message ) )
    {
        $message .= " " if ( $message =~ /\n$/ );
        return $message;
    }

    my @lines = safe_pipe_capture("git-cat-file", "commit", $commithash);
    shift @lines while ( $lines[0] =~ /\S/ );
    $message = join("",@lines);
    $message .= " " if ( $message =~ /\n$/ );
    return $message;
}

=head2 gethistory

This function takes a filename (with path) argument and returns an arrayofarrays
containing revision,filehash,commithash ordered by revision descending

=cut
sub gethistory
{
    my $self = shift;
    my $filename = shift;

    my $db_query;
    $db_query = $self->{dbh}->prepare_cached("SELECT revision, filehash, commithash FROM revision WHERE name=? ORDER BY revision DESC",{},1);
    $db_query->execute($filename);

    return $db_query->fetchall_arrayref;
}

=head2 gethistorydense

This function takes a filename (with path) argument and returns an arrayofarrays
containing revision,filehash,commithash ordered by revision descending.

This version of gethistory skips deleted entries -- so it is useful for annotate.
The 'dense' part is a reference to a '--dense' option available for git-rev-list
and other git tools that depend on it.

=cut
sub gethistorydense
{
    my $self = shift;
    my $filename = shift;

    my $db_query;
    $db_query = $self->{dbh}->prepare_cached("SELECT revision, filehash, commithash FROM revision WHERE name=? AND filehash!='deleted' ORDER BY revision DESC",{},1);
    $db_query->execute($filename);

    return $db_query->fetchall_arrayref;
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

=head2 safe_pipe_capture

an alternative to `command` that allows input to be passed as an array
to work around shell problems with weird characters in arguments

=cut
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

1;
