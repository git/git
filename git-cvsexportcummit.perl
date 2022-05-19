#!/usr/bin/perl

use 5.008;
use strict;
use warnings;
use Getopt::Std;
use File::Temp qw(tempdir);
use Data::Dumper;
use File::Basename qw(basename dirname);
use File::Spec;
use Git;

our ($opt_h, $opt_P, $opt_p, $opt_v, $opt_c, $opt_f, $opt_a, $opt_m, $opt_d, $opt_u, $opt_w, $opt_W, $opt_k);

getopts('uhPpvcfkam:d:w:W');

$opt_h && usage();

die "Need at least one cummit identifier!" unless @ARGV;

# Get but-config settings
my $repo = Git->repository();
$opt_w = $repo->config('cvsexportcummit.cvsdir') unless defined $opt_w;

my $tmpdir = File::Temp::tempdir(CLEANUP => 1);
my $hash_algo = $repo->config('extensions.objectformat') || 'sha1';
my $hexsz = $hash_algo eq 'sha256' ? 64 : 40;

if ($opt_w || $opt_W) {
	# Remember where BUT_DIR is before changing to CVS checkout
	unless ($ENV{BUT_DIR}) {
		# No BUT_DIR set. Figure it out for ourselves
		my $gd =`but rev-parse --but-dir`;
		chomp($gd);
		$ENV{BUT_DIR} = $gd;
	}

	# On MSYS, convert a Windows-style path to an MSYS-style path
	# so that rel2abs() below works correctly.
	if ($^O eq 'msys') {
		$ENV{BUT_DIR} =~ s#^([[:alpha:]]):/#/$1/#;
	}

	# Make sure BUT_DIR is absolute
	$ENV{BUT_DIR} = File::Spec->rel2abs($ENV{BUT_DIR});
}

if ($opt_w) {
	if (! -d $opt_w."/CVS" ) {
		die "$opt_w is not a CVS checkout";
	}
	chdir $opt_w or die "Cannot change to CVS checkout at $opt_w";
}
unless ($ENV{BUT_DIR} && -r $ENV{BUT_DIR}){
    die "BUT_DIR is not defined or is unreadable";
}


my @cvs;
if ($opt_d) {
	@cvs = ('cvs', '-d', $opt_d);
} else {
	@cvs = ('cvs');
}

# resolve target cummit
my $cummit;
$cummit = pop @ARGV;
$cummit = safe_pipe_capture('but', 'rev-parse', '--verify', "$cummit^0");
chomp $cummit;
if ($?) {
    die "The cummit reference $cummit did not resolve!";
}

# resolve what parent we want
my $parent;
if (@ARGV) {
    $parent = pop @ARGV;
    $parent =  safe_pipe_capture('but', 'rev-parse', '--verify', "$parent^0");
    chomp $parent;
    if ($?) {
	die "The parent reference did not resolve!";
    }
}

# find parents from the cummit itself
my @cummit  = safe_pipe_capture('but', 'cat-file', 'cummit', $cummit);
my @parents;
my $cummitter;
my $author;
my $stage = 'headers'; # headers, msg
my $title;
my $msg = '';

foreach my $line (@cummit) {
    chomp $line;
    if ($stage eq 'headers' && $line eq '') {
	$stage = 'msg';
	next;
    }

    if ($stage eq 'headers') {
	if ($line =~ m/^parent ([0-9a-f]{$hexsz})$/) { # found a parent
	    push @parents, $1;
	} elsif ($line =~ m/^author (.+) \d+ [-+]\d+$/) {
	    $author = $1;
	} elsif ($line =~ m/^cummitter (.+) \d+ [-+]\d+$/) {
	    $cummitter = $1;
	}
    } else {
	$msg .= $line . "\n";
	unless ($title) {
	    $title = $line;
	}
    }
}

my $noparent = "0" x $hexsz;
if ($parent) {
    my $found;
    # double check that it's a valid parent
    foreach my $p (@parents) {
	if ($p eq $parent) {
	    $found = 1;
	    last;
	}; # found it
    }
    die "Did not find $parent in the parents for this cummit!" if !$found and !$opt_P;
} else { # we don't have a parent from the cmdline...
    if (@parents == 1) { # it's safe to get it from the cummit
	$parent = $parents[0];
    } elsif (@parents == 0) { # there is no parent
        $parent = $noparent;
    } else { # cannot choose automatically from multiple parents
        die "This commit has more than one parent -- please name the parent you want to use explicitly";
    }
}

