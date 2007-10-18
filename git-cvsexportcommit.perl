#!/usr/bin/perl -w

# Known limitations:
# - does not propagate permissions
# - error handling has not been extensively tested
#

use strict;
use Getopt::Std;
use File::Temp qw(tempdir);
use Data::Dumper;
use File::Basename qw(basename dirname);

unless ($ENV{GIT_DIR} && -r $ENV{GIT_DIR}){
    die "GIT_DIR is not defined or is unreadable";
}

our ($opt_h, $opt_P, $opt_p, $opt_v, $opt_c, $opt_f, $opt_a, $opt_m, $opt_d, $opt_u);

getopts('uhPpvcfam:d:');

$opt_h && usage();

die "Need at least one commit identifier!" unless @ARGV;

my @cvs;
if ($opt_d) {
	@cvs = ('cvs', '-d', $opt_d);
} else {
	@cvs = ('cvs');
}

# setup a tempdir
our ($tmpdir, $tmpdirname) = tempdir('git-cvsapplycommit-XXXXXX',
				     TMPDIR => 1,
				     CLEANUP => 1);

# resolve target commit
my $commit;
$commit = pop @ARGV;
$commit = safe_pipe_capture('git-rev-parse', '--verify', "$commit^0");
chomp $commit;
if ($?) {
    die "The commit reference $commit did not resolve!";
}

# resolve what parent we want
my $parent;
if (@ARGV) {
    $parent = pop @ARGV;
    $parent =  safe_pipe_capture('git-rev-parse', '--verify', "$parent^0");
    chomp $parent;
    if ($?) {
	die "The parent reference did not resolve!";
    }
}

# find parents from the commit itself
my @commit  = safe_pipe_capture('git-cat-file', 'commit', $commit);
my @parents;
my $committer;
my $author;
my $stage = 'headers'; # headers, msg
my $title;
my $msg = '';

foreach my $line (@commit) {
    chomp $line;
    if ($stage eq 'headers' && $line eq '') {
	$stage = 'msg';
	next;
    }

    if ($stage eq 'headers') {
	if ($line =~ m/^parent (\w{40})$/) { # found a parent
	    push @parents, $1;
	} elsif ($line =~ m/^author (.+) \d+ [-+]\d+$/) {
	    $author = $1;
	} elsif ($line =~ m/^committer (.+) \d+ [-+]\d+$/) {
	    $committer = $1;
	}
    } else {
	$msg .= $line . "\n";
	unless ($title) {
	    $title = $line;
	}
    }
}

if ($parent) {
    my $found;
    # double check that it's a valid parent
    foreach my $p (@parents) {
	if ($p eq $parent) {
	    $found = 1;
	    last;
	}; # found it
    }
    die "Did not find $parent in the parents for this commit!" if !$found and !$opt_P;
} else { # we don't have a parent from the cmdline...
    if (@parents == 1) { # it's safe to get it from the commit
	$parent = $parents[0];
    } else { # or perhaps not!
	die "This commit has more than one parent -- please name the parent you want to use explicitly";
    }
}

$opt_v && print "Applying to CVS commit $commit from parent $parent\n";

# grab the commit message
open(MSG, ">.msg") or die "Cannot open .msg for writing";
if ($opt_m) {
    print MSG $opt_m;
}
print MSG $msg;
if ($opt_a) {
    print MSG "\n\nAuthor: $author\n";
    if ($author ne $committer) {
	print MSG "Committer: $committer\n";
    }
}
close MSG;

`git-diff-tree --binary -p $parent $commit >.cvsexportcommit.diff`;# || die "Cannot diff";

## apply non-binary changes

# In pedantic mode require all lines of context to match.  In normal
# mode, be compatible with diff/patch: assume 3 lines of context and
# require at least one line match, i.e. ignore at most 2 lines of
# context, like diff/patch do by default.
my $context = $opt_p ? '' : '-C1';

print "Checking if patch will apply\n";

my @stat;
open APPLY, "GIT_DIR= git-apply $context --binary --summary --numstat<.cvsexportcommit.diff|" || die "cannot patch";
@stat=<APPLY>;
close APPLY || die "Cannot patch";
my (@bfiles,@files,@afiles,@dfiles);
chomp @stat;
foreach (@stat) {
	push (@bfiles,$1) if m/^-\t-\t(.*)$/;
	push (@files, $1) if m/^-\t-\t(.*)$/;
	push (@files, $1) if m/^\d+\t\d+\t(.*)$/;
	push (@afiles,$1) if m/^ create mode [0-7]+ (.*)$/;
	push (@dfiles,$1) if m/^ delete mode [0-7]+ (.*)$/;
}
map { s/^"(.*)"$/$1/g } @bfiles,@files;
map { s/\\([0-7]{3})/sprintf('%c',oct $1)/eg } @bfiles,@files;

# check that the files are clean and up to date according to cvs
my $dirty;
my @dirs;
foreach my $p (@afiles) {
    my $path = dirname $p;
    while (!-d $path and ! grep { $_ eq $path } @dirs) {
	unshift @dirs, $path;
	$path = dirname $path;
    }
}

# ... check dirs,
foreach my $d (@dirs) {
    if (-e $d) {
	$dirty = 1;
	warn "$d exists and is not a directory!\n";
    }
}

