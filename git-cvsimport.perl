#!/usr/bin/perl -w

# This tool is copyright (c) 2005, Matthias Urlichs.
# It is released under the Gnu Public License, version 2.
#
# The basic idea is to aggregate CVS check-ins into related changes.
# Fortunately, "cvsps" does that for us; all we have to do is to parse
# its output.
#
# Checking out the files is done by a single long-running CVS connection
# / server process.
#
# The head revision is on branch "origin" by default.
# You can change that with the '-o' option.

use strict;
use warnings;
use Getopt::Std;
use File::Spec;
use File::Temp qw(tempfile tmpnam);
use File::Path qw(mkpath);
use File::Basename qw(basename dirname);
use Time::Local;
use IO::Socket;
use IO::Pipe;
use POSIX qw(strftime dup2 ENOENT);
use IPC::Open2;

$SIG{'PIPE'}="IGNORE";
$ENV{'TZ'}="UTC";

our ($opt_h,$opt_o,$opt_v,$opt_k,$opt_u,$opt_d,$opt_p,$opt_C,$opt_z,$opt_i,$opt_P, $opt_s,$opt_m,$opt_M,$opt_A,$opt_S,$opt_L, $opt_a);
my (%conv_author_name, %conv_author_email);

sub usage(;$) {
	my $msg = shift;
	print(STDERR "Error: $msg\n") if $msg;
	print STDERR <<END;
Usage: ${\basename $0}     # fetch/update GIT from CVS
       [-o branch-for-HEAD] [-h] [-v] [-d CVSROOT] [-A author-conv-file]
       [-p opts-for-cvsps] [-P file] [-C GIT_repository] [-z fuzz] [-i] [-k]
       [-u] [-s subst] [-a] [-m] [-M regex] [-S regex] [-L commitlimit]
       [CVS_module]
END
	exit(1);
}