my $go_back_to = 0;

if ($opt_W) {
    $opt_v && print "Resetting to $parent\n";
    $go_back_to = `but symbolic-ref HEAD 2> /dev/null ||
	but rev-parse HEAD` || die "Could not determine current branch";
    system("but checkout -q $parent^0") && die "Could not check out $parent^0";
}

$opt_v && print "Applying to CVS cummit $cummit from parent $parent\n";

# grab the cummit message
open(MSG, ">.msg") or die "Cannot open .msg for writing";
if ($opt_m) {
    print MSG $opt_m;
}
print MSG $msg;
if ($opt_a) {
    print MSG "\n\nAuthor: $author\n";
    if ($author ne $cummitter) {
	print MSG "cummitter: $cummitter\n";
    }
}
close MSG;

if ($parent eq $noparent) {
    `but diff-tree --binary -p --root $cummit >.cvsexportcummit.diff`;# || die "Cannot diff";
} else {
    `but diff-tree --binary -p $parent $cummit >.cvsexportcummit.diff`;# || die "Cannot diff";
}

## apply non-binary changes

# In pedantic mode require all lines of context to match.  In normal
# mode, be compatible with diff/patch: assume 3 lines of context and
# require at least one line match, i.e. ignore at most 2 lines of
# context, like diff/patch do by default.
my $context = $opt_p ? '' : '-C1';

print "Checking if patch will apply\n";

my @stat;
open APPLY, "BUT_INDEX_FILE=$tmpdir/index but apply $context --summary --numstat<.cvsexportcummit.diff|" || die "cannot patch";
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
      my @updated = xargs_safe_pipe_capture([@cvs, 'update'], @canstatusfiles);
      print @updated;
    }
    # "cvs status" reorders the parameters, notably when there are multiple
    # arguments with the same basename.  So be precise here.

    my %added = map { $_ => 1 } @afiles;
    my %todo = map { $_ => 1 } @canstatusfiles;

    while (%todo) {
      my @canstatusfiles2 = ();
      my %fullname = ();
      foreach my $name (keys %todo) {
	my $basename = basename($name);

	# CVS reports files that don't exist in the current revision as
	# "no file $basename" in its "status" output, so we should
	# anticipate that.  Totally unknown files will have a status
	# "Unknown". However, if they exist in the Attic, their status
	# will be "Up-to-date" (this means they were added once but have
	# been removed).
	$basename = "no file $basename" if $added{$basename};

	$basename =~ s/^\s+//;
	$basename =~ s/\s+$//;

	if (!exists($fullname{$basename})) {
	  $fullname{$basename} = $name;
	  push (@canstatusfiles2, $name);
	  delete($todo{$name});
	}
      }
      my @cvsoutput;
      @cvsoutput = xargs_safe_pipe_capture([@cvs, 'status'], @canstatusfiles2);
      foreach my $l (@cvsoutput) {
	chomp $l;
	next unless
	    my ($file, $status) = $l =~ /^File:\s+(.*\S)\s+Status: (.*)$/;

	my $fullname = $fullname{$file};
	print STDERR "Huh? Status '$status' reported for unexpected file '$file'\n"
	    unless defined $fullname;

	# This response means the file does not exist except in
	# CVS's attic, so set the status accordingly
	$status = "In-attic"
	    if $file =~ /^no file /
		&& $status eq 'Up-to-date';

	$cvsstat{$fullname{$file}} = $status
	    if defined $fullname{$file};
      }
    }
}

# ... Validate that new files have the correct status
foreach my $f (@afiles) {
    next unless defined(my $stat = $cvsstat{$f});

    # This means the file has never been seen before
    next if $stat eq 'Unknown';

    # This means the file has been seen before but was removed
    next if $stat eq 'In-attic';

    $dirty = 1;
	warn "File $f is already known in your CVS checkout -- perhaps it has been added by another user. Or this may indicate that it exists on a different branch. If this is the case, use -f to force the merge.\n";
	warn "Status was: $cvsstat{$f}\n";
}

