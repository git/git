#!/usr/bin/perl -w
#
# This tool is copyright (c) 2005, Martin Langhoff.
# It is released under the Gnu Public License, version 2.
#
# The basic idea is to walk the output of tla abrowse, 
# fetch the changesets and apply them. 
#

=head1 Invocation

    git-archimport [ -h ] [ -v ] [ -T ] [ -t tempdir ] <archive>/<branch> [ <archive>/<branch> ]

Imports a project from one or more Arch repositories. It will follow branches
and repositories within the namespaces defined by the <archive/branch>
parameters suppplied. If it cannot find the remote branch a merge comes from
it will just import it as a regular commit. If it can find it, it will mark it 
as a merge whenever possible.

See man (1) git-archimport for more details.

=head1 TODO

 - create tag objects instead of ref tags
 - audit shell-escaping of filenames
 - hide our private tags somewhere smarter
 - find a way to make "cat *patches | patch" safe even when patchfiles are missing newlines  

=head1 Devel tricks

Add print in front of the shell commands invoked via backticks. 

=head1 Devel Notes

There are several places where Arch and git terminology are intermixed
and potentially confused.

The notion of a "branch" in git is approximately equivalent to
a "archive/category--branch--version" in Arch.  Also, it should be noted
that the "--branch" portion of "archive/category--branch--version" is really
optional in Arch although not many people (nor tools!) seem to know this.
This means that "archive/category--version" is also a valid "branch"
in git terms.

