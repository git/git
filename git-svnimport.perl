#!/usr/bin/perl -w

# This tool is copyright (c) 2005, Matthias Urlichs.
# It is released under the Gnu Public License, version 2.
#
# The basic idea is to pull and analyze SVN changes.
#
# Checking out the files is done by a single long-running SVN connection.
#
# The head revision is on branch "origin" by default.
# You can change that with the '-o' option.

use strict;
use warnings;
use Getopt::Std;
use File::Copy;
use File::Spec;
use File::Temp qw(tempfile);
use File::Path qw(mkpath);
use File::Basename qw(basename dirname);
use Time::Local;
use IO::Pipe;
use POSIX qw(strftime dup2);
use IPC::Open2;
use SVN::Core;
use SVN::Ra;

die "Need SVN:Core 1.2.1 or better" if $SVN::Core::VERSION lt "1.2.1";

$SIG{'PIPE'}="IGNORE";
$ENV{'TZ'}="UTC";

our($opt_h,$opt_o,$opt_v,$opt_u,$opt_C,$opt_i,$opt_m,$opt_M,$opt_t,$opt_T,
    $opt_b,$opt_r,$opt_I,$opt_A,$opt_s,$opt_l,$opt_d,$opt_D,$opt_S,$opt_F,
    $opt_P,$opt_R);

sub usage() {
	print STDERR <<END;
Usage: ${\basename $0}     # fetch/update GIT from SVN
       [-o branch-for-HEAD] [-h] [-v] [-l max_rev] [-R repack_each_revs]
       [-C GIT_repository] [-t tagname] [-T trunkname] [-b branchname]
       [-d|-D] [-i] [-u] [-r] [-I ignorefilename] [-s start_chg]
       [-m] [-M regex] [-A author_file] [-S] [-F] [-P project_name] [SVN_URL]
END
	exit(1);
}

getopts("A:b:C:dDFhiI:l:mM:o:rs:t:T:SP:R:uv") or usage();
usage if $opt_h;

my $tag_name = $opt_t || "tags";
my $trunk_name = $opt_T || "trunk";
my $branch_name = $opt_b || "branches";
my $project_name = $opt_P || "";
$project_name = "/" . $project_name if ($project_name);
my $repack_after = $opt_R || 1000;

@ARGV == 1 or @ARGV == 2 or usage();

$opt_o ||= "origin";
$opt_s ||= 1;
my $git_tree = $opt_C;
$git_tree ||= ".";

my $svn_url = $ARGV[0];
my $svn_dir = $ARGV[1];

our @mergerx = ();
if ($opt_m) {
	my $branch_esc = quotemeta ($branch_name);
	my $trunk_esc  = quotemeta ($trunk_name);
	@mergerx =
	(
		qr!\b(?:merg(?:ed?|ing))\b.*?\b((?:(?<=$branch_esc/)[\w\.\-]+)|(?:$trunk_esc))\b!i,
		qr!\b(?:from|of)\W+((?:(?<=$branch_esc/)[\w\.\-]+)|(?:$trunk_esc))\b!i,
		qr!\b(?:from|of)\W+(?:the )?([\w\.\-]+)[-\s]branch\b!i
	);
}
if ($opt_M) {
	unshift (@mergerx, qr/$opt_M/);
}

# Absolutize filename now, since we will have chdir'ed by the time we
# get around to opening it.
$opt_A = File::Spec->rel2abs($opt_A) if $opt_A;

our %users = ();
our $users_file = undef;
sub read_users($) {
	$users_file = File::Spec->rel2abs(@_);
	die "Cannot open $users_file\n" unless -f $users_file;
	open(my $authors,$users_file);
	while(<$authors>) {
		chomp;
		next unless /^(\S+?)\s*=\s*(.+?)\s*<(.+)>\s*$/;
		(my $user,my $name,my $email) = ($1,$2,$3);
		$users{$user} = [$name,$email];
	}
	close($authors);
}

select(STDERR); $|=1; select(STDOUT);


package SVNconn;
# Basic SVN connection.
# We're only interested in connecting and downloading, so ...

use File::Spec;
use File::Temp qw(tempfile);
use POSIX qw(strftime dup2);
use Fcntl qw(SEEK_SET);