sub read_author_info($) {
	my ($file) = @_;
	my $user;
	open my $f, '<', "$file" or die("Failed to open $file: $!\n");

	while (<$f>) {
		# Expected format is this:
		#   exon=Andreas Ericsson <ae@op5.se>
		if (m/^(\S+?)\s*=\s*(.+?)\s*<(.+)>\s*$/) {
			$user = $1;
			$conv_author_name{$user} = $2;
			$conv_author_email{$user} = $3;
		}
		# However, we also read from CVSROOT/users format
		# to ease migration.
		elsif (/^(\w+):(['"]?)(.+?)\2\s*$/) {
			my $mapped;
			($user, $mapped) = ($1, $3);
			if ($mapped =~ /^\s*(.*?)\s*<(.*)>\s*$/) {
				$conv_author_name{$user} = $1;
				$conv_author_email{$user} = $2;
			}
			elsif ($mapped =~ /^<?(.*)>?$/) {
				$conv_author_name{$user} = $user;
				$conv_author_email{$user} = $1;
			}
		}
		# NEEDSWORK: Maybe warn on unrecognized lines?
	}
	close ($f);
}

sub write_author_info($) {
	my ($file) = @_;
	open my $f, '>', $file or
	  die("Failed to open $file for writing: $!");

	foreach (keys %conv_author_name) {
		print $f "$_=$conv_author_name{$_} <$conv_author_email{$_}>\n";
	}
	close ($f);
}

# convert getopts specs for use by git-repo-config
sub read_repo_config {
    # Split the string between characters, unless there is a ':'
    # So "abc:de" becomes ["a", "b", "c:", "d", "e"]
	my @opts = split(/ *(?!:)/, shift);
	foreach my $o (@opts) {
		my $key = $o;
		$key =~ s/://g;
		my $arg = 'git-repo-config';
		$arg .= ' --bool' if ($o !~ /:$/);

        chomp(my $tmp = `$arg --get cvsimport.$key`);
		if ($tmp && !($arg =~ /--bool/ && $tmp eq 'false')) {
            no strict 'refs';
            my $opt_name = "opt_" . $key;
            if (!$$opt_name) {
                $$opt_name = $tmp;
            }
		}
	}
    if (@ARGV == 0) {
        chomp(my $module = `git-repo-config --get cvsimport.module`);
        push(@ARGV, $module);
    }
}

my $opts = "haivmkuo:d:p:C:z:s:M:P:A:S:L:";
read_repo_config($opts);
getopts($opts) or usage();
usage if $opt_h;

@ARGV <= 1 or usage("You can't specify more than one CVS module");

if ($opt_d) {
	$ENV{"CVSROOT"} = $opt_d;
} elsif (-f 'CVS/Root') {
	open my $f, '<', 'CVS/Root' or die 'Failed to open CVS/Root';
	$opt_d = <$f>;
	chomp $opt_d;
	close $f;
	$ENV{"CVSROOT"} = $opt_d;
} elsif ($ENV{"CVSROOT"}) {
	$opt_d = $ENV{"CVSROOT"};
} else {
	usage("CVSROOT needs to be set");
}
$opt_o ||= "origin";
$opt_s ||= "-";
$opt_a ||= 0;

my $git_tree = $opt_C;
$git_tree ||= ".";

my $cvs_tree;
if ($#ARGV == 0) {
	$cvs_tree = $ARGV[0];
} elsif (-f 'CVS/Repository') {
	open my $f, '<', 'CVS/Repository' or 
	    die 'Failed to open CVS/Repository';
	$cvs_tree = <$f>;
	chomp $cvs_tree;
	close $f;
} else {
	usage("CVS module has to be specified");
}

our @mergerx = ();
if ($opt_m) {
	@mergerx = ( qr/\W(?:from|of|merge|merging|merged) (\w+)/i );
}
if ($opt_M) {
	push (@mergerx, qr/$opt_M/);
}

# Remember UTC of our starting time
# we'll want to avoid importing commits
# that are too recent
our $starttime = time();

select(STDERR); $|=1; select(STDOUT);


package CVSconn;
# Basic CVS dialog.
# We're only interested in connecting and downloading, so ...

use File::Spec;
use File::Temp qw(tempfile);
use POSIX qw(strftime dup2);

sub new {
	my ($what,$repo,$subdir) = @_;
	$what=ref($what) if ref($what);

	my $self = {};
	$self->{'buffer'} = "";
	bless($self,$what);

	$repo =~ s#/+$##;
	$self->{'fullrep'} = $repo;
	$self->conn();

	$self->{'subdir'} = $subdir;
	$self->{'lines'} = undef;

	return $self;
}

sub conn {
	my $self = shift;
	my $repo = $self->{'fullrep'};
	if ($repo =~ s/^:pserver(?:([^:]*)):(?:(.*?)(?::(.*?))?@)?([^:\/]*)(?::(\d*))?//) {
		my ($param,$user,$pass,$serv,$port) = ($1,$2,$3,$4,$5);

		my ($proxyhost,$proxyport);
		if ($param && ($param =~ m/proxy=([^;]+)/)) {
			$proxyhost = $1;
			# Default proxyport, if not specified, is 8080.
			$proxyport = 8080;
			if ($ENV{"CVS_PROXY_PORT"}) {
				$proxyport = $ENV{"CVS_PROXY_PORT"};
			}
			if ($param =~ m/proxyport=([^;]+)/) {
				$proxyport = $1;
			}
		}

		$user="anonymous" unless defined $user;
		my $rr2 = "-";
		unless ($port) {
			$rr2 = ":pserver:$user\@$serv:$repo";
			$port=2401;
		}
		my $rr = ":pserver:$user\@$serv:$port$repo";

		unless ($pass) {
			open(H,$ENV{'HOME'}."/.cvspass") and do {
				# :pserver:cvs@mea.tmt.tele.fi:/cvsroot/zmailer Ah<Z
				while (<H>) {
					chomp;
					s/^\/\d+\s+//;
					my ($w,$p) = split(/\s/,$_,2);
					if ($w eq $rr or $w eq $rr2) {
						$pass = $p;
						last;
					}
				}
			};
		}
		$pass="A" unless $pass;

		my ($s, $rep);
		if ($proxyhost) {

			# Use a HTTP Proxy. Only works for HTTP proxies that
			# don't require user authentication
			#
			# See: http://www.ietf.org/rfc/rfc2817.txt

			$s = IO::Socket::INET->new(PeerHost => $proxyhost, PeerPort => $proxyport);
			die "Socket to $proxyhost: $!\n" unless defined $s;
			$s->write("CONNECT $serv:$port HTTP/1.1\r\nHost: $serv:$port\r\n\r\n")
	                        or die "Write to $proxyhost: $!\n";
	                $s->flush();

			$rep = <$s>;

			# The answer should look like 'HTTP/1.x 2yy ....'
			if (!($rep =~ m#^HTTP/1\.. 2[0-9][0-9]#)) {
				die "Proxy connect: $rep\n";
			}
			# Skip up to the empty line of the proxy server output
			# including the response headers.
			while ($rep = <$s>) {
				last if (!defined $rep ||
					 $rep eq "\n" ||
					 $rep eq "\r\n");
			}
		} else {
			$s = IO::Socket::INET->new(PeerHost => $serv, PeerPort => $port);
			die "Socket to $serv: $!\n" unless defined $s;
		}

		$s->write("BEGIN AUTH REQUEST\n$repo\n$user\n$pass\nEND AUTH REQUEST\n")
			or die "Write to $serv: $!\n";
		$s->flush();

		$rep = <$s>;

		if ($rep ne "I LOVE YOU\n") {
			$rep="<unknown>" unless $rep;
			die "AuthReply: $rep\n";
		}
		$self->{'socketo'} = $s;
		$self->{'socketi'} = $s;
	} else { # local or ext: Fork off our own cvs server.
		my $pr = IO::Pipe->new();
		my $pw = IO::Pipe->new();
		my $pid = fork();
		die "Fork: $!\n" unless defined $pid;
		my $cvs = 'cvs';
		$cvs = $ENV{CVS_SERVER} if exists $ENV{CVS_SERVER};
		my $rsh = 'rsh';
		$rsh = $ENV{CVS_RSH} if exists $ENV{CVS_RSH};

		my @cvs = ($cvs, 'server');
		my ($local, $user, $host);
		$local = $repo =~ s/:local://;
		if (!$local) {
		    $repo =~ s/:ext://;
		    $local = !($repo =~ s/^(?:([^\@:]+)\@)?([^:]+)://);
		    ($user, $host) = ($1, $2);
		}
		if (!$local) {
		    if ($user) {
			unshift @cvs, $rsh, '-l', $user, $host;
		    } else {
			unshift @cvs, $rsh, $host;
		    }
		}

		unless ($pid) {
			$pr->writer();
			$pw->reader();
			dup2($pw->fileno(),0);
			dup2($pr->fileno(),1);
			$pr->close();
			$pw->close();
			exec(@cvs);
		}
		$pw->writer();
		$pr->reader();
		$self->{'socketo'} = $pw;
		$self->{'socketi'} = $pr;
	}
	$self->{'socketo'}->write("Root $repo\n");

	# Trial and error says that this probably is the minimum set
	$self->{'socketo'}->write("Valid-responses ok error Valid-requests Mode M Mbinary E Checked-in Created Updated Merged Removed\n");

	$self->{'socketo'}->write("valid-requests\n");
	$self->{'socketo'}->flush();

	chomp(my $rep=$self->readline());
	if ($rep !~ s/^Valid-requests\s*//) {
		$rep="<unknown>" unless $rep;
		die "Expected Valid-requests from server, but got: $rep\n";
	}
	chomp(my $res=$self->readline());
	die "validReply: $res\n" if $res ne "ok";

	$self->{'socketo'}->write("UseUnchanged\n") if $rep =~ /\bUseUnchanged\b/;
	$self->{'repo'} = $repo;
}

sub readline {
	my ($self) = @_;
	return $self->{'socketi'}->getline();
}

sub _file {
	# Request a file with a given revision.
	# Trial and error says this is a good way to do it. :-/
	my ($self,$fn,$rev) = @_;
	$self->{'socketo'}->write("Argument -N\n") or return undef;
	$self->{'socketo'}->write("Argument -P\n") or return undef;
	# -kk: Linus' version doesn't use it - defaults to off
	if ($opt_k) {
	    $self->{'socketo'}->write("Argument -kk\n") or return undef;
	}
	$self->{'socketo'}->write("Argument -r\n") or return undef;
	$self->{'socketo'}->write("Argument $rev\n") or return undef;
	$self->{'socketo'}->write("Argument --\n") or return undef;
	$self->{'socketo'}->write("Argument $self->{'subdir'}/$fn\n") or return undef;
	$self->{'socketo'}->write("Directory .\n") or return undef;
	$self->{'socketo'}->write("$self->{'repo'}\n") or return undef;
	# $self->{'socketo'}->write("Sticky T1.0\n") or return undef;
	$self->{'socketo'}->write("co\n") or return undef;
	$self->{'socketo'}->flush() or return undef;
	$self->{'lines'} = 0;
	return 1;
}
sub _line {
	# Read a line from the server.
	# ... except that 'line' may be an entire file. ;-)
	my ($self, $fh) = @_;
	die "Not in lines" unless defined $self->{'lines'};

	my $line;
	my $res=0;
	while (defined($line = $self->readline())) {
		# M U gnupg-cvs-rep/AUTHORS
		# Updated gnupg-cvs-rep/
		# /daten/src/rsync/gnupg-cvs-rep/AUTHORS
		# /AUTHORS/1.1///T1.1
		# u=rw,g=rw,o=rw
		# 0
		# ok

		if ($line =~ s/^(?:Created|Updated) //) {
			$line = $self->readline(); # path
			$line = $self->readline(); # Entries line
			my $mode = $self->readline(); chomp $mode;
			$self->{'mode'} = $mode;
			defined (my $cnt = $self->readline())
				or die "EOF from server after 'Changed'\n";
			chomp $cnt;
			die "Duh: Filesize $cnt" if $cnt !~ /^\d+$/;
			$line="";
			$res = $self->_fetchfile($fh, $cnt);
		} elsif ($line =~ s/^ //) {
			print $fh $line;
			$res += length($line);
		} elsif ($line =~ /^M\b/) {
			# output, do nothing
		} elsif ($line =~ /^Mbinary\b/) {
			my $cnt;
			die "EOF from server after 'Mbinary'" unless defined ($cnt = $self->readline());
			chomp $cnt;
			die "Duh: Mbinary $cnt" if $cnt !~ /^\d+$/ or $cnt<1;
			$line="";
			$res += $self->_fetchfile($fh, $cnt);
		} else {
			chomp $line;
			if ($line eq "ok") {
				# print STDERR "S: ok (".length($res).")\n";
				return $res;
			} elsif ($line =~ s/^E //) {
				# print STDERR "S: $line\n";
			} elsif ($line =~ /^(Remove-entry|Removed) /i) {
				$line = $self->readline(); # filename
				$line = $self->readline(); # OK
				chomp $line;
				die "Unknown: $line" if $line ne "ok";
				return -1;
			} else {
				die "Unknown: $line\n";
			}
		}
	}
	return undef;
}
sub file {
	my ($self,$fn,$rev) = @_;
	my $res;

	my ($fh, $name) = tempfile('gitcvs.XXXXXX', 
		    DIR => File::Spec->tmpdir(), UNLINK => 1);

	$self->_file($fn,$rev) and $res = $self->_line($fh);

	if (!defined $res) {
	    print STDERR "Server has gone away while fetching $fn $rev, retrying...\n";
	    truncate $fh, 0;
	    $self->conn();
	    $self->_file($fn,$rev) or die "No file command send";
	    $res = $self->_line($fh);
	    die "Retry failed" unless defined $res;
	}
	close ($fh);

	return ($name, $res);
}
sub _fetchfile {
	my ($self, $fh, $cnt) = @_;
	my $res = 0;
	my $bufsize = 1024 * 1024;
	while ($cnt) {
	    if ($bufsize > $cnt) {
		$bufsize = $cnt;
	    }
	    my $buf;
	    my $num = $self->{'socketi'}->read($buf,$bufsize);
	    die "Server: Filesize $cnt: $num: $!\n" if not defined $num or $num<=0;
	    print $fh $buf;
	    $res += $num;
	    $cnt -= $num;
	}
	return $res;
}


package main;

my $cvs = CVSconn->new($opt_d, $cvs_tree);


sub pdate($) {
	my ($d) = @_;
	m#(\d{2,4})/(\d\d)/(\d\d)\s(\d\d):(\d\d)(?::(\d\d))?#
		or die "Unparseable date: $d\n";
	my $y=$1; $y-=1900 if $y>1900;
	return timegm($6||0,$5,$4,$3,$2-1,$y);
}

sub pmode($) {
	my ($mode) = @_;
	my $m = 0;
	my $mm = 0;
	my $um = 0;
	for my $x(split(//,$mode)) {
		if ($x eq ",") {
			$m |= $mm&$um;
			$mm = 0;
			$um = 0;
		} elsif ($x eq "u") { $um |= 0700;
		} elsif ($x eq "g") { $um |= 0070;
		} elsif ($x eq "o") { $um |= 0007;
		} elsif ($x eq "r") { $mm |= 0444;
		} elsif ($x eq "w") { $mm |= 0222;
		} elsif ($x eq "x") { $mm |= 0111;
		} elsif ($x eq "=") { # do nothing
		} else { die "Unknown mode: $mode\n";
		}
	}
	$m |= $mm&$um;
	return $m;
}

sub getwd() {
	my $pwd = `pwd`;
	chomp $pwd;
	return $pwd;
}

sub is_sha1 {
	my $s = shift;
	return $s =~ /^[a-f0-9]{40}$/;
}

sub get_headref ($$) {
    my $name    = shift;
    my $git_dir = shift; 
    
    my $f = "$git_dir/refs/heads/$name";
    if (open(my $fh, $f)) {
	    chomp(my $r = <$fh>);
	    is_sha1($r) or die "Cannot get head id for $name ($r): $!";
	    return $r;
    }
    die "unable to open $f: $!" unless $! == POSIX::ENOENT;
    return undef;
}

-d $git_tree
	or mkdir($git_tree,0777)
	or die "Could not create $git_tree: $!";
chdir($git_tree);

my $last_branch = "";
my $orig_branch = "";
my %branch_date;
my $tip_at_start = undef;

my $git_dir = $ENV{"GIT_DIR"} || ".git";
$git_dir = getwd()."/".$git_dir unless $git_dir =~ m#^/#;
$ENV{"GIT_DIR"} = $git_dir;
my $orig_git_index;
$orig_git_index = $ENV{GIT_INDEX_FILE} if exists $ENV{GIT_INDEX_FILE};

my %index; # holds filenames of one index per branch

unless (-d $git_dir) {
	system("git-init");
	die "Cannot init the GIT db at $git_tree: $?\n" if $?;
	system("git-read-tree");
	die "Cannot init an empty tree: $?\n" if $?;

	$last_branch = $opt_o;
	$orig_branch = "";
} else {
	open(F, "git-symbolic-ref HEAD |") or
		die "Cannot run git-symbolic-ref: $!\n";
	chomp ($last_branch = <F>);
	$last_branch = basename($last_branch);
	close(F);
	unless ($last_branch) {
		warn "Cannot read the last branch name: $! -- assuming 'master'\n";
		$last_branch = "master";
	}
	$orig_branch = $last_branch;
	$tip_at_start = `git-rev-parse --verify HEAD`;

	# Get the last import timestamps
	my $fmt = '($ref, $author) = (%(refname), %(author));';
	open(H, "git-for-each-ref --perl --format='$fmt' refs/heads |") or
		die "Cannot run git-for-each-ref: $!\n";
	while (defined(my $entry = <H>)) {
		my ($ref, $author);
		eval($entry) || die "cannot eval refs list: $@";
		my ($head) = ($ref =~ m|^refs/heads/(.*)|);
		$author =~ /^.*\s(\d+)\s[-+]\d{4}$/;
		$branch_date{$head} = $1;
	}
	close(H);
        if (!exists $branch_date{$opt_o}) {
		die "Branch '$opt_o' does not exist.\n".
		       "Either use the correct '-o branch' option,\n".
		       "or import to a new repository.\n";
        }
}

-d $git_dir
	or die "Could not create git subdir ($git_dir).\n";

# now we read (and possibly save) author-info as well
-f "$git_dir/cvs-authors" and
  read_author_info("$git_dir/cvs-authors");
if ($opt_A) {
	read_author_info($opt_A);
	write_author_info("$git_dir/cvs-authors");
}


#
# run cvsps into a file unless we are getting
# it passed as a file via $opt_P
#
my $cvspsfile;
unless ($opt_P) {
	print "Running cvsps...\n" if $opt_v;
	my $pid = open(CVSPS,"-|");
	my $cvspsfh;
	die "Cannot fork: $!\n" unless defined $pid;
	unless ($pid) {
		my @opt;
		@opt = split(/,/,$opt_p) if defined $opt_p;
		unshift @opt, '-z', $opt_z if defined $opt_z;
		unshift @opt, '-q'         unless defined $opt_v;
		unless (defined($opt_p) && $opt_p =~ m/--no-cvs-direct/) {
			push @opt, '--cvs-direct';
		}
		exec("cvsps","--norc",@opt,"-u","-A",'--root',$opt_d,$cvs_tree);
		die "Could not start cvsps: $!\n";
	}
	($cvspsfh, $cvspsfile) = tempfile('gitXXXXXX', SUFFIX => '.cvsps',
					  DIR => File::Spec->tmpdir());
	while (<CVSPS>) {
	    print $cvspsfh $_;
	}
	close CVSPS;
	close $cvspsfh;
} else {
	$cvspsfile = $opt_P;
}

open(CVS, "<$cvspsfile") or die $!;

## cvsps output:
#---------------------
#PatchSet 314
#Date: 1999/09/18 13:03:59
#Author: wkoch
#Branch: STABLE-BRANCH-1-0
#Ancestor branch: HEAD
#Tag: (none)
#Log:
#    See ChangeLog: Sat Sep 18 13:03:28 CEST 1999  Werner Koch
#Members:
#	README:1.57->1.57.2.1
#	VERSION:1.96->1.96.2.1
#
#---------------------

my $state = 0;

sub update_index (\@\@) {
	my $old = shift;
	my $new = shift;
	open(my $fh, '|-', qw(git-update-index -z --index-info))
		or die "unable to open git-update-index: $!";
	print $fh
		(map { "0 0000000000000000000000000000000000000000\t$_\0" }
			@$old),
		(map { '100' . sprintf('%o', $_->[0]) . " $_->[1]\t$_->[2]\0" }
			@$new)
		or die "unable to write to git-update-index: $!";
	close $fh
		or die "unable to write to git-update-index: $!";
	$? and die "git-update-index reported error: $?";
}

sub write_tree () {
	open(my $fh, '-|', qw(git-write-tree))
		or die "unable to open git-write-tree: $!";
	chomp(my $tree = <$fh>);
	is_sha1($tree)
		or die "Cannot get tree id ($tree): $!";
	close($fh)
		or die "Error running git-write-tree: $?\n";
	print "Tree ID $tree\n" if $opt_v;
	return $tree;
}

my ($patchset,$date,$author_name,$author_email,$branch,$ancestor,$tag,$logmsg);
my (@old,@new,@skipped,%ignorebranch);

# commits that cvsps cannot place anywhere...
$ignorebranch{'#CVSPS_NO_BRANCH'} = 1;

sub commit {
	if ($branch eq $opt_o && !$index{branch} && !get_headref($branch, $git_dir)) {
	    # looks like an initial commit
	    # use the index primed by git-init
	    $ENV{GIT_INDEX_FILE} = '.git/index';
	    $index{$branch} = '.git/index';
	} else {
	    # use an index per branch to speed up
	    # imports of projects with many branches
	    unless ($index{$branch}) {
		$index{$branch} = tmpnam();
		$ENV{GIT_INDEX_FILE} = $index{$branch};
		if ($ancestor) {
		    system("git-read-tree", $ancestor);
		} else {
		    system("git-read-tree", $branch);
		}
		die "read-tree failed: $?\n" if $?;
	    }
	}
        $ENV{GIT_INDEX_FILE} = $index{$branch};

	update_index(@old, @new);
	@old = @new = ();
	my $tree = write_tree();
	my $parent = get_headref($last_branch, $git_dir);
	print "Parent ID " . ($parent ? $parent : "(empty)") . "\n" if $opt_v;

	my @commit_args;
	push @commit_args, ("-p", $parent) if $parent;

	# loose detection of merges
	# based on the commit msg
	foreach my $rx (@mergerx) {
		next unless $logmsg =~ $rx && $1;
		my $mparent = $1 eq 'HEAD' ? $opt_o : $1;
		if (my $sha1 = get_headref($mparent, $git_dir)) {
			push @commit_args, '-p', $mparent;
			print "Merge parent branch: $mparent\n" if $opt_v;
		}
	}

	my $commit_date = strftime("+0000 %Y-%m-%d %H:%M:%S",gmtime($date));
	$ENV{GIT_AUTHOR_NAME} = $author_name;
	$ENV{GIT_AUTHOR_EMAIL} = $author_email;
	$ENV{GIT_AUTHOR_DATE} = $commit_date;
	$ENV{GIT_COMMITTER_NAME} = $author_name;
	$ENV{GIT_COMMITTER_EMAIL} = $author_email;
	$ENV{GIT_COMMITTER_DATE} = $commit_date;
	my $pid = open2(my $commit_read, my $commit_write,
		'git-commit-tree', $tree, @commit_args);

	# compatibility with git2cvs
	substr($logmsg,32767) = "" if length($logmsg) > 32767;
	$logmsg =~ s/[\s\n]+\z//;

	if (@skipped) {
	    $logmsg .= "\n\n\nSKIPPED:\n\t";
	    $logmsg .= join("\n\t", @skipped) . "\n";
	    @skipped = ();
	}

	print($commit_write "$logmsg\n") && close($commit_write)
		or die "Error writing to git-commit-tree: $!\n";

	print "Committed patch $patchset ($branch $commit_date)\n" if $opt_v;
	chomp(my $cid = <$commit_read>);
	is_sha1($cid) or die "Cannot get commit id ($cid): $!\n";
	print "Commit ID $cid\n" if $opt_v;
	close($commit_read);

	waitpid($pid,0);
	die "Error running git-commit-tree: $?\n" if $?;

	system("git-update-ref refs/heads/$branch $cid") == 0
		or die "Cannot write branch $branch for update: $!\n";

	if ($tag) {
		my ($in, $out) = ('','');
	        my ($xtag) = $tag;
		$xtag =~ s/\s+\*\*.*$//; # Remove stuff like ** INVALID ** and ** FUNKY **
		$xtag =~ tr/_/\./ if ( $opt_u );
		$xtag =~ s/[\/]/$opt_s/g;
		
		my $pid = open2($in, $out, 'git-mktag');
		print $out "object $cid\n".
		    "type commit\n".
		    "tag $xtag\n".
		    "tagger $author_name <$author_email>\n"
		    or die "Cannot create tag object $xtag: $!\n";
		close($out)
		    or die "Cannot create tag object $xtag: $!\n";

		my $tagobj = <$in>;
		chomp $tagobj;

		if ( !close($in) or waitpid($pid, 0) != $pid or
		     $? != 0 or $tagobj !~ /^[0123456789abcdef]{40}$/ ) {
		    die "Cannot create tag object $xtag: $!\n";
	        }
		

		open(C,">$git_dir/refs/tags/$xtag")
			or die "Cannot create tag $xtag: $!\n";
		print C "$tagobj\n"
			or die "Cannot write tag $xtag: $!\n";
		close(C)
			or die "Cannot write tag $xtag: $!\n";

		print "Created tag '$xtag' on '$branch'\n" if $opt_v;
	}
};

my $commitcount = 1;
while (<CVS>) {
	chomp;
	if ($state == 0 and /^-+$/) {
		$state = 1;
	} elsif ($state == 0) {
		$state = 1;
		redo;
	} elsif (($state==0 or $state==1) and s/^PatchSet\s+//) {
		$patchset = 0+$_;
		$state=2;
	} elsif ($state == 2 and s/^Date:\s+//) {
		$date = pdate($_);
		unless ($date) {
			print STDERR "Could not parse date: $_\n";
			$state=0;
			next;
		}
		$state=3;
	} elsif ($state == 3 and s/^Author:\s+//) {
		s/\s+$//;
		if (/^(.*?)\s+<(.*)>/) {
		    ($author_name, $author_email) = ($1, $2);
		} elsif ($conv_author_name{$_}) {
			$author_name = $conv_author_name{$_};
			$author_email = $conv_author_email{$_};
		} else {
		    $author_name = $author_email = $_;
		}
		$state = 4;
	} elsif ($state == 4 and s/^Branch:\s+//) {
		s/\s+$//;
		s/[\/]/$opt_s/g;
		$branch = $_;
		$state = 5;
	} elsif ($state == 5 and s/^Ancestor branch:\s+//) {
		s/\s+$//;
		$ancestor = $_;
		$ancestor = $opt_o if $ancestor eq "HEAD";
		$state = 6;
	} elsif ($state == 5) {
		$ancestor = undef;
		$state = 6;
		redo;
	} elsif ($state == 6 and s/^Tag:\s+//) {
		s/\s+$//;
		if ($_ eq "(none)") {
			$tag = undef;
		} else {
			$tag = $_;
		}
		$state = 7;
	} elsif ($state == 7 and /^Log:/) {
		$logmsg = "";
		$state = 8;
	} elsif ($state == 8 and /^Members:/) {
		$branch = $opt_o if $branch eq "HEAD";
		if (defined $branch_date{$branch} and $branch_date{$branch} >= $date) {
			# skip
			print "skip patchset $patchset: $date before $branch_date{$branch}\n" if $opt_v;
			$state = 11;
			next;
		}
		if (!$opt_a && $starttime - 300 - (defined $opt_z ? $opt_z : 300) <= $date) {
			# skip if the commit is too recent
			# that the cvsps default fuzz is 300s, we give ourselves another
			# 300s just in case -- this also prevents skipping commits
			# due to server clock drift
			print "skip patchset $patchset: $date too recent\n" if $opt_v;
			$state = 11;
			next;
		}
		if (exists $ignorebranch{$branch}) {
			print STDERR "Skipping $branch\n";
			$state = 11;
			next;
		}
		if ($ancestor) {
			if ($ancestor eq $branch) {
				print STDERR "Branch $branch erroneously stems from itself -- changed ancestor to $opt_o\n";
				$ancestor = $opt_o;
			}
			if (-f "$git_dir/refs/heads/$branch") {
				print STDERR "Branch $branch already exists!\n";
				$state=11;
				next;
			}
			unless (open(H,"$git_dir/refs/heads/$ancestor")) {
				print STDERR "Branch $ancestor does not exist!\n";
				$ignorebranch{$branch} = 1;
				$state=11;
				next;
			}
			chomp(my $id = <H>);
			close(H);
			unless (open(H,"> $git_dir/refs/heads/$branch")) {
				print STDERR "Could not create branch $branch: $!\n";
				$ignorebranch{$branch} = 1;
				$state=11;
				next;
			}
			print H "$id\n"
				or die "Could not write branch $branch: $!";
			close(H)
				or die "Could not write branch $branch: $!";
		}
		$last_branch = $branch if $branch ne $last_branch;
		$state = 9;
	} elsif ($state == 8) {
		$logmsg .= "$_\n";
	} elsif ($state == 9 and /^\s+(.+?):(INITIAL|\d+(?:\.\d+)+)->(\d+(?:\.\d+)+)\s*$/) {
#	VERSION:1.96->1.96.2.1
		my $init = ($2 eq "INITIAL");
		my $fn = $1;
		my $rev = $3;
		$fn =~ s#^/+##;
		if ($opt_S && $fn =~ m/$opt_S/) {
		    print "SKIPPING $fn v $rev\n";
		    push(@skipped, $fn);
		    next;
		}
		print "Fetching $fn   v $rev\n" if $opt_v;
		my ($tmpname, $size) = $cvs->file($fn,$rev);
		if ($size == -1) {
			push(@old,$fn);
			print "Drop $fn\n" if $opt_v;
		} else {
			print "".($init ? "New" : "Update")." $fn: $size bytes\n" if $opt_v;
			my $pid = open(my $F, '-|');
			die $! unless defined $pid;
			if (!$pid) {
			    exec("git-hash-object", "-w", $tmpname)
				or die "Cannot create object: $!\n";
			}
			my $sha = <$F>;
			chomp $sha;
			close $F;
			my $mode = pmode($cvs->{'mode'});
			push(@new,[$mode, $sha, $fn]); # may be resurrected!
		}
		unlink($tmpname);
	} elsif ($state == 9 and /^\s+(.+?):\d+(?:\.\d+)+->(\d+(?:\.\d+)+)\(DEAD\)\s*$/) {
		my $fn = $1;
		$fn =~ s#^/+##;
		push(@old,$fn);
		print "Delete $fn\n" if $opt_v;
	} elsif ($state == 9 and /^\s*$/) {
		$state = 10;
	} elsif (($state == 9 or $state == 10) and /^-+$/) {
		$commitcount++;
		if ($opt_L && $commitcount > $opt_L) {
			last;
		}
		commit();
		if (($commitcount & 1023) == 0) {
			system("git repack -a -d");
		}
		$state = 1;
	} elsif ($state == 11 and /^-+$/) {
		$state = 1;
	} elsif (/^-+$/) { # end of unknown-line processing
		$state = 1;
	} elsif ($state != 11) { # ignore stuff when skipping
		print "* UNKNOWN LINE * $_\n";
	}
}
commit() if $branch and $state != 11;

unless ($opt_P) {
	unlink($cvspsfile);
}

# The heuristic of repacking every 1024 commits can leave a
# lot of unpacked data.  If there is more than 1MB worth of
# not-packed objects, repack once more.
my $line = `git-count-objects`;
if ($line =~ /^(\d+) objects, (\d+) kilobytes$/) {
  my ($n_objects, $kb) = ($1, $2);
  1024 < $kb
    and system("git repack -a -d");
}

foreach my $git_index (values %index) {
    if ($git_index ne '.git/index') {
	unlink($git_index);
    }
}

if (defined $orig_git_index) {
	$ENV{GIT_INDEX_FILE} = $orig_git_index;
} else {
	delete $ENV{GIT_INDEX_FILE};
}

# Now switch back to the branch we were in before all of this happened
if ($orig_branch) {
	print "DONE.\n" if $opt_v;
	if ($opt_i) {
		exit 0;
	}
	my $tip_at_end = `git-rev-parse --verify HEAD`;
	if ($tip_at_start ne $tip_at_end) {
		for ($tip_at_start, $tip_at_end) { chomp; }
		print "Fetched into the current branch.\n" if $opt_v;
		system(qw(git-read-tree -u -m),
		       $tip_at_start, $tip_at_end);
		die "Fast-forward update failed: $?\n" if $?;
	}
	else {
		system(qw(git-merge cvsimport HEAD), "refs/heads/$opt_o");
		die "Could not merge $opt_o into the current branch.\n" if $?;
	}
} else {
	$orig_branch = "master";
	print "DONE; creating $orig_branch branch\n" if $opt_v;
	system("git-update-ref", "refs/heads/master", "refs/heads/$opt_o")
		unless -f "$git_dir/refs/heads/master";
	system('git-update-ref', 'HEAD', "$orig_branch");
	unless ($opt_i) {
		system('git checkout');
		die "checkout failed: $?\n" if $?;
	}
}