# ... validate known files.
foreach my $f (@files) {
    next if grep { $_ eq $f } @afiles;
    # TODO:we need to handle removed in cvs
    unless (defined ($cvsstat{$f}) and $cvsstat{$f} eq "Up-to-date") {
	$dirty = 1;
	warn "File $f not up to date but has status '$cvsstat{$f}' in your CVS checkout!\n";
    }

    # Depending on how your BUT tree got imported from CVS you may
    # have a conflict between expanded keywords in your CVS tree and
    # unexpanded keywords in the patch about to be applied.
    if ($opt_k) {
	my $orig_file ="$f.orig";
	rename $f, $orig_file;
	open(FILTER_IN, "<$orig_file") or die "Cannot open $orig_file\n";
	open(FILTER_OUT, ">$f") or die "Cannot open $f\n";
	while (<FILTER_IN>)
	{
	    my $line = $_;
	    $line =~ s/\$([A-Z][a-z]+):[^\$]+\$/\$$1\$/g;
	    print FILTER_OUT $line;
	}
	close FILTER_IN;
	close FILTER_OUT;
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
if ($opt_W) {
    system("but checkout -q $cummit^0") && die "cannot patch";
} else {
    `BUT_INDEX_FILE=$tmpdir/index but apply $context --summary --numstat --apply <.cvsexportcummit.diff` || die "cannot patch";
}

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

print "cummit to CVS\n";
print "Patch title (first comment line): $title\n";
my @cummitfiles = map { unless (m/\s/) { '\''.$_.'\''; } else { $_; }; } (@files);
my $cmd = join(' ', @cvs)." cummit -F .msg @cummitfiles";

if ($dirtypatch) {
    print "NOTE: One or more hunks failed to apply cleanly.\n";
    print "You'll need to apply the patch in .cvsexportcummit.diff manually\n";
    print "using a patch program. After applying the patch and resolving the\n";
    print "problems you may cummit using:";
    print "\n    cd \"$opt_w\"" if $opt_w;
    print "\n    $cmd\n";
    print "\n    but checkout $go_back_to\n" if $go_back_to;
    print "\n";
    exit(1);
}

if ($opt_c) {
    print "Autocummit\n  $cmd\n";
    print xargs_safe_pipe_capture([@cvs, 'cummit', '-F', '.msg'], @files);
    if ($?) {
	die "Exiting: The cummit did not succeed";
    }
    print "cummitted successfully to CVS\n";
    # clean up
    unlink(".msg");
} else {
    print "Ready for you to cummit, just run:\n\n   $cmd\n";
}

# clean up
unlink(".cvsexportcummit.diff");

if ($opt_W) {
    system("but checkout $go_back_to") && die "cannot move back to $go_back_to";
    if (!($go_back_to =~ /^[0-9a-fA-F]{$hexsz}$/)) {
	system("but symbolic-ref HEAD $go_back_to") &&
	    die "cannot move back to $go_back_to";
    }
}

# CVS version 1.11.x and 1.12.x sleeps the wrong way to ensure the timestamp
# used by CVS and the one set by subsequence file modifications are different.
# If they are not different CVS will not detect changes.
sleep(1);

sub usage {
	print STDERR <<END;
usage: BUT_DIR=/path/to/.but but cvsexportcummit [-h] [-p] [-v] [-c] [-f] [-u] [-k] [-w cvsworkdir] [-m msgprefix] [ parent ] cummit
END
	exit(1);
}

# An alternative to `command` that allows input to be passed as an array
# to work around shell problems with weird characters in arguments
# if the exec returns non-zero we die
sub safe_pipe_capture {
    my @output;
    if (my $pid = open my $child, '-|') {
	binmode($child, ":crlf");
	@output = (<$child>);
	close $child or die join(' ',@_).": $! $?";
    } else {
	exec(@_) or die "$! $?"; # exec() can fail the executable can't be found
    }
    return wantarray ? @output : join('',@output);
}

sub xargs_safe_pipe_capture {
	my $MAX_ARG_LENGTH = 65536;
	my $cmd = shift;
	my @output;
	my $output;
	while(@_) {
		my @args;
		my $length = 0;
		while(@_ && $length < $MAX_ARG_LENGTH) {
			push @args, shift;
			$length += length($args[$#args]);
		}
		if (wantarray) {
			push @output, safe_pipe_capture(@$cmd, @args);
		}
		else {
			$output .= safe_pipe_capture(@$cmd, @args);
		}
	}
	return wantarray ? @output : $output;
}
