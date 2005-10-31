#!/usr/bin/perl -w
#
# Copyright (c) 2005 Martin Langhoff
#
# Walk the tags and find if they match a commit
# expects a SHA1 of a commit. Option -t enables 
# searching trees too.
#

use strict;
use File::Basename;
use File::Find;
use Getopt::Std;

my $git_dir = $ENV{GIT_DIR} || '.git';
$git_dir =~ s|/$||; # chomp trailing slash

# options
our $opt_t;
getopts("t") || usage();

my @tagfiles   = `find $git_dir/refs/tags -follow -type f`; # haystack
my $target = shift @ARGV;                     # needle
unless ($target) {
    usage();
}

# drive the processing from the find hook
# slower, safer (?) than the find utility
find( { wanted   => \&process,
	no_chdir => 1,
	follow   => 1,
      }, "$git_dir/refs/tags");


sub process {
    my ($dev,$ino,$mode,$nlink,$uid,$gid);

    # process only regular files
    unless ((($dev,$ino,$mode,$nlink,$uid,$gid) = lstat($_)) && -f _) {
	return 1; # ignored anyway
    }

    my $tagfile = $_;
    chomp $tagfile;
    my $tagname = substr($tagfile, length($git_dir.'/refs/tags/'));

    my $tagid = quickread($tagfile);
    chomp $tagid;

    # is it just a soft tag?
    if ($tagid eq $target) {
	print "$tagname\n";
	return 1; # done with this tag
    }

    # grab the first 2 lines (the whole tag could be large)
    my $tagobj = `git-cat-file tag $tagid | head -n2 `;
    if ($tagobj =~  m/^type commit$/m) { # only deal with commits

	if ($tagobj =~ m/^object $target$/m) { # match on the commit
	    print "$tagname\n";

	} elsif ( $opt_t &&                      # follow the commit
		 $tagobj =~ m/^object (\S+)$/m) { # and try to match trees
	    my $commitid = $1;
	    my $commitobj = `git-cat-file commit $commitid | head -n1`;
	    chomp $commitobj;
	    $commitobj =~ m/^tree (\S+)$/;
	    my $treeid = $1;
	    if ($target eq $treeid) {
		print "$tagname\n";
	    }
	}
    }
}

sub quickread {
    my $file = shift;
    local $/; # undef: slurp mode
    open FILE, "<$file"
	or die "Cannot open $file : $!";
    my $content = <FILE>;
    close FILE;
    return $content;
}

sub usage {
	print STDERR <<END;
Usage: ${\basename $0}     # find tags for a commit or tree
       [ -t ] <commit-or-tree-sha1>
END
	exit(1);
}