# ... query status of all files that we have a directory for and parse output of 'cvs status' to %cvsstat.
my @canstatusfiles;
foreach my $f (@files) {
    my $path = dirname $f;
    next if (grep { $_ eq $path } @dirs);
    push @canstatusfiles, $f;
}

my %cvsstat;
if (@canstatusfiles) {
    if ($opt_u) {
      my @updated = safe_pipe_capture(@cvs, 'update', @canstatusfiles);
      print @updated;
    }
    my @cvsoutput;
    @cvsoutput= safe_pipe_capture(@cvs, 'status', @canstatusfiles);
    my $matchcount = 0;
    foreach my $l (@cvsoutput) {
        chomp $l;
        if ( $l =~ /^File:/ and  $l =~ /Status: (.*)$/ ) {
            $cvsstat{$canstatusfiles[$matchcount]} = $1;
            $matchcount++;
        }
    }
}

# ... validate new files,
foreach my $f (@afiles) {
    if (defined ($cvsstat{$f}) and $cvsstat{$f} ne "Unknown") {
	$dirty = 1;
	warn "File $f is already known in your CVS checkout -- perhaps it has been added by another user. Or this may indicate that it exists on a different branch. If this is the case, use -f to force the merge.\n";
	warn "Status was: $cvsstat{$f}\n";
    }
}
# ... validate known files.
foreach my $f (@files) {
    next if grep { $_ eq $f } @afiles;
    # TODO:we need to handle removed in cvs
    unless (defined ($cvsstat{$f}) and $cvsstat{$f} eq "Up-to-date") {
	$dirty = 1;
	warn "File $f not up to date but has status '$cvsstat{$f}' in your CVS checkout!\n";
    }
}
if ($dirty) {
    if ($opt_f) {	warn "The tree is not clean -- forced merge\n";
	$dirty = 0;
    } else {
	die "Exiting: your CVS tree is not clean for this merge.";
    }
}

print "Applying\n";
`GIT_DIR= git-apply $context --binary --summary --numstat --apply <.cvsexportcommit.diff` || die "cannot patch";

print "Patch applied successfully. Adding new files and directories to CVS\n";
my $dirtypatch = 0;

#
# We have to add the directories in order otherwise we will have
# problems when we try and add the sub-directory of a directory we
# have not added yet.
#
# Luckily this is easy to deal with by sorting the directories and
# dealing with the shortest ones first.
#
@dirs = sort { length $a <=> length $b} @dirs;

foreach my $d (@dirs) {
    if (system(@cvs,'add',$d)) {
	$dirtypatch = 1;
	warn "Failed to cvs add directory $d -- you may need to do it manually";
    }
}

foreach my $f (@afiles) {
    if (grep { $_ eq $f } @bfiles) {
      system(@cvs, 'add','-kb',$f);
    } else {
      system(@cvs, 'add', $f);
    }
    if ($?) {
	$dirtypatch = 1;
	warn "Failed to cvs add $f -- you may need to do it manually";
    }
}

foreach my $f (@dfiles) {
    system(@cvs, 'rm', '-f', $f);
    if ($?) {
	$dirtypatch = 1;
	warn "Failed to cvs rm -f $f -- you may need to do it manually";
    }
}

print "Commit to CVS\n";
print "Patch title (first comment line): $title\n";
my @commitfiles = map { unless (m/\s/) { '\''.$_.'\''; } else { $_; }; } (@files);
my $cmd = join(' ', @cvs)." commit -F .msg @commitfiles";

if ($dirtypatch) {
    print "NOTE: One or more hunks failed to apply cleanly.\n";
    print "You'll need to apply the patch in .cvsexportcommit.diff manually\n";
    print "using a patch program. After applying the patch and resolving the\n";
    print "problems you may commit using:";
    print "\n    $cmd\n\n";
    exit(1);
}

if ($opt_c) {
    print "Autocommit\n  $cmd\n";
    print safe_pipe_capture(@cvs, 'commit', '-F', '.msg', @files);
    if ($?) {
	die "Exiting: The commit did not succeed";
    }
    print "Committed successfully to CVS\n";
    # clean up
    unlink(".msg");
} else {
    print "Ready for you to commit, just run:\n\n   $cmd\n";
}

# clean up
unlink(".cvsexportcommit.diff");

# CVS version 1.11.x and 1.12.x sleeps the wrong way to ensure the timestamp
# used by CVS and the one set by subsequence file modifications are different.
# If they are not different CVS will not detect changes.
sleep(1);

sub usage {
	print STDERR <<END;
Usage: GIT_DIR=/path/to/.git ${\basename $0} [-h] [-p] [-v] [-c] [-f] [-m msgprefix] [ parent ] commit
END
	exit(1);
}

# An alternative to `command` that allows input to be passed as an array
# to work around shell problems with weird characters in arguments
# if the exec returns non-zero we die
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

sub safe_pipe_capture_blob {
    my $output;
    if (my $pid = open my $child, '-|') {
        local $/;
	undef $/;
	$output = (<$child>);
	close $child or die join(' ',@_).": $! $?";
    } else {
	exec(@_) or die "$! $?"; # exec() can fail the executable can't be found
    }
    return $output;
}