sub new {
	my($what,$repo) = @_;
	$what=ref($what) if ref($what);

	my $self = {};
	$self->{'buffer'} = "";
	bless($self,$what);

	$repo =~ s#/+$##;
	$self->{'fullrep'} = $repo;
	$self->conn();

	return $self;
}

sub conn {
	my $self = shift;
	my $repo = $self->{'fullrep'};
	my $auth = SVN::Core::auth_open ([SVN::Client::get_simple_provider,
			  SVN::Client::get_ssl_server_trust_file_provider,
			  SVN::Client::get_username_provider]);
	my $s = SVN::Ra->new(url => $repo, auth => $auth);
	die "SVN connection to $repo: $!\n" unless defined $s;
	$self->{'svn'} = $s;
	$self->{'repo'} = $repo;
	$self->{'maxrev'} = $s->get_latest_revnum();
}

sub file {
	my($self,$path,$rev) = @_;

	my ($fh, $name) = tempfile('gitsvn.XXXXXX',
		    DIR => File::Spec->tmpdir(), UNLINK => 1);

	print "... $rev $path ...\n" if $opt_v;
	my (undef, $properties);
	my $pool = SVN::Pool->new();
	$path =~ s#^/*##;
	eval { (undef, $properties)
		   = $self->{'svn'}->get_file($path,$rev,$fh,$pool); };
	$pool->clear;
	if($@) {
		return undef if $@ =~ /Attempted to get checksum/;
		die $@;
	}
	my $mode;
	if (exists $properties->{'svn:executable'}) {
		$mode = '100755';
	} elsif (exists $properties->{'svn:special'}) {
		my ($special_content, $filesize);
		$filesize = tell $fh;
		seek $fh, 0, SEEK_SET;
		read $fh, $special_content, $filesize;
		if ($special_content =~ s/^link //) {
			$mode = '120000';
			seek $fh, 0, SEEK_SET;
			truncate $fh, 0;
			print $fh $special_content;
		} else {
			die "unexpected svn:special file encountered";
		}
	} else {
		$mode = '100644';
	}
	close ($fh);

	return ($name, $mode);
}

sub ignore {
	my($self,$path,$rev) = @_;

	print "... $rev $path ...\n" if $opt_v;
	$path =~ s#^/*##;
	my (undef,undef,$properties)
	    = $self->{'svn'}->get_dir($path,$rev,undef);
	if (exists $properties->{'svn:ignore'}) {
		my ($fh, $name) = tempfile('gitsvn.XXXXXX',
					   DIR => File::Spec->tmpdir(),
					   UNLINK => 1);
		print $fh $properties->{'svn:ignore'};
		close($fh);
		return $name;
	} else {
		return undef;
	}
}

sub dir_list {
	my($self,$path,$rev) = @_;
	$path =~ s#^/*##;
	my ($dirents,undef,$properties)
	    = $self->{'svn'}->get_dir($path,$rev,undef);
	return $dirents;
}

package main;
use URI;

our $svn = $svn_url;
$svn .= "/$svn_dir" if defined $svn_dir;
my $svn2 = SVNconn->new($svn);
$svn = SVNconn->new($svn);

my $lwp_ua;
if($opt_d or $opt_D) {
	$svn_url = URI->new($svn_url)->canonical;
	if($opt_D) {
		$svn_dir =~ s#/*$#/#;
	} else {
		$svn_dir = "";
	}
	if ($svn_url->scheme eq "http") {
		use LWP::UserAgent;
		$lwp_ua = LWP::UserAgent->new(keep_alive => 1, requests_redirectable => []);
	} else {
		print STDERR "Warning: not HTTP; turning off direct file access\n";
		$opt_d=0;
	}
}

sub pdate($) {
	my($d) = @_;
	$d =~ m#(\d\d\d\d)-(\d\d)-(\d\d)T(\d\d):(\d\d):(\d\d)#
		or die "Unparseable date: $d\n";
	my $y=$1; $y-=1900 if $y>1900;
	return timegm($6||0,$5,$4,$3,$2-1,$y);
}

sub getwd() {
	my $pwd = `pwd`;
	chomp $pwd;
	return $pwd;
}


