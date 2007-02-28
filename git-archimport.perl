#!/usr/bin/perl -w
#
# This tool is copyright (c) 2005, Martin Langhoff.
# It is released under the Gnu Public License, version 2.
#
# The basic idea is to walk the output of tla abrowse, 
# fetch the changesets and apply them. 
#

=head1 Invocation

    git-archimport [ -h ] [ -v ] [ -o ] [ -a ] [ -f ] [ -T ] 
    	[ -D depth] [ -t tempdir ] <archive>/<branch> [ <archive>/<branch> ]

Imports a project from one or more Arch repositories. It will follow branches
and repositories within the namespaces defined by the <archive/branch>
parameters supplied. If it cannot find the remote branch a merge comes from
it will just import it as a regular commit. If it can find it, it will mark it 
as a merge whenever possible.

See man (1) git-archimport for more details.

=head1 TODO

 - create tag objects instead of ref tags
 - audit shell-escaping of filenames
 - hide our private tags somewhere smarter
 - find a way to make "cat *patches | patch" safe even when patchfiles are missing newlines  
 - sort and apply patches by graphing ancestry relations instead of just
   relying in dates supplied in the changeset itself.
   tla ancestry-graph -m could be helpful here...

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
use File::Temp qw(tempdir);
use File::Path qw(mkpath rmtree);
use File::Basename qw(basename dirname);
use Data::Dumper qw/ Dumper /;
use IPC::Open2;

$SIG{'PIPE'}="IGNORE";
$ENV{'TZ'}="UTC";

my $git_dir = $ENV{"GIT_DIR"} || ".git";
$ENV{"GIT_DIR"} = $git_dir;
my $ptag_dir = "$git_dir/archimport/tags";

our($opt_h,$opt_f,$opt_v,$opt_T,$opt_t,$opt_D,$opt_a,$opt_o);

sub usage() {
    print STDERR <<END;
Usage: ${\basename $0}     # fetch/update GIT from Arch
       [ -h ] [ -v ] [ -o ] [ -a ] [ -f ] [ -T ] [ -D depth ] [ -t tempdir ]
       repository/arch-branch [ repository/arch-branch] ...
END
    exit(1);
}

getopts("fThvat:D:") or usage();
usage if $opt_h;

@ARGV >= 1 or usage();
# $arch_branches:
# values associated with keys:
#   =1 - Arch version / git 'branch' detected via abrowse on a limit
#   >1 - Arch version / git 'branch' of an auxiliary branch we've merged
my %arch_branches = map { $_ => 1 } @ARGV;

$ENV{'TMPDIR'} = $opt_t if $opt_t; # $ENV{TMPDIR} will affect tempdir() calls:
my $tmp = tempdir('git-archimport-XXXXXX', TMPDIR => 1, CLEANUP => 1);
$opt_v && print "+ Using $tmp as temporary directory\n";

unless (-d $git_dir) { # initial import needs empty directory
    opendir DIR, '.' or die "Unable to open current directory: $!\n";
    while (my $entry = readdir DIR) {
        $entry =~ /^\.\.?$/ or
            die "Initial import needs an empty current working directory.\n"
    }
    closedir DIR
}

my %reachable = ();             # Arch repositories we can access
my %unreachable = ();           # Arch repositories we can't access :<
my @psets  = ();                # the collection
my %psets  = ();                # the collection, by name
my %stats  = (			# Track which strategy we used to import:
	get_tag => 0, replay => 0, get_new => 0, get_delta => 0,
        simple_changeset => 0, import_or_tag => 0
);

my %rptags = ();                # my reverse private tags
                                # to map a SHA1 to a commitid
my $TLA = $ENV{'ARCH_CLIENT'} || 'tla';

