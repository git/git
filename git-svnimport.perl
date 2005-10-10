#!/usr/bin/perl -w

# This tool is copyright (c) 2005, Matthias Urlichs.
# It is released under the Gnu Public License, version 2.
#
# The basic idea is to pull and analyze SVN changes.
#
# Checking out the files is done by a single long-running CVS connection
# / server process.
#
# The head revision is on branch "origin" by default.
# You can change that with the '-o' option.

require v5.8.0; # for shell-safe open("-|",LIST)
use strict;
use warnings;
use Getopt::Std;
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

$SIG{'PIPE'}="IGNORE";
$ENV{'TZ'}="UTC";

our($opt_h,$opt_o,$opt_v,$opt_u,$opt_C,$opt_i,$opt_m,$opt_M,$opt_t,$opt_T,$opt_b);

sub usage() {
	print STDERR <<END;
Usage: ${\basename $0}     # fetch/update GIT from CVS
       [-o branch-for-HEAD] [-h] [-v]
       [-C GIT_repository] [-t tagname] [-T trunkname] [-b branchname]
       [-i] [-u] [-s subst] [-m] [-M regex] [SVN_URL]
END
	exit(1);
}

getopts("b:C:hivmM:o:t:T:u") or usage();
usage if $opt_h;

my $tag_name = $opt_t || "tags";
my $trunk_name = $opt_T || "trunk";
my $branch_name = $opt_b || "branches";

@ARGV <= 1 or usage();

$opt_o ||= "origin";
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
	usage();
}

our @mergerx = ();
if ($opt_m) {
	@mergerx = ( qr/\W(?:from|of|merge|merging|merged) (\w+)/i );
}
if ($opt_M) {
	push (@mergerx, qr/$opt_M/);
}

select(STDERR); $|=1; select(STDOUT);


package SVNconn;
# Basic SVN connection.
# We're only interested in connecting and downloading, so ...

use File::Spec;
use File::Temp qw(tempfile);
use POSIX qw(strftime dup2);

sub new {
	my($what,$repo) = @_;
	$what=ref($what) if ref($what);

	my $self = {};
	$self->{'buffer'} = "";
	bless($self,$what);

	$repo =~ s#/+$##;
	$self->{'fullrep'} = $repo;
	$self->conn();

	$self->{'lines'} = undef;

	return $self;
}

sub conn {
	my $self = shift;
	my $repo = $self->{'fullrep'};
	my $s = SVN::Ra->new($repo);

	die "SVN connection to $repo: $!\n" unless defined $s;
	$self->{'svn'} = $s;
	$self->{'repo'} = $repo;
	$self->{'maxrev'} = $s->get_latest_revnum();
}

sub file {
	my($self,$path,$rev) = @_;
	my $res;

	my ($fh, $name) = tempfile('gitsvn.XXXXXX', 
		    DIR => File::Spec->tmpdir(), UNLINK => 1);

	print "... $rev $path ...\n" if $opt_v;
	eval { $self->{'svn'}->get_file($path,$rev,$fh); };
	if (defined $@ and $@ !~ /Attempted to get checksum/) {
	    # retry
	    $self->conn();
		eval { $self->{'svn'}->get_file($path,$rev,$fh); };
	};
	return () if defined $@ and $@ !~ /Attempted to get checksum/;
	die $@ if $@;
	close ($fh);

	return ($name, $res);
}


package main;

my $svn = SVNconn->new($cvs_tree);


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
my $current_rev = 0;
unless(-d $git_dir) {
	system("git-init-db");
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
	$last_branch = basename(readlink("$git_dir/HEAD"));
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
		$current_rev = $num+1 if $current_rev < $num+1;
	}
	close($B);
}
-d $git_dir
	or die "Could not create git subdir ($git_dir).\n";

open BRANCHES,">>", "$git_dir/svn2git";


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