sub get_headref($$) {
    my $name    = shift;
    my $git_dir = shift;
    my $sha;

    if (open(C,"$git_dir/refs/heads/$name")) {
	chomp($sha = <C>);
	close(C);
	length($sha) == 40
	    or die "Cannot get head id for $name ($sha): $!\n";
    }
    return $sha;
}


-d $git_tree
	or mkdir($git_tree,0777)
	or die "Could not create $git_tree: $!";
chdir($git_tree);

my $orig_branch = "";
my $forward_master = 0;
my %branches;

my $git_dir = $ENV{"GIT_DIR"} || ".git";
$git_dir = getwd()."/".$git_dir unless $git_dir =~ m#^/#;
$ENV{"GIT_DIR"} = $git_dir;
my $orig_git_index;
$orig_git_index = $ENV{GIT_INDEX_FILE} if exists $ENV{GIT_INDEX_FILE};
my ($git_ih, $git_index) = tempfile('gitXXXXXX', SUFFIX => '.idx',
				    DIR => File::Spec->tmpdir());
close ($git_ih);
$ENV{GIT_INDEX_FILE} = $git_index;
my $maxnum = 0;
my $last_rev = "";
my $last_branch;
my $current_rev = $opt_s || 1;
unless(-d $git_dir) {
	system("git-init");
	die "Cannot init the GIT db at $git_tree: $?\n" if $?;
	system("git-read-tree");
	die "Cannot init an empty tree: $?\n" if $?;

	$last_branch = $opt_o;
	$orig_branch = "";
} else {
	-f "$git_dir/refs/heads/$opt_o"
		or die "Branch '$opt_o' does not exist.\n".
		       "Either use the correct '-o branch' option,\n".
		       "or import to a new repository.\n";

	-f "$git_dir/svn2git"
		or die "'$git_dir/svn2git' does not exist.\n".
		       "You need that file for incremental imports.\n";
	open(F, "git-symbolic-ref HEAD |") or
		die "Cannot run git-symbolic-ref: $!\n";
	chomp ($last_branch = <F>);
	$last_branch = basename($last_branch);
	close(F);
	unless($last_branch) {
		warn "Cannot read the last branch name: $! -- assuming 'master'\n";
		$last_branch = "master";
	}
	$orig_branch = $last_branch;
	$last_rev = get_headref($orig_branch, $git_dir);
	if (-f "$git_dir/SVN2GIT_HEAD") {
		die <<EOM;
SVN2GIT_HEAD exists.
Make sure your working directory corresponds to HEAD and remove SVN2GIT_HEAD.
You may need to run

    git-read-tree -m -u SVN2GIT_HEAD HEAD
EOM
	}
	system('cp', "$git_dir/HEAD", "$git_dir/SVN2GIT_HEAD");

	$forward_master =
	    $opt_o ne 'master' && -f "$git_dir/refs/heads/master" &&
	    system('cmp', '-s', "$git_dir/refs/heads/master",
				"$git_dir/refs/heads/$opt_o") == 0;

	# populate index
	system('git-read-tree', $last_rev);
	die "read-tree failed: $?\n" if $?;

	# Get the last import timestamps
	open my $B,"<", "$git_dir/svn2git";
	while(<$B>) {
		chomp;
		my($num,$branch,$ref) = split;
		$branches{$branch}{$num} = $ref;
		$branches{$branch}{"LAST"} = $ref;
		$current_rev = $num+1 if $current_rev <= $num;
	}
	close($B);
}
-d $git_dir
	or die "Could not create git subdir ($git_dir).\n";

my $default_authors = "$git_dir/svn-authors";
if ($opt_A) {
	read_users($opt_A);
	copy($opt_A,$default_authors) or die "Copy failed: $!";
} else {
	read_users($default_authors) if -f $default_authors;
}

open BRANCHES,">>", "$git_dir/svn2git";

sub node_kind($$) {
	my ($svnpath, $revision) = @_;
	my $pool=SVN::Pool->new;
	$svnpath =~ s#^/*##;
	my $kind = $svn->{'svn'}->check_path($svnpath,$revision,$pool);
	$pool->clear;
	return $kind;
}