We always refer to Arch names by their fully qualified variant (which
means the "archive" name is prefixed.

For people unfamiliar with Arch, an "archive" is the term for "repository",
and can contain multiple, unrelated branches.

=cut

use strict;
use warnings;
use Getopt::Std;
use File::Spec;
use File::Temp qw(tempfile tempdir);
use File::Path qw(mkpath rmtree);
use File::Basename qw(basename dirname);
use Time::Local;
use IO::Socket;
use IO::Pipe;
use POSIX qw(strftime dup2);
use Data::Dumper qw/ Dumper /;
use IPC::Open2;

$SIG{'PIPE'}="IGNORE";
$ENV{'TZ'}="UTC";

my $git_dir = $ENV{"GIT_DIR"} || ".git";
$ENV{"GIT_DIR"} = $git_dir;
my $ptag_dir = "$git_dir/archimport/tags";

our($opt_h,$opt_v, $opt_T,$opt_t,$opt_o);

sub usage() {
    print STDERR <<END;
Usage: ${\basename $0}     # fetch/update GIT from Arch
       [ -o ] [ -h ] [ -v ] [ -T ] [ -t tempdir ] 
       repository/arch-branch [ repository/arch-branch] ...
END
    exit(1);
}

getopts("Thvt:") or usage();
usage if $opt_h;

@ARGV >= 1 or usage();
my @arch_roots = @ARGV;

$ENV{'TMPDIR'} = $opt_t if $opt_t; # $ENV{TMPDIR} will affect tempdir() calls:
my $tmp = tempdir('git-archimport-XXXXXX', TMPDIR => 1, CLEANUP => 1);
$opt_v && print "+ Using $tmp as temporary directory\n";

my @psets  = ();                # the collection
my %psets  = ();                # the collection, by name

my %rptags = ();                # my reverse private tags
                                # to map a SHA1 to a commitid
my $TLA = $ENV{'ARCH_CLIENT'} || 'tla';

foreach my $root (@arch_roots) {
    my ($arepo, $abranch) = split(m!/!, $root);
    open ABROWSE, "tla abrowse -f -A $arepo --desc --merges $abranch |" 
        or die "Problems with tla abrowse: $!";
    
    my %ps        = ();         # the current one
    my $mode      = '';
    my $lastseen  = '';
    
    while (<ABROWSE>) {
        chomp;
        
        # first record padded w 8 spaces
        if (s/^\s{8}\b//) {
            
            # store the record we just captured
            if (%ps) {
                my %temp = %ps; # break references
                push (@psets, \%temp);
		$psets{$temp{id}} = \%temp;
                %ps = ();
            }
            
            my ($id, $type) = split(m/\s{3}/, $_);
            $ps{id}   = $id;
            $ps{repo} = $arepo;

            # deal with types
            if ($type =~ m/^\(simple changeset\)/) {
                $ps{type} = 's';
            } elsif ($type eq '(initial import)') {
                $ps{type} = 'i';
            } elsif ($type =~ m/^\(tag revision of (.+)\)/) {
                $ps{type} = 't';
                $ps{tag}  = $1;
            } else { 
                warn "Unknown type $type";
            }
            $lastseen = 'id';
        }
        
        if (s/^\s{10}//) { 
            # 10 leading spaces or more 
            # indicate commit metadata
            
            # date & author 
            if ($lastseen eq 'id' && m/^\d{4}-\d{2}-\d{2}/) {
                
                my ($date, $authoremail) = split(m/\s{2,}/, $_);
                $ps{date}   = $date;
                $ps{date}   =~ s/\bGMT$//; # strip off trailign GMT
                if ($ps{date} =~ m/\b\w+$/) {
                    warn 'Arch dates not in GMT?! - imported dates will be wrong';
                }
            
                $authoremail =~ m/^(.+)\s(\S+)$/;
                $ps{author} = $1;
                $ps{email}  = $2;
            
                $lastseen = 'date';
            
            } elsif ($lastseen eq 'date') {
                # the only hint is position
                # subject is after date
                $ps{subj} = $_;
                $lastseen = 'subj';
            
            } elsif ($lastseen eq 'subj' && $_ eq 'merges in:') {
                $ps{merges} = [];
                $lastseen = 'merges';
            
            } elsif ($lastseen eq 'merges' && s/^\s{2}//) {
                push (@{$ps{merges}}, $_);
            } else {
                warn 'more metadata after merges!?';
            }
            
        }
    }

    if (%ps) {
        my %temp = %ps;         # break references
        push (@psets, \%temp);  
	$psets{ $temp{id} } = \%temp;
        %ps = ();
    }    
    close ABROWSE;
}                               # end foreach $root

## Order patches by time
@psets = sort {$a->{date}.$b->{id} cmp $b->{date}.$b->{id}} @psets;

#print Dumper \@psets;

##
## TODO cleanup irrelevant patches
##      and put an initial import
##      or a full tag
my $import = 0;
unless (-d $git_dir) { # initial import
    if ($psets[0]{type} eq 'i' || $psets[0]{type} eq 't') {
        print "Starting import from $psets[0]{id}\n";
	`git-init-db`;
	die $! if $?;
	$import = 1;
    } else {
        die "Need to start from an import or a tag -- cannot use $psets[0]{id}";
    }
} else {    # progressing an import
    # load the rptags
    opendir(DIR, "$git_dir/archimport/tags")
	|| die "can't opendir: $!";
    while (my $file = readdir(DIR)) {
        # skip non-interesting-files
        next unless -f "$ptag_dir/$file";
   
        # convert first '--' to '/' from old git-archimport to use
        # as an archivename/c--b--v private tag
        if ($file !~ m!,!) {
            my $oldfile = $file;
            $file =~ s!--!,!;
            print STDERR "converting old tag $oldfile to $file\n";
            rename("$ptag_dir/$oldfile", "$ptag_dir/$file") or die $!;
        }
	my $sha = ptag($file);
	chomp $sha;
	$rptags{$sha} = $file;
    }
    closedir DIR;
}

# process patchsets
# extract the Arch repository name (Arch "archive" in Arch-speak)
sub extract_reponame {
    my $fq_cvbr = shift; # archivename/[[[[category]branch]version]revision]
    return (split(/\//, $fq_cvbr))[0];
}
 
sub extract_versionname {
    my $name = shift;
    $name =~ s/--(?:patch|version(?:fix)?|base)-\d+$//;
    return $name;
}

# convert a fully-qualified revision or version to a unique dirname:
#   normalperson@yhbt.net-05/mpd--uclinux--1--patch-2 
# becomes: normalperson@yhbt.net-05,mpd--uclinux--1
#
# the git notion of a branch is closer to
# archive/category--branch--version than archive/category--branch, so we
# use this to convert to git branch names.
# Also, keep archive names but replace '/' with ',' since it won't require
# subdirectories, and is safer than swapping '--' which could confuse
# reverse-mapping when dealing with bastard branches that
# are just archive/category--version  (no --branch)
sub tree_dirname {
    my $revision = shift;
    my $name = extract_versionname($revision);
    $name =~ s#/#,#;
    return $name;
}

# old versions of git-archimport just use the <category--branch> part:
sub old_style_branchname {
    my $id = shift;
    my $ret = safe_pipe_capture($TLA,'parse-package-name','-p',$id);
    chomp $ret;
    return $ret;
}

*git_branchname = $opt_o ? *old_style_branchname : *tree_dirname;

# process patchsets
foreach my $ps (@psets) {
    $ps->{branch} = git_branchname($ps->{id});

    #
    # ensure we have a clean state 
    # 
    if (`git diff-files`) {
        die "Unclean tree when about to process $ps->{id} " .
            " - did we fail to commit cleanly before?";
    }
    die $! if $?;

    #
    # skip commits already in repo
    #
    if (ptag($ps->{id})) {
      $opt_v && print " * Skipping already imported: $ps->{id}\n";
      next;
    }

    print " * Starting to work on $ps->{id}\n";

    # 
    # create the branch if needed
    #
    if ($ps->{type} eq 'i' && !$import) {
        die "Should not have more than one 'Initial import' per GIT import: $ps->{id}";
    }

    unless ($import) { # skip for import
        if ( -e "$git_dir/refs/heads/$ps->{branch}") {
            # we know about this branch
            system('git-checkout',$ps->{branch});
        } else {
            # new branch! we need to verify a few things
            die "Branch on a non-tag!" unless $ps->{type} eq 't';
            my $branchpoint = ptag($ps->{tag});
            die "Tagging from unknown id unsupported: $ps->{tag}" 
                unless $branchpoint;
            
            # find where we are supposed to branch from
            system('git-checkout','-b',$ps->{branch},$branchpoint);

            # If we trust Arch with the fact that this is just 
            # a tag, and it does not affect the state of the tree
            # then we just tag and move on
            tag($ps->{id}, $branchpoint);
            ptag($ps->{id}, $branchpoint);
            print " * Tagged $ps->{id} at $branchpoint\n";
            next;
        } 
        die $! if $?;
    } 

    #
    # Apply the import/changeset/merge into the working tree
    # 
    if ($ps->{type} eq 'i' || $ps->{type} eq 't') {
        apply_import($ps) or die $!;
        $import=0;
    } elsif ($ps->{type} eq 's') {
        apply_cset($ps);
    }

    #
    # prepare update git's index, based on what arch knows
    # about the pset, resolve parents, etc
    #
    my $tree;
    
    my $commitlog = safe_pipe_capture($TLA,'cat-archive-log',$ps->{id}); 
    die "Error in cat-archive-log: $!" if $?;
        
    # parselog will git-add/rm files
    # and generally prepare things for the commit
    # NOTE: parselog will shell-quote filenames! 
    my ($sum, $msg, $add, $del, $mod, $ren) = parselog($commitlog);
    my $logmessage = "$sum\n$msg";


    # imports don't give us good info
    # on added files. Shame on them
    if ($ps->{type} eq 'i' || $ps->{type} eq 't') { 
        `find . -type f -print0 | grep -zv '^./$git_dir' | xargs -0 -l100 git-update-index --add`;
        `git-ls-files --deleted -z | xargs --no-run-if-empty -0 -l100 git-update-index --remove`;
    }

    if (@$add) {
        while (@$add) {
            my @slice = splice(@$add, 0, 100);
            my $slice = join(' ', @slice);          
            `git-update-index --add $slice`;
            die "Error in git-update-index --add: $!" if $?;
        }
    }
    if (@$del) {
        foreach my $file (@$del) {
            unlink $file or die "Problems deleting $file : $!";
        }
        while (@$del) {
            my @slice = splice(@$del, 0, 100);
            my $slice = join(' ', @slice);
            `git-update-index --remove $slice`;
            die "Error in git-update-index --remove: $!" if $?;
        }
    }
    if (@$ren) {                # renamed
        if (@$ren % 2) {
            die "Odd number of entries in rename!?";
        }
        ;
        while (@$ren) {
            my $from = pop @$ren;
            my $to   = pop @$ren;           

            unless (-d dirname($to)) {
                mkpath(dirname($to)); # will die on err
            }
            #print "moving $from $to";
            `mv $from $to`;
            die "Error renaming $from $to : $!" if $?;
            `git-update-index --remove $from`;
            die "Error in git-update-index --remove: $!" if $?;
            `git-update-index --add $to`;
            die "Error in git-update-index --add: $!" if $?;
        }

    }
    if (@$mod) {                # must be _after_ renames
        while (@$mod) {
            my @slice = splice(@$mod, 0, 100);
            my $slice = join(' ', @slice);
            `git-update-index $slice`;
            die "Error in git-update-index: $!" if $?;
        }
    }

    # warn "errors when running git-update-index! $!";
    $tree = `git-write-tree`;
    die "cannot write tree $!" if $?;
    chomp $tree;
        
    
    #
    # Who's your daddy?
    #
    my @par;
    if ( -e "$git_dir/refs/heads/$ps->{branch}") {
        if (open HEAD, "<","$git_dir/refs/heads/$ps->{branch}") {
            my $p = <HEAD>;
            close HEAD;
            chomp $p;
            push @par, '-p', $p;
        } else { 
            if ($ps->{type} eq 's') {
                warn "Could not find the right head for the branch $ps->{branch}";
            }
        }
    }
    
    if ($ps->{merges}) {
        push @par, find_parents($ps);
    }

    #    
    # Commit, tag and clean state
    #
    $ENV{TZ}                  = 'GMT';
    $ENV{GIT_AUTHOR_NAME}     = $ps->{author};
    $ENV{GIT_AUTHOR_EMAIL}    = $ps->{email};
    $ENV{GIT_AUTHOR_DATE}     = $ps->{date};
    $ENV{GIT_COMMITTER_NAME}  = $ps->{author};
    $ENV{GIT_COMMITTER_EMAIL} = $ps->{email};
    $ENV{GIT_COMMITTER_DATE}  = $ps->{date};

    my ($pid, $commit_rh, $commit_wh);
    $commit_rh = 'commit_rh';
    $commit_wh = 'commit_wh';
    
    $pid = open2(*READER, *WRITER,'git-commit-tree',$tree,@par) 
        or die $!;
    print WRITER $logmessage;   # write
    close WRITER;
    my $commitid = <READER>;    # read
    chomp $commitid;
    close READER;
    waitpid $pid,0;             # close;

    if (length $commitid != 40) {
        die "Something went wrong with the commit! $! $commitid";
    }
    #
    # Update the branch
    # 
    open  HEAD, ">","$git_dir/refs/heads/$ps->{branch}";
    print HEAD $commitid;
    close HEAD;
    system('git-update-ref', 'HEAD', "$ps->{branch}");

    # tag accordingly
    ptag($ps->{id}, $commitid); # private tag
    if ($opt_T || $ps->{type} eq 't' || $ps->{type} eq 'i') {
        tag($ps->{id}, $commitid);
    }
    print " * Committed $ps->{id}\n";
    print "   + tree   $tree\n";
    print "   + commit $commitid\n";
    $opt_v && print "   + commit date is  $ps->{date} \n";
    $opt_v && print "   + parents:  ",join(' ',@par),"\n";
}

sub apply_import {
    my $ps = shift;
    my $bname = git_branchname($ps->{id});

    mkpath($tmp);

    safe_pipe_capture($TLA,'get','-s','--no-pristine',$ps->{id},"$tmp/import");
    die "Cannot get import: $!" if $?;    
    system('rsync','-aI','--delete', '--exclude',$git_dir,
		'--exclude','.arch-ids','--exclude','{arch}',
		"$tmp/import/", './');
    die "Cannot rsync import:$!" if $?;
    
    rmtree("$tmp/import");
    die "Cannot remove tempdir: $!" if $?;
    

    return 1;
}

sub apply_cset {
    my $ps = shift;

    mkpath($tmp);

    # get the changeset
    safe_pipe_capture($TLA,'get-changeset',$ps->{id},"$tmp/changeset");
    die "Cannot get changeset: $!" if $?;
    
    # apply patches
    if (`find $tmp/changeset/patches -type f -name '*.patch'`) {
        # this can be sped up considerably by doing
        #    (find | xargs cat) | patch
        # but that cna get mucked up by patches
        # with missing trailing newlines or the standard 
        # 'missing newline' flag in the patch - possibly
        # produced with an old/buggy diff.
        # slow and safe, we invoke patch once per patchfile
        `find $tmp/changeset/patches -type f -name '*.patch' -print0 | grep -zv '{arch}' | xargs -iFILE -0 --no-run-if-empty patch -p1 --forward -iFILE`;
        die "Problem applying patches! $!" if $?;
    }

    # apply changed binary files
    if (my @modified = `find $tmp/changeset/patches -type f -name '*.modified'`) {
        foreach my $mod (@modified) {
            chomp $mod;
            my $orig = $mod;
            $orig =~ s/\.modified$//; # lazy
            $orig =~ s!^\Q$tmp\E/changeset/patches/!!;
            #print "rsync -p '$mod' '$orig'";
            system('rsync','-p',$mod,"./$orig");
            die "Problem applying binary changes! $!" if $?;
        }
    }

    # bring in new files
    system('rsync','-aI','--exclude',$git_dir,
    		'--exclude','.arch-ids',
		'--exclude', '{arch}',
		"$tmp/changeset/new-files-archive/",'./');

    # deleted files are hinted from the commitlog processing

    rmtree("$tmp/changeset");
}


# =for reference
# A log entry looks like 
# Revision: moodle-org--moodle--1.3.3--patch-15
# Archive: arch-eduforge@catalyst.net.nz--2004
# Creator: Penny Leach <penny@catalyst.net.nz>
# Date: Wed May 25 14:15:34 NZST 2005
# Standard-date: 2005-05-25 02:15:34 GMT
# New-files: lang/de/.arch-ids/block_glossary_random.php.id
#     lang/de/.arch-ids/block_html.php.id
# New-directories: lang/de/help/questionnaire
#     lang/de/help/questionnaire/.arch-ids
# Renamed-files: .arch-ids/db_sears.sql.id db/.arch-ids/db_sears.sql.id
#    db_sears.sql db/db_sears.sql
# Removed-files: lang/be/docs/.arch-ids/release.html.id
#     lang/be/docs/.arch-ids/releaseold.html.id
# Modified-files: admin/cron.php admin/delete.php
#     admin/editor.html backup/lib.php backup/restore.php
# New-patches: arch-eduforge@catalyst.net.nz--2004/moodle-org--moodle--1.3.3--patch-15
# Summary: Updating to latest from MOODLE_14_STABLE (1.4.5+)
# Keywords:
#
# Updating yadda tadda tadda madda
sub parselog {
    my $log = shift;
    #print $log;

    my (@add, @del, @mod, @ren, @kw, $sum, $msg );

    if ($log =~ m/(?:\n|^)New-files:(.*?)(?=\n\w)/s ) {
        my $files = $1;
        @add = split(m/\s+/s, $files);
    }
       
    if ($log =~ m/(?:\n|^)Removed-files:(.*?)(?=\n\w)/s ) {
        my $files = $1;
        @del = split(m/\s+/s, $files);
    }
    
    if ($log =~ m/(?:\n|^)Modified-files:(.*?)(?=\n\w)/s ) {
        my $files = $1;
        @mod = split(m/\s+/s, $files);
    }
    
    if ($log =~ m/(?:\n|^)Renamed-files:(.*?)(?=\n\w)/s ) {
        my $files = $1;
        @ren = split(m/\s+/s, $files);
    }

    $sum ='';
    if ($log =~ m/^Summary:(.+?)$/m ) {
        $sum = $1;
        $sum =~ s/^\s+//;
        $sum =~ s/\s+$//;
    }

    $msg = '';
    if ($log =~ m/\n\n(.+)$/s) {
        $msg = $1;
        $msg =~ s/^\s+//;
        $msg =~ s/\s+$//;
    }


    # cleanup the arrays
    foreach my $ref ( (\@add, \@del, \@mod, \@ren) ) {
        my @tmp = ();
        while (my $t = pop @$ref) {
            next unless length ($t);
            next if $t =~ m!\{arch\}/!;
            next if $t =~ m!\.arch-ids/!;
            next if $t =~ m!\.arch-inventory$!;
           # tla cat-archive-log will give us filenames with spaces as file\(sp)name - why?
           # we can assume that any filename with \ indicates some pika escaping that we want to get rid of.
           if  ($t =~ /\\/ ){
               $t = (safe_pipe_capture($TLA,'escape','--unescaped',$t))[0];
           }
            push (@tmp, $t);
        }
        @$ref = @tmp;
    }
    
    #print Dumper [$sum, $msg, \@add, \@del, \@mod, \@ren]; 
    return       ($sum, $msg, \@add, \@del, \@mod, \@ren); 
}

# write/read a tag
sub tag {
    my ($tag, $commit) = @_;
 
    if ($opt_o) {
        $tag =~ s|/|--|g;
    } else {
        # don't use subdirs for tags yet, it could screw up other porcelains
        $tag =~ s|/|,|g;
    }
    
    if ($commit) {
        open(C,">","$git_dir/refs/tags/$tag")
            or die "Cannot create tag $tag: $!\n";
        print C "$commit\n"
            or die "Cannot write tag $tag: $!\n";
        close(C)
            or die "Cannot write tag $tag: $!\n";
        print " * Created tag '$tag' on '$commit'\n" if $opt_v;
    } else {                    # read
        open(C,"<","$git_dir/refs/tags/$tag")
            or die "Cannot read tag $tag: $!\n";
        $commit = <C>;
        chomp $commit;
        die "Error reading tag $tag: $!\n" unless length $commit == 40;
        close(C)
            or die "Cannot read tag $tag: $!\n";
        return $commit;
    }
}

# write/read a private tag
# reads fail softly if the tag isn't there
sub ptag {
    my ($tag, $commit) = @_;

    # don't use subdirs for tags yet, it could screw up other porcelains
    $tag =~ s|/|,|g; 
    
    my $tag_file = "$ptag_dir/$tag";
    my $tag_branch_dir = dirname($tag_file);
    mkpath($tag_branch_dir) unless (-d $tag_branch_dir);

    if ($commit) {              # write
        open(C,">",$tag_file)
            or die "Cannot create tag $tag: $!\n";
        print C "$commit\n"
            or die "Cannot write tag $tag: $!\n";
        close(C)
            or die "Cannot write tag $tag: $!\n";
	$rptags{$commit} = $tag 
	    unless $tag =~ m/--base-0$/;
    } else {                    # read
        # if the tag isn't there, return 0
        unless ( -s $tag_file) {
            return 0;
        }
        open(C,"<",$tag_file)
            or die "Cannot read tag $tag: $!\n";
        $commit = <C>;
        chomp $commit;
        die "Error reading tag $tag: $!\n" unless length $commit == 40;
        close(C)
            or die "Cannot read tag $tag: $!\n";
	unless (defined $rptags{$commit}) {
	    $rptags{$commit} = $tag;
	}
        return $commit;
    }
}

sub find_parents {
    #
    # Identify what branches are merging into me
    # and whether we are fully merged
    # git-merge-base <headsha> <headsha> should tell
    # me what the base of the merge should be 
    #
    my $ps = shift;

    my %branches; # holds an arrayref per branch
                  # the arrayref contains a list of
                  # merged patches between the base
                  # of the merge and the current head

    my @parents;  # parents found for this commit

    # simple loop to split the merges
    # per branch
    foreach my $merge (@{$ps->{merges}}) {
	my $branch = git_branchname($merge);
	unless (defined $branches{$branch} ){
	    $branches{$branch} = [];
	}
	push @{$branches{$branch}}, $merge;
    }

    #
    # foreach branch find a merge base and walk it to the 
    # head where we are, collecting the merged patchsets that
    # Arch has recorded. Keep that in @have
    # Compare that with the commits on the other branch
    # between merge-base and the tip of the branch (@need)
    # and see if we have a series of consecutive patches
    # starting from the merge base. The tip of the series
    # of consecutive patches merged is our new parent for 
    # that branch.
    #
    foreach my $branch (keys %branches) {

	# check that we actually know about the branch
	next unless -e "$git_dir/refs/heads/$branch";

	my $mergebase = `git-merge-base $branch $ps->{branch}`;
 	if ($?) { 
 	    # Don't die here, Arch supports one-way cherry-picking
 	    # between branches with no common base (or any relationship
 	    # at all beforehand)
 	    warn "Cannot find merge base for $branch and $ps->{branch}";
 	    next;
 	}
	chomp $mergebase;

	# now walk up to the mergepoint collecting what patches we have
	my $branchtip = git_rev_parse($ps->{branch});
	my @ancestors = `git-rev-list --merge-order $branchtip ^$mergebase`;
	my %have; # collected merges this branch has
	foreach my $merge (@{$ps->{merges}}) {
	    $have{$merge} = 1;
	}
	my %ancestorshave;
	foreach my $par (@ancestors) {
	    $par = commitid2pset($par);
	    if (defined $par->{merges}) {
		foreach my $merge (@{$par->{merges}}) {
		    $ancestorshave{$merge}=1;
		}
	    }
	}
	# print "++++ Merges in $ps->{id} are....\n";
	# my @have = sort keys %have;	print Dumper(\@have);

	# merge what we have with what ancestors have
	%have = (%have, %ancestorshave);

	# see what the remote branch has - these are the merges we 
	# will want to have in a consecutive series from the mergebase
	my $otherbranchtip = git_rev_parse($branch);
	my @needraw = `git-rev-list --merge-order $otherbranchtip ^$mergebase`;
	my @need;
	foreach my $needps (@needraw) { 	# get the psets
	    $needps = commitid2pset($needps);
	    # git-rev-list will also
	    # list commits merged in via earlier 
	    # merges. we are only interested in commits
	    # from the branch we're looking at
	    if ($branch eq $needps->{branch}) {
		push @need, $needps->{id};
	    }
	}

	# print "++++ Merges from $branch we want are....\n";
	# print Dumper(\@need);

	my $newparent;
	while (my $needed_commit = pop @need) {
	    if ($have{$needed_commit}) {
		$newparent = $needed_commit;
	    } else {
		last; # break out of the while
	    }
	}
	if ($newparent) {
	    push @parents, $newparent;
	}


    } # end foreach branch

    # prune redundant parents
    my %parents;
    foreach my $p (@parents) {
	$parents{$p} = 1;
    }
    foreach my $p (@parents) {
	next unless exists $psets{$p}{merges};
	next unless ref    $psets{$p}{merges};
	my @merges = @{$psets{$p}{merges}};
	foreach my $merge (@merges) {
	    if ($parents{$merge}) { 
		delete $parents{$merge};
	    }
	}
    }
    @parents = ();
    foreach (keys %parents) {
        push @parents, '-p', ptag($_);
    }
    return @parents;
}

sub git_rev_parse {
    my $name = shift;
    my $val  = `git-rev-parse $name`;
    die "Error: git-rev-parse $name" if $?;
    chomp $val;
    return $val;
}

# resolve a SHA1 to a known patchset
sub commitid2pset {
    my $commitid = shift;
    chomp $commitid;
    my $name = $rptags{$commitid} 
	|| die "Cannot find reverse tag mapping for $commitid";
    $name =~ s|,|/|;
    my $ps   = $psets{$name} 
	|| (print Dumper(sort keys %psets)) && die "Cannot find patchset for $name";
    return $ps;
}

# an alterative to `command` that allows input to be passed as an array
# to work around shell problems with weird characters in arguments
sub safe_pipe_capture {
    my @output;
    if (my $pid = open my $child, '-|') {
        @output = (<$child>);
        close $child or die join(' ',@_).": $! $?";
    } else {
	exec(@_) or die $?; # exec() can fail the executable can't be found
    }
    return wantarray ? @output : join('',@output);
}