sub do_abrowse {
    my $stage = shift;
    while (my ($limit, $level) = each %arch_branches) {
        next unless $level == $stage;
        
	open ABROWSE, "$TLA abrowse -fkD --merges $limit |" 
                                or die "Problems with tla abrowse: $!";
    
        my %ps        = ();         # the current one
        my $lastseen  = '';
    
        while (<ABROWSE>) {
            chomp;
            
            # first record padded w 8 spaces
            if (s/^\s{8}\b//) {
                my ($id, $type) = split(m/\s+/, $_, 2);

                my %last_ps;
                # store the record we just captured
                if (%ps && !exists $psets{ $ps{id} }) {
                    %last_ps = %ps; # break references
                    push (@psets, \%last_ps);
                    $psets{ $last_ps{id} } = \%last_ps;
                }
                
                my $branch = extract_versionname($id);
                %ps = ( id => $id, branch => $branch );
                if (%last_ps && ($last_ps{branch} eq $branch)) {
                    $ps{parent_id} = $last_ps{id};
                }
                
                $arch_branches{$branch} = 1;
                $lastseen = 'id';

                # deal with types (should work with baz or tla):
                if ($type =~ m/\(.*changeset\)/) {
                    $ps{type} = 's';
                } elsif ($type =~ /\(.*import\)/) {
                    $ps{type} = 'i';
                } elsif ($type =~ m/\(tag.*?(\S+\@\S+).*?\)/) {
                    $ps{type} = 't';
                    # read which revision we've tagged when we parse the log
                    $ps{tag}  = $1;
                } else { 
                    warn "Unknown type $type";
                }

                $arch_branches{$branch} = 1;
                $lastseen = 'id';
            } elsif (s/^\s{10}//) { 
                # 10 leading spaces or more 
                # indicate commit metadata
                
                # date
                if ($lastseen eq 'id' && m/^(\d{4}-\d\d-\d\d \d\d:\d\d:\d\d)/){
                    $ps{date}   = $1;
                    $lastseen = 'date';
                } elsif ($_ eq 'merges in:') {
                    $ps{merges} = [];
                    $lastseen = 'merges';
                } elsif ($lastseen eq 'merges' && s/^\s{2}//) {
                    my $id = $_;
                    push (@{$ps{merges}}, $id);
                   
                    # aggressive branch finding:
                    if ($opt_D) {
                        my $branch = extract_versionname($id);
                        my $repo = extract_reponame($branch);
                        
                        if (archive_reachable($repo) &&
                                !defined $arch_branches{$branch}) {
                            $arch_branches{$branch} = $stage + 1;
                        }
                    }
                } else {
                    warn "more metadata after merges!?: $_\n" unless /^\s*$/;
                }
            }
        }

        if (%ps && !exists $psets{ $ps{id} }) {
            my %temp = %ps;         # break references
            if (@psets && $psets[$#psets]{branch} eq $ps{branch}) {
                $temp{parent_id} = $psets[$#psets]{id};
            }
            push (@psets, \%temp);  
            $psets{ $temp{id} } = \%temp;
        }    
        
        close ABROWSE or die "$TLA abrowse failed on $limit\n";
    }
}                               # end foreach $root

do_abrowse(1);
my $depth = 2;
$opt_D ||= 0;
while ($depth <= $opt_D) {
    do_abrowse($depth);
    $depth++;
}

## Order patches by time
# FIXME see if we can find a more optimal way to do this by graphing
# the ancestry data and walking it, that way we won't have to rely on
# client-supplied dates
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
	`git-init`;
	die $! if $?;
	$import = 1;
    } else {
        die "Need to start from an import or a tag -- cannot use $psets[0]{id}";
    }
} else {    # progressing an import
    # load the rptags
    opendir(DIR, $ptag_dir)
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

sub process_patchset_accurate {
    my $ps = shift;
    
    # switch to that branch if we're not already in that branch:
    if (-e "$git_dir/refs/heads/$ps->{branch}") {
       system('git-checkout','-f',$ps->{branch}) == 0 or die "$! $?\n";

       # remove any old stuff that got leftover:
       my $rm = safe_pipe_capture('git-ls-files','--others','-z');
       rmtree(split(/\0/,$rm)) if $rm;
    }
    
    # Apply the import/changeset/merge into the working tree
    my $dir = sync_to_ps($ps);
    # read the new log entry:
    my @commitlog = safe_pipe_capture($TLA,'cat-log','-d',$dir,$ps->{id});
    die "Error in cat-log: $!" if $?;
    chomp @commitlog;

    # grab variables we want from the log, new fields get added to $ps:
    # (author, date, email, summary, message body ...)
    parselog($ps, \@commitlog);

    if ($ps->{id} =~ /--base-0$/ && $ps->{id} ne $psets[0]{id}) {
        # this should work when importing continuations 
        if ($ps->{tag} && (my $branchpoint = eval { ptag($ps->{tag}) })) {
            
            # find where we are supposed to branch from
            system('git-checkout','-f','-b',$ps->{branch},
                            $branchpoint) == 0 or die "$! $?\n";
            
            # remove any old stuff that got leftover:
            my $rm = safe_pipe_capture('git-ls-files','--others','-z');
            rmtree(split(/\0/,$rm)) if $rm;

            # If we trust Arch with the fact that this is just 
            # a tag, and it does not affect the state of the tree
            # then we just tag and move on
            tag($ps->{id}, $branchpoint);
            ptag($ps->{id}, $branchpoint);
            print " * Tagged $ps->{id} at $branchpoint\n";
            return 0;
        } else {
            warn "Tagging from unknown id unsupported\n" if $ps->{tag};
        }
        # allow multiple bases/imports here since Arch supports cherry-picks
        # from unrelated trees
    } 
    
    # update the index with all the changes we got
    system('git-diff-files --name-only -z | '.
            'git-update-index --remove -z --stdin') == 0 or die "$! $?\n";
    system('git-ls-files --others -z | '.
            'git-update-index --add -z --stdin') == 0 or die "$! $?\n";
    return 1;
}

# the native changeset processing strategy.  This is very fast, but
# does not handle permissions or any renames involving directories
sub process_patchset_fast {
    my $ps = shift;
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
            return 0;
        } 
        die $! if $?;
    } 

    #
    # Apply the import/changeset/merge into the working tree
    # 
    if ($ps->{type} eq 'i' || $ps->{type} eq 't') {
        apply_import($ps) or die $!;
        $stats{import_or_tag}++;
        $import=0;
    } elsif ($ps->{type} eq 's') {
        apply_cset($ps);
        $stats{simple_changeset}++;
    }

    #
    # prepare update git's index, based on what arch knows
    # about the pset, resolve parents, etc
    #
    
    my @commitlog = safe_pipe_capture($TLA,'cat-archive-log',$ps->{id}); 
    die "Error in cat-archive-log: $!" if $?;
        
    parselog($ps,\@commitlog);

    # imports don't give us good info
    # on added files. Shame on them
    if ($ps->{type} eq 'i' || $ps->{type} eq 't') {
        system('git-ls-files --deleted -z | '.
                'git-update-index --remove -z --stdin') == 0 or die "$! $?\n";
        system('git-ls-files --others -z | '.
                'git-update-index --add -z --stdin') == 0 or die "$! $?\n";
    }

    # TODO: handle removed_directories and renamed_directories:

    if (my $del = $ps->{removed_files}) {
        unlink @$del;
        while (@$del) {
            my @slice = splice(@$del, 0, 100);
            system('git-update-index','--remove','--',@slice) == 0 or
                            die "Error in git-update-index --remove: $! $?\n";
        }
    }

    if (my $ren = $ps->{renamed_files}) {                # renamed
        if (@$ren % 2) {
            die "Odd number of entries in rename!?";
        }
        
        while (@$ren) {
            my $from = shift @$ren;
            my $to   = shift @$ren;           

            unless (-d dirname($to)) {
                mkpath(dirname($to)); # will die on err
            }
            # print "moving $from $to";
            rename($from, $to) or die "Error renaming '$from' '$to': $!\n";
            system('git-update-index','--remove','--',$from) == 0 or
                            die "Error in git-update-index --remove: $! $?\n";
            system('git-update-index','--add','--',$to) == 0 or
                            die "Error in git-update-index --add: $! $?\n";
        }
    }

    if (my $add = $ps->{new_files}) {
        while (@$add) {
            my @slice = splice(@$add, 0, 100);
            system('git-update-index','--add','--',@slice) == 0 or
                            die "Error in git-update-index --add: $! $?\n";
        }
    }

    if (my $mod = $ps->{modified_files}) {
        while (@$mod) {
            my @slice = splice(@$mod, 0, 100);
            system('git-update-index','--',@slice) == 0 or
                            die "Error in git-update-index: $! $?\n";
        }
    }
    return 1; # we successfully applied the changeset
}