sub get_file($$$) {
	my($svnpath,$rev,$path) = @_;

	# now get it
	my ($name,$mode);
	if($opt_d) {
		my($req,$res);

		# /svn/!svn/bc/2/django/trunk/django-docs/build.py
		my $url=$svn_url->clone();
		$url->path($url->path."/!svn/bc/$rev/$svn_dir$svnpath");
		print "... $path...\n" if $opt_v;
		$req = HTTP::Request->new(GET => $url);
		$res = $lwp_ua->request($req);
		if ($res->is_success) {
			my $fh;
			($fh, $name) = tempfile('gitsvn.XXXXXX',
			DIR => File::Spec->tmpdir(), UNLINK => 1);
			print $fh $res->content;
			close($fh) or die "Could not write $name: $!\n";
		} else {
			return undef if $res->code == 301; # directory?
			die $res->status_line." at $url\n";
		}
		$mode = '0644'; # can't obtain mode via direct http request?
	} else {
		($name,$mode) = $svn->file("$svnpath",$rev);
		return undef unless defined $name;
	}

	my $pid = open(my $F, '-|');
	die $! unless defined $pid;
	if (!$pid) {
	    exec("git-hash-object", "-w", $name)
		or die "Cannot create object: $!\n";
	}
	my $sha = <$F>;
	chomp $sha;
	close $F;
	unlink $name;
	return [$mode, $sha, $path];
}

sub get_ignore($$$$$) {
	my($new,$old,$rev,$path,$svnpath) = @_;

	return unless $opt_I;
	my $name = $svn->ignore("$svnpath",$rev);
	if ($path eq '/') {
		$path = $opt_I;
	} else {
		$path = File::Spec->catfile($path,$opt_I);
	}
	if (defined $name) {
		my $pid = open(my $F, '-|');
		die $! unless defined $pid;
		if (!$pid) {
			exec("git-hash-object", "-w", $name)
			    or die "Cannot create object: $!\n";
		}
		my $sha = <$F>;
		chomp $sha;
		close $F;
		unlink $name;
		push(@$new,['0644',$sha,$path]);
	} elsif (defined $old) {
		push(@$old,$path);
	}
}