sub get_file($$$) {
	my($rev,$branch,$path) = @_;

	# revert split_path(), below
	my $svnpath;
	$path = "" if $path eq "/"; # this should not happen, but ...
	if($branch eq "/") {
		$svnpath = "/$trunk_name/$path";
	} elsif($branch =~ m#^/#) {
		$svnpath = "/$tag_name$branch/$path";
	} else {
		$svnpath = "/$branch_name/$branch/$path";
	}

	# now get it
	my ($name, $res) = eval { $svn->file($svnpath,$rev); };
	return () unless defined $name;

	open my $F, '-|', "git-hash-object", "-w", $name
		or die "Cannot create object: $!\n";
	my $sha = <$F>;
	chomp $sha;
	close $F;
	my $mode = "0644"; # SV does not seem to store any file modes
	return [$mode, $sha, $path];
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
		print STDERR "$rev: Unrecognized path: $path\n";
		return ()
	}
	$path = "/" if $path eq "";
	return ($branch,$path);
}

sub commit {
	my($branch, $changed_paths, $revision, $author, $date, $message) = @_;
	my($author_name,$author_email,$dest);
	my(@old,@new);

	if (not defined $author) {
		$author_name = $author_email = "unknown";
	} elsif ($author =~ /^(.*?)\s+<(.*)>$/) {
		($author_name, $author_email) = ($1, $2);
	} else {
		$author =~ s/^<(.*)>$/$1/;
		$author_name = $author_email = $author;
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
			if($parent eq "/") {
				$parent = $opt_o;
			} else {
				$parent =~ s#^/##; # if it's a tag
			}
		} else {
			$parent = undef;
		}
	}

	my $rev;
	if(defined $parent) {
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
		if($revision != 1 and not $rev) {
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

	while(my($path,$action) = each %$changed_paths) {
		if ($action->[0] eq "A") {
			my $f = get_file($revision,$branch,$path);
			push(@new,$f) if $f;
		} elsif ($action->[0] eq "D") {
			push(@old,$path);
		} elsif ($action->[0] eq "M") {
			my $f = get_file($revision,$branch,$path);
			push(@new,$f) if $f;
		} elsif ($action->[0] eq "R") {
			# refer to a file/tree in an earlier commit
			push(@old,$path); # remove any old stuff

			# ... and add any new stuff
			my($b,$p) = split_path($revision,$action->[1]);
			open my $F,"-|","git-ls-tree","-r","-z", $branches{$b}{$action->[2]}, $p;
			local $/ = '\0';
			while(<$F>) {
				chomp;
				my($m,$p) = split(/\t/,$_,2);
				my($mode,$type,$sha1) = split(/ /,$m);
				next if $type ne "blob";
				push(@new,[$mode,$sha1,$p]);
			}
		} else {
			die "$revision: unknown action '".$action->[0]."' for $path\n";
		}
	}

	if(@old) {
		open my $F, "-│", "git-ls-files", "-z", @old or die $!;
		@old = ();
		local $/ = '\0';
		while(<$F>) {
			chomp;
			push(@old,$_);
		}
		close($F);

		while(@old) {
			my @o2;
			if(@old > 55) {
				@o2 = splice(@old,0,50);
			} else {
				@o2 = @old;
				@old = ();
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
		@par = ("-p",$rev) if defined $rev;

		# loose detection of merges
		# based on the commit msg
		foreach my $rx (@mergerx) {
			if ($message =~ $rx) {
				my $mparent = $1;
				if ($mparent eq 'HEAD') { $mparent = $opt_o };
				if ( -e "$git_dir/refs/heads/$mparent") {
					$mparent = get_headref($mparent, $git_dir);
					push @par, '-p', $mparent;
					print OUT "Merge parent branch: $mparent\n" if $opt_v;
				}
			} 
		}

		exec("env",
			"GIT_AUTHOR_NAME=$author_name",
			"GIT_AUTHOR_EMAIL=$author_email",
			"GIT_AUTHOR_DATE=".strftime("+0000 %Y-%m-%d %H:%M:%S",gmtime($date)),
			"GIT_COMMITTER_NAME=$author_name",
			"GIT_COMMITTER_EMAIL=$author_email",
			"GIT_COMMITTER_DATE=".strftime("+0000 %Y-%m-%d %H:%M:%S",gmtime($date)),
			"git-commit-tree", $tree,@par);
		die "Cannot exec git-commit-tree: $!\n";
	}
	$pw->writer();
	$pr->reader();

	$message =~ s/[\s\n]+\z//;

	print $pw "$message\n"
		or die "Error writing to git-commit-tree: $!\n";
	$pw->close();

	print "Committed change $revision:$branch ".strftime("%Y-%m-%d %H:%M:%S",gmtime($date)).")\n" if $opt_v;
	chomp(my $cid = <$pr>);
	length($cid) == 40
		or die "Cannot get commit id ($cid): $!\n";
	print "Commit ID $cid\n" if $opt_v;
	$pr->close();

	waitpid($pid,0);
	die "Error running git-commit-tree: $?\n" if $?;

	if(defined $dest) {
		print "Writing to refs/heads/$dest\n" if $opt_v;
		open(C,">$git_dir/refs/heads/$dest") and 
		print C ("$cid\n") and
		close(C)
			or die "Cannot write branch $dest for update: $!\n";
	} else {
		print "... no known parent\n" if $opt_v;
	}
	$branches{$branch}{"LAST"} = $cid;
	$branches{$branch}{$revision} = $cid;
	$last_rev = $cid;
	print BRANCHES "$revision $branch $cid\n";
	print "DONE: $revision $dest $cid\n" if $opt_v;

	if($tag) {
		my($in, $out) = ('','');
		$last_rev = "-" if %$changed_paths;
		# the tag was 'complex', i.e. did not refer to a "real" revision
		
		$tag =~ tr/_/\./ if $opt_u;

		my $pid = open2($in, $out, 'git-mktag');
		print $out ("object $cid\n".
		    "type commit\n".
		    "tag $tag\n".
		    "tagger $author_name <$author_email>\n") and
		close($out)
		    or die "Cannot create tag object $tag: $!\n";

		my $tagobj = <$in>;
		chomp $tagobj;

		if ( !close($in) or waitpid($pid, 0) != $pid or
				$? != 0 or $tagobj !~ /^[0123456789abcdef]{40}$/ ) {
			die "Cannot create tag object $tag: $!\n";
		}
		

		open(C,">$git_dir/refs/tags/$tag")
			or die "Cannot create tag $tag: $!\n";
		print C "$tagobj\n"
			or die "Cannot write tag $tag: $!\n";
		close(C)
			or die "Cannot write tag $tag: $!\n";

		print "Created tag '$tag' on '$branch'\n" if $opt_v;
	}
}

my ($changed_paths, $revision, $author, $date, $message, $pool) = @_;
sub _commit_all {
	($changed_paths, $revision, $author, $date, $message, $pool) = @_;
	my %p;
	while(my($path,$action) = each %$changed_paths) {
		$p{$path} = [ $action->action,$action->copyfrom_path, $action->copyfrom_rev ];
	}
	$changed_paths = \%p;
}

sub commit_all {
	my %done;
	my @col;
	my $pref;
	my $branch;

	while(my($path,$action) = each %$changed_paths) {
		($branch,$path) = split_path($revision,$path);
		next if not defined $branch;
		$done{$branch}{$path} = $action;
	}
	while(($branch,$changed_paths) = each %done) {
		commit($branch, $changed_paths, $revision, $author, $date, $message);
	}
}

while(++$current_rev < $svn->{'maxrev'}) {
	$svn->{'svn'}->get_log("/",$current_rev,$current_rev,$current_rev,1,1,\&_commit_all,"");
	commit_all();
}


unlink($git_index);

if (defined $orig_git_index) {
	$ENV{GIT_INDEX_FILE} = $orig_git_index;
} else {
	delete $ENV{GIT_INDEX_FILE};
}

# Now switch back to the branch we were in before all of this happened
if($orig_branch) {
	print "DONE\n" if $opt_v;
	system("cp","$git_dir/refs/heads/$opt_o","$git_dir/refs/heads/master")
		if $forward_master;
	unless ($opt_i) {
		system('git-read-tree', '-m', '-u', 'SVN2GIT_HEAD', 'HEAD');
		die "read-tree failed: $?\n" if $?;
	}
} else {
	$orig_branch = "master";
	print "DONE; creating $orig_branch branch\n" if $opt_v;
	system("cp","$git_dir/refs/heads/$opt_o","$git_dir/refs/heads/master")
		unless -f "$git_dir/refs/heads/master";
	unlink("$git_dir/HEAD");
	symlink("refs/heads/$orig_branch","$git_dir/HEAD");
	unless ($opt_i) {
		system('git checkout');
		die "checkout failed: $?\n" if $?;
	}
}
unlink("$git_dir/SVN2GIT_HEAD");
close(BRANCHES);