if ($opt_f) {
    print "Will import patchsets using the fast strategy\n",
            "Renamed directories and permission changes will be missed\n";
    *process_patchset = *process_patchset_fast;
} else {
    print "Using the default (accurate) import strategy.\n",
            "Things may be a bit slow\n";
    *process_patchset = *process_patchset_accurate;
}
    
foreach my $ps (@psets) {
    # process patchsets
    $ps->{branch} = git_branchname($ps->{id});

    #
    # ensure we have a clean state 
    # 
    if (my $dirty = `git-diff-files`) {
        die "Unclean tree when about to process $ps->{id} " .
            " - did we fail to commit cleanly before?\n$dirty";
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

    process_patchset($ps) or next;

    # warn "errors when running git-update-index! $!";
    my $tree = `git-write-tree`;
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

    my $pid = open2(*READER, *WRITER,'git-commit-tree',$tree,@par) 
        or die $!;
    print WRITER $ps->{summary},"\n\n";
    print WRITER $ps->{message},"\n";
    
    # make it easy to backtrack and figure out which Arch revision this was:
    print WRITER 'git-archimport-id: ',$ps->{id},"\n";
    
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

if ($opt_v) {
    foreach (sort keys %stats) {
        print" $_: $stats{$_}\n";
    }
}
exit 0;

# used by the accurate strategy:
sub sync_to_ps {
    my $ps = shift;
    my $tree_dir = $tmp.'/'.tree_dirname($ps->{id});
    
    $opt_v && print "sync_to_ps($ps->{id}) method: ";

    if (-d $tree_dir) {
        if ($ps->{type} eq 't') {
	    $opt_v && print "get (tag)\n";
            # looks like a tag-only or (worse,) a mixed tags/changeset branch,
            # can't rely on replay to work correctly on these
            rmtree($tree_dir);
            safe_pipe_capture($TLA,'get','--no-pristine',$ps->{id},$tree_dir);
            $stats{get_tag}++;
        } else {
                my $tree_id = arch_tree_id($tree_dir);
                if ($ps->{parent_id} && ($ps->{parent_id} eq $tree_id)) {
                    # the common case (hopefully)
		    $opt_v && print "replay\n";
                    safe_pipe_capture($TLA,'replay','-d',$tree_dir,$ps->{id});
                    $stats{replay}++;
                } else {
                    # getting one tree is usually faster than getting two trees
                    # and applying the delta ...
                    rmtree($tree_dir);
		    $opt_v && print "apply-delta\n";
                    safe_pipe_capture($TLA,'get','--no-pristine',
                                        $ps->{id},$tree_dir);
                    $stats{get_delta}++;
                }
        }
    } else {
        # new branch work
        $opt_v && print "get (new tree)\n";
        safe_pipe_capture($TLA,'get','--no-pristine',$ps->{id},$tree_dir);
        $stats{get_new}++;
    }
   
    # added -I flag to rsync since we're going to fast! AIEEEEE!!!!
    system('rsync','-aI','--delete','--exclude',$git_dir,
#               '--exclude','.arch-inventory',
                '--exclude','.arch-ids','--exclude','{arch}',
                '--exclude','+*','--exclude',',*',
                "$tree_dir/",'./') == 0 or die "Cannot rsync $tree_dir: $! $?";
    return $tree_dir;
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
        # but that can get mucked up by patches
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
# notes: *-files/-directories keys cannot have spaces, they're always
# pika-escaped.  Everything after the first newline
# A log entry looks like:
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
#   summary can be multiline with a leading space just like the above fields
# Keywords:
#
# Updating yadda tadda tadda madda
sub parselog {
    my ($ps, $log) = @_;
    my $key = undef;

    # headers we want that contain filenames:
    my %want_headers = (
        new_files => 1,
        modified_files => 1,
        renamed_files => 1,
        renamed_directories => 1,
        removed_files => 1,
        removed_directories => 1,
    );
    
    chomp (@$log);
    while ($_ = shift @$log) {
        if (/^Continuation-of:\s*(.*)/) {
            $ps->{tag} = $1;
            $key = undef;
        } elsif (/^Summary:\s*(.*)$/ ) {
            # summary can be multiline as long as it has a leading space.
	    # we squeeze it onto a single line, though.
            $ps->{summary} = [ $1 ];
            $key = 'summary';
        } elsif (/^Creator: (.*)\s*<([^\>]+)>/) {
            $ps->{author} = $1;
            $ps->{email} = $2;
            $key = undef;
        # any *-files or *-directories can be read here:
        } elsif (/^([A-Z][a-z\-]+):\s*(.*)$/) {
            my $val = $2;
            $key = lc $1;
            $key =~ tr/-/_/; # too lazy to quote :P
            if ($want_headers{$key}) {
                push @{$ps->{$key}}, split(/\s+/, $val);
            } else {
                $key = undef;
            }
        } elsif (/^$/) {
            last; # remainder of @$log that didn't get shifted off is message
        } elsif ($key) {
            if (/^\s+(.*)$/) {
                if ($key eq 'summary') {
                    push @{$ps->{$key}}, $1;
                } else { # files/directories:
                    push @{$ps->{$key}}, split(/\s+/, $1);
                }
            } else {
                $key = undef;
            }
        }
    }
   
    # drop leading empty lines from the log message
    while (@$log && $log->[0] eq '') {
	shift @$log;
    }
    if (exists $ps->{summary} && @{$ps->{summary}}) {
	$ps->{summary} = join(' ', @{$ps->{summary}});
    }
    elsif (@$log == 0) {
	$ps->{summary} = 'empty commit message';
    } else {
	$ps->{summary} = $log->[0] . '...';
    }
    $ps->{message} = join("\n",@$log);
    
    # skip Arch control files, unescape pika-escaped files
    foreach my $k (keys %want_headers) {
        next unless (defined $ps->{$k});
        my @tmp = ();
        foreach my $t (@{$ps->{$k}}) {
           next unless length ($t);
           next if $t =~ m!\{arch\}/!;
           next if $t =~ m!\.arch-ids/!;
           # should we skip this?
           next if $t =~ m!\.arch-inventory$!;
           # tla cat-archive-log will give us filenames with spaces as file\(sp)name - why?
           # we can assume that any filename with \ indicates some pika escaping that we want to get rid of.
           if ($t =~ /\\/ ){
               $t = (safe_pipe_capture($TLA,'escape','--unescaped',$t))[0];
           }
           push @tmp, $t;
        }
        $ps->{$k} = \@tmp;
    }
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
	my @ancestors = `git-rev-list --topo-order $branchtip ^$mergebase`;
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
	my @needraw = `git-rev-list --topo-order $otherbranchtip ^$mergebase`;
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

# `tla logs -rf -d <dir> | head -n1` or `baz tree-id <dir>`
sub arch_tree_id {
    my $dir = shift;
    chomp( my $ret = (safe_pipe_capture($TLA,'logs','-rf','-d',$dir))[0] );
    return $ret;
}

sub archive_reachable {
    my $archive = shift;
    return 1 if $reachable{$archive};
    return 0 if $unreachable{$archive};
    
    if (system "$TLA whereis-archive $archive >/dev/null") {
        if ($opt_a && (system($TLA,'register-archive',
                      "http://mirrors.sourcecontrol.net/$archive") == 0)) {
            $reachable{$archive} = 1;
            return 1;
        }
        print STDERR "Archive is unreachable: $archive\n";
        $unreachable{$archive} = 1;
        return 0;
    } else {
        $reachable{$archive} = 1;
        return 1;
    }
}