sub project_path($$)
{
	my ($path, $project) = @_;

	$path = "/".$path unless ($path =~ m#^\/#) ;
	return $1 if ($path =~ m#^$project\/(.*)$#);

	$path =~ s#\.#\\\.#g;
	$path =~ s#\+#\\\+#g;
	return "/" if ($project =~ m#^$path.*$#);

	return undef;
}

sub split_path($$) {
	my($rev,$path) = @_;
	my $branch;

	if($path =~ s#^/\Q$tag_name\E/([^/]+)/?##) {
		$branch = "/$1";
	} elsif($path =~ s#^/\Q$trunk_name\E/?##) {
		$branch = "/";
	} elsif($path =~ s#^/\Q$branch_name\E/([^/]+)/?##) {
		$branch = $1;
	} else {
		my %no_error = (
			"/" => 1,
			"/$tag_name" => 1,
			"/$branch_name" => 1
		);
		print STDERR "$rev: Unrecognized path: $path\n" unless (defined $no_error{$path});
		return ()
	}
	if ($path eq "") {
		$path = "/";
	} elsif ($project_name) {
		$path = project_path($path, $project_name);
	}
	return ($branch,$path);
}

sub branch_rev($$) {

	my ($srcbranch,$uptorev) = @_;

	my $bbranches = $branches{$srcbranch};
	my @revs = reverse sort { ($a eq 'LAST' ? 0 : $a) <=> ($b eq 'LAST' ? 0 : $b) } keys %$bbranches;
	my $therev;
	foreach my $arev(@revs) {
		next if  ($arev eq 'LAST');
		if ($arev <= $uptorev) {
			$therev = $arev;
			last;
		}
	}
	return $therev;
}

sub expand_svndir($$$);

sub expand_svndir($$$)
{
	my ($svnpath, $rev, $path) = @_;
	my @list;
	get_ignore(\@list, undef, $rev, $path, $svnpath);
	my $dirents = $svn->dir_list($svnpath, $rev);
	foreach my $p(keys %$dirents) {
		my $kind = node_kind($svnpath.'/'.$p, $rev);
		if ($kind eq $SVN::Node::file) {
			my $f = get_file($svnpath.'/'.$p, $rev, $path.'/'.$p);
			push(@list, $f) if $f;
		} elsif ($kind eq $SVN::Node::dir) {
			push(@list,
			     expand_svndir($svnpath.'/'.$p, $rev, $path.'/'.$p));
		}
	}
	return @list;
}

sub copy_path($$$$$$$$) {
	# Somebody copied a whole subdirectory.
	# We need to find the index entries from the old version which the
	# SVN log entry points to, and add them to the new place.

	my($newrev,$newbranch,$path,$oldpath,$rev,$node_kind,$new,$parents) = @_;

	my($srcbranch,$srcpath) = split_path($rev,$oldpath);
	unless(defined $srcbranch && defined $srcpath) {
		print "Path not found when copying from $oldpath @ $rev.\n".
			"Will try to copy from original SVN location...\n"
			if $opt_v;
		push (@$new, expand_svndir($oldpath, $rev, $path));
		return;
	}
	my $therev = branch_rev($srcbranch, $rev);
	my $gitrev = $branches{$srcbranch}{$therev};
	unless($gitrev) {
		print STDERR "$newrev:$newbranch: could not find $oldpath \@ $rev\n";
		return;
	}
	if ($srcbranch ne $newbranch) {
		push(@$parents, $branches{$srcbranch}{'LAST'});
	}
	print "$newrev:$newbranch:$path: copying from $srcbranch:$srcpath @ $rev\n" if $opt_v;
	if ($node_kind eq $SVN::Node::dir) {
		$srcpath =~ s#/*$#/#;
	}

	my $pid = open my $f,'-|';
	die $! unless defined $pid;
	if (!$pid) {
		exec("git-ls-tree","-r","-z",$gitrev,$srcpath)
			or die $!;
	}
	local $/ = "\0";
	while(<$f>) {
		chomp;
		my($m,$p) = split(/\t/,$_,2);
		my($mode,$type,$sha1) = split(/ /,$m);
		next if $type ne "blob";
		if ($node_kind eq $SVN::Node::dir) {
			$p = $path . substr($p,length($srcpath)-1);
		} else {
			$p = $path;
		}
		push(@$new,[$mode,$sha1,$p]);
	}
	close($f) or
		print STDERR "$newrev:$newbranch: could not list files in $oldpath \@ $rev\n";
}

sub commit {
	my($branch, $changed_paths, $revision, $author, $date, $message) = @_;
	my($committer_name,$committer_email,$dest);
	my($author_name,$author_email);
	my(@old,@new,@parents);

	if (not defined $author or $author eq "") {
		$committer_name = $committer_email = "unknown";
	} elsif (defined $users_file) {
		die "User $author is not listed in $users_file\n"
		    unless exists $users{$author};
		($committer_name,$committer_email) = @{$users{$author}};
	} elsif ($author =~ /^(.*?)\s+<(.*)>$/) {
		($committer_name, $committer_email) = ($1, $2);
	} else {
		$author =~ s/^<(.*)>$/$1/;
		$committer_name = $committer_email = $author;
	}

	if ($opt_F && $message =~ /From:\s+(.*?)\s+<(.*)>\s*\n/) {
		($author_name, $author_email) = ($1, $2);
		print "Author from From: $1 <$2>\n" if ($opt_v);;
	} elsif ($opt_S && $message =~ /Signed-off-by:\s+(.*?)\s+<(.*)>\s*\n/) {
		($author_name, $author_email) = ($1, $2);
		print "Author from Signed-off-by: $1 <$2>\n" if ($opt_v);;
	} else {
		$author_name = $committer_name;
		$author_email = $committer_email;
	}

	$date = pdate($date);

	my $tag;
	my $parent;
	if($branch eq "/") { # trunk
		$parent = $opt_o;
	} elsif($branch =~ m#^/(.+)#) { # tag
		$tag = 1;
		$parent = $1;
	} else { # "normal" branch
		# nothing to do
		$parent = $branch;
	}
	$dest = $parent;

	my $prev = $changed_paths->{"/"};
	if($prev and $prev->[0] eq "A") {
		delete $changed_paths->{"/"};
		my $oldpath = $prev->[1];
		my $rev;
		if(defined $oldpath) {
			my $p;
			($parent,$p) = split_path($revision,$oldpath);
			if(defined $parent) {
				if($parent eq "/") {
					$parent = $opt_o;
				} else {
					$parent =~ s#^/##; # if it's a tag
				}
			}
		} else {
			$parent = undef;
		}
	}

	my $rev;
	if($revision > $opt_s and defined $parent) {
		open(H,"git-rev-parse --verify $parent |");
		$rev = <H>;
		close(H) or do {
			print STDERR "$revision: cannot find commit '$parent'!\n";
			return;
		};
		chop $rev;
		if(length($rev) != 40) {
			print STDERR "$revision: cannot find commit '$parent'!\n";
			return;
		}
		$rev = $branches{($parent eq $opt_o) ? "/" : $parent}{"LAST"};
		if($revision != $opt_s and not $rev) {
			print STDERR "$revision: do not know ancestor for '$parent'!\n";
			return;
		}
	} else {
		$rev = undef;
	}

#	if($prev and $prev->[0] eq "A") {
#		if(not $tag) {
#			unless(open(H,"> $git_dir/refs/heads/$branch")) {
#				print STDERR "$revision: Could not create branch $branch: $!\n";
#				$state=11;
#				next;
#			}
#			print H "$rev\n"
#				or die "Could not write branch $branch: $!";
#			close(H)
#				or die "Could not write branch $branch: $!";
#		}
#	}
	if(not defined $rev) {
		unlink($git_index);
	} elsif ($rev ne $last_rev) {
		print "Switching from $last_rev to $rev ($branch)\n" if $opt_v;
		system("git-read-tree", $rev);
		die "read-tree failed for $rev: $?\n" if $?;
		$last_rev = $rev;
	}

	push (@parents, $rev) if defined $rev;

	my $cid;
	if($tag and not %$changed_paths) {
		$cid = $rev;
	} else {
		my @paths = sort keys %$changed_paths;
		foreach my $path(@paths) {
			my $action = $changed_paths->{$path};

			if ($action->[0] eq "R") {
				# refer to a file/tree in an earlier commit
				push(@old,$path); # remove any old stuff
			}
			if(($action->[0] eq "A") || ($action->[0] eq "R")) {
				my $node_kind = node_kind($action->[3], $revision);
				if ($node_kind eq $SVN::Node::file) {
					my $f = get_file($action->[3],
							 $revision, $path);
					if ($f) {
						push(@new,$f) if $f;
					} else {
						my $opath = $action->[3];
						print STDERR "$revision: $branch: could not fetch '$opath'\n";
					}
				} elsif ($node_kind eq $SVN::Node::dir) {
					if($action->[1]) {
						copy_path($revision, $branch,
							  $path, $action->[1],
							  $action->[2], $node_kind,
							  \@new, \@parents);
					} else {
						get_ignore(\@new, \@old, $revision,
							   $path, $action->[3]);
					}
				}
			} elsif ($action->[0] eq "D") {
				push(@old,$path);
			} elsif ($action->[0] eq "M") {
				my $node_kind = node_kind($action->[3], $revision);
				if ($node_kind eq $SVN::Node::file) {
					my $f = get_file($action->[3],
							 $revision, $path);
					push(@new,$f) if $f;
				} elsif ($node_kind eq $SVN::Node::dir) {
					get_ignore(\@new, \@old, $revision,
						   $path, $action->[3]);
				}
			} else {
				die "$revision: unknown action '".$action->[0]."' for $path\n";
			}
		}

		while(@old) {
			my @o1;
			if(@old > 55) {
				@o1 = splice(@old,0,50);
			} else {
				@o1 = @old;
				@old = ();
			}
			my $pid = open my $F, "-|";
			die "$!" unless defined $pid;
			if (!$pid) {
				exec("git-ls-files", "-z", @o1) or die $!;
			}
			@o1 = ();
			local $/ = "\0";
			while(<$F>) {
				chomp;
				push(@o1,$_);
			}
			close($F);

			while(@o1) {
				my @o2;
				if(@o1 > 55) {
					@o2 = splice(@o1,0,50);
				} else {
					@o2 = @o1;
					@o1 = ();
				}
				system("git-update-index","--force-remove","--",@o2);
				die "Cannot remove files: $?\n" if $?;
			}
		}
		while(@new) {
			my @n2;
			if(@new > 12) {
				@n2 = splice(@new,0,10);
			} else {
				@n2 = @new;
				@new = ();
			}
			system("git-update-index","--add",
				(map { ('--cacheinfo', @$_) } @n2));
			die "Cannot add files: $?\n" if $?;
		}

		my $pid = open(C,"-|");
		die "Cannot fork: $!" unless defined $pid;
		unless($pid) {
			exec("git-write-tree");
			die "Cannot exec git-write-tree: $!\n";
		}
		chomp(my $tree = <C>);
		length($tree) == 40
			or die "Cannot get tree id ($tree): $!\n";
		close(C)
			or die "Error running git-write-tree: $?\n";
		print "Tree ID $tree\n" if $opt_v;

		my $pr = IO::Pipe->new() or die "Cannot open pipe: $!\n";
		my $pw = IO::Pipe->new() or die "Cannot open pipe: $!\n";
		$pid = fork();
		die "Fork: $!\n" unless defined $pid;
		unless($pid) {
			$pr->writer();
			$pw->reader();
			open(OUT,">&STDOUT");
			dup2($pw->fileno(),0);
			dup2($pr->fileno(),1);
			$pr->close();
			$pw->close();

			my @par = ();

			# loose detection of merges
			# based on the commit msg
			foreach my $rx (@mergerx) {
				if ($message =~ $rx) {
					my $mparent = $1;
					if ($mparent eq 'HEAD') { $mparent = $opt_o };
					if ( -e "$git_dir/refs/heads/$mparent") {
						$mparent = get_headref($mparent, $git_dir);
						push (@parents, $mparent);
						print OUT "Merge parent branch: $mparent\n" if $opt_v;
					}
				}
			}
			my %seen_parents = ();
			my @unique_parents = grep { ! $seen_parents{$_} ++ } @parents;
			foreach my $bparent (@unique_parents) {
				push @par, '-p', $bparent;
				print OUT "Merge parent branch: $bparent\n" if $opt_v;
			}

			exec("env",
				"GIT_AUTHOR_NAME=$author_name",
				"GIT_AUTHOR_EMAIL=$author_email",
				"GIT_AUTHOR_DATE=".strftime("+0000 %Y-%m-%d %H:%M:%S",gmtime($date)),
				"GIT_COMMITTER_NAME=$committer_name",
				"GIT_COMMITTER_EMAIL=$committer_email",
				"GIT_COMMITTER_DATE=".strftime("+0000 %Y-%m-%d %H:%M:%S",gmtime($date)),
				"git-commit-tree", $tree,@par);
			die "Cannot exec git-commit-tree: $!\n";
		}
		$pw->writer();
		$pr->reader();

		$message =~ s/[\s\n]+\z//;
		$message = "r$revision: $message" if $opt_r;

		print $pw "$message\n"
			or die "Error writing to git-commit-tree: $!\n";
		$pw->close();

		print "Committed change $revision:$branch ".strftime("%Y-%m-%d %H:%M:%S",gmtime($date)).")\n" if $opt_v;
		chomp($cid = <$pr>);
		length($cid) == 40
			or die "Cannot get commit id ($cid): $!\n";
		print "Commit ID $cid\n" if $opt_v;
		$pr->close();

		waitpid($pid,0);
		die "Error running git-commit-tree: $?\n" if $?;
	}

	if (not defined $cid) {
		$cid = $branches{"/"}{"LAST"};
	}

	if(not defined $dest) {
		print "... no known parent\n" if $opt_v;
	} elsif(not $tag) {
		print "Writing to refs/heads/$dest\n" if $opt_v;
		open(C,">$git_dir/refs/heads/$dest") and
		print C ("$cid\n") and
		close(C)
			or die "Cannot write branch $dest for update: $!\n";
	}

	if($tag) {
		my($in, $out) = ('','');
		$last_rev = "-" if %$changed_paths;
		# the tag was 'complex', i.e. did not refer to a "real" revision

		$dest =~ tr/_/\./ if $opt_u;
		$branch = $dest;

		my $pid = open2($in, $out, 'git-mktag');
		print $out ("object $cid\n".
		    "type commit\n".
		    "tag $dest\n".
		    "tagger $committer_name <$committer_email> 0 +0000\n") and
		close($out)
		    or die "Cannot create tag object $dest: $!\n";

		my $tagobj = <$in>;
		chomp $tagobj;

		if ( !close($in) or waitpid($pid, 0) != $pid or
				$? != 0 or $tagobj !~ /^[0123456789abcdef]{40}$/ ) {
			die "Cannot create tag object $dest: $!\n";
		}

		open(C,">$git_dir/refs/tags/$dest") and
		print C ("$tagobj\n") and
		close(C)
			or die "Cannot create tag $branch: $!\n";

		print "Created tag '$dest' on '$branch'\n" if $opt_v;
	}
	$branches{$branch}{"LAST"} = $cid;
	$branches{$branch}{$revision} = $cid;
	$last_rev = $cid;
	print BRANCHES "$revision $branch $cid\n";
	print "DONE: $revision $dest $cid\n" if $opt_v;
}

sub commit_all {
	# Recursive use of the SVN connection does not work
	local $svn = $svn2;

	my ($changed_paths, $revision, $author, $date, $message, $pool) = @_;
	my %p;
	while(my($path,$action) = each %$changed_paths) {
		$p{$path} = [ $action->action,$action->copyfrom_path, $action->copyfrom_rev, $path ];
	}
	$changed_paths = \%p;

	my %done;
	my @col;
	my $pref;
	my $branch;

	while(my($path,$action) = each %$changed_paths) {
		($branch,$path) = split_path($revision,$path);
		next if not defined $branch;
		next if not defined $path;
		$done{$branch}{$path} = $action;
	}
	while(($branch,$changed_paths) = each %done) {
		commit($branch, $changed_paths, $revision, $author, $date, $message);
	}
}

$opt_l = $svn->{'maxrev'} if not defined $opt_l or $opt_l > $svn->{'maxrev'};

if ($opt_l < $current_rev) {
    print "Up to date: no new revisions to fetch!\n" if $opt_v;
    unlink("$git_dir/SVN2GIT_HEAD");
    exit;
}

print "Processing from $current_rev to $opt_l ...\n" if $opt_v;

my $from_rev;
my $to_rev = $current_rev - 1;

while ($to_rev < $opt_l) {
	$from_rev = $to_rev + 1;
	$to_rev = $from_rev + $repack_after;
	$to_rev = $opt_l if $opt_l < $to_rev;
	print "Fetching from $from_rev to $to_rev ...\n" if $opt_v;
	my $pool=SVN::Pool->new;
	$svn->{'svn'}->get_log("/",$from_rev,$to_rev,0,1,1,\&commit_all,$pool);
	$pool->clear;
	my $pid = fork();
	die "Fork: $!\n" unless defined $pid;
	unless($pid) {
		exec("git-repack", "-d")
			or die "Cannot repack: $!\n";
	}
	waitpid($pid, 0);
}


unlink($git_index);

if (defined $orig_git_index) {
	$ENV{GIT_INDEX_FILE} = $orig_git_index;
} else {
	delete $ENV{GIT_INDEX_FILE};
}

# Now switch back to the branch we were in before all of this happened
if($orig_branch) {
	print "DONE\n" if $opt_v and (not defined $opt_l or $opt_l > 0);
	system("cp","$git_dir/refs/heads/$opt_o","$git_dir/refs/heads/master")
		if $forward_master;
	unless ($opt_i) {
		system('git-read-tree', '-m', '-u', 'SVN2GIT_HEAD', 'HEAD');
		die "read-tree failed: $?\n" if $?;
	}
} else {
	$orig_branch = "master";
	print "DONE; creating $orig_branch branch\n" if $opt_v and (not defined $opt_l or $opt_l > 0);
	system("cp","$git_dir/refs/heads/$opt_o","$git_dir/refs/heads/master")
		unless -f "$git_dir/refs/heads/master";
	system('git-update-ref', 'HEAD', "$orig_branch");
	unless ($opt_i) {
		system('git checkout');
		die "checkout failed: $?\n" if $?;
	}
}
unlink("$git_dir/SVN2GIT_HEAD");
close(BRANCHES);
