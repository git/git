#!/usr/bin/perl
# Copyright 2006, Ryan Anderson <ryan@michonline.com>
#
# GPL v2 (See COPYING)
#
# This file is licensed under the GPL v2, or a later version
# at the discretion of Linus Torvalds.

use warnings;
use strict;
use Getopt::Long;
use POSIX qw(strftime gmtime);
use File::Basename qw(basename dirname);
use Git;

sub usage() {
	print STDERR "Usage: ${\basename $0} [-s] [-S revs-file] file [ revision ]
	-l, --long
			Show long rev (Defaults off)
	-t, --time
			Show raw timestamp (Defaults off)
	-r, --rename
			Follow renames (Defaults on).
	-S, --rev-file revs-file
			Use revs from revs-file instead of calling git-rev-list
	-h, --help
			This message.
";

	exit(1);
}

our ($help, $longrev, $rename, $rawtime, $starting_rev, $rev_file, $repo) = (0, 0, 1);

my $rc = GetOptions(	"long|l" => \$longrev,
			"time|t" => \$rawtime,
			"help|h" => \$help,
			"rename|r" => \$rename,
			"rev-file|S=s" => \$rev_file);
if (!$rc or $help or !@ARGV) {
	usage();
}

my $filename = shift @ARGV;
if (@ARGV) {
	$starting_rev = shift @ARGV;
}

my @stack = (
	{
		'rev' => defined $starting_rev ? $starting_rev : "HEAD",
		'filename' => $filename,
	},
);

$repo = Git->repository();

our @filelines = ();

if (defined $starting_rev) {
	@filelines = git_cat_file($starting_rev, $filename);
} else {
	open(F,"<",$filename)
		or die "Failed to open filename: $!";

	while(<F>) {
		chomp;
		push @filelines, $_;
	}
	close(F);

}

our %revs;
our @revqueue;
our $head;

my $revsprocessed = 0;
while (my $bound = pop @stack) {
	my @revisions = git_rev_list($bound->{'rev'}, $bound->{'filename'});
	foreach my $revinst (@revisions) {
		my ($rev, @parents) = @$revinst;
		$head ||= $rev;

		if (!defined($rev)) {
			$rev = "";
		}
		$revs{$rev}{'filename'} = $bound->{'filename'};
		if (scalar @parents > 0) {
			$revs{$rev}{'parents'} = \@parents;
			next;
		}

		if (!$rename) {
			next;
		}

		my $newbound = find_parent_renames($rev, $bound->{'filename'});
		if ( exists $newbound->{'filename'} && $newbound->{'filename'} ne $bound->{'filename'}) {
			push @stack, $newbound;
			$revs{$rev}{'parents'} = [$newbound->{'rev'}];
		}
	}
}
push @revqueue, $head;
init_claim( defined $starting_rev ? $head : 'dirty');
unless (defined $starting_rev) {
	my %ident;
	@ident{'author', 'author_email', 'author_date'} = $repo->ident('author');
	my $diff = $repo->command_output_pipe('diff', '-R', 'HEAD', '--', $filename);
	_git_diff_parse($diff, [$head], "dirty", %ident);
	$repo->command_close_pipe($diff);
}
handle_rev();


my $i = 0;
foreach my $l (@filelines) {
	my ($output, $rev, $committer, $date);
	if (ref $l eq 'ARRAY') {
		($output, $rev, $committer, $date) = @$l;
		if (!$longrev && length($rev) > 8) {
			$rev = substr($rev,0,8);
		}
	} else {
		$output = $l;
		($rev, $committer, $date) = ('unknown', 'unknown', 'unknown');
	}

	printf("%s\t(%10s\t%10s\t%d)%s\n", $rev, $committer,
		format_date($date), ++$i, $output);
}

sub init_claim {
	my ($rev) = @_;
	for (my $i = 0; $i < @filelines; $i++) {
		$filelines[$i] = [ $filelines[$i], '', '', '', 1];
			# line,
			# rev,
			# author,
			# date,
			# 1 <-- belongs to the original file.
	}
	$revs{$rev}{'lines'} = \@filelines;
}


sub handle_rev {
	my $revseen = 0;
	my %seen;
	while (my $rev = shift @revqueue) {
		next if $seen{$rev}++;

		my %revinfo = git_commit_info($rev);

		if (exists $revs{$rev}{parents} &&
		    scalar @{$revs{$rev}{parents}} != 0) {

			git_diff_parse($revs{$rev}{'parents'}, $rev, %revinfo);
			push @revqueue, @{$revs{$rev}{'parents'}};

		} else {
			# We must be at the initial rev here, so claim everything that is left.
			for (my $i = 0; $i < @{$revs{$rev}{lines}}; $i++) {
				if (ref ${$revs{$rev}{lines}}[$i] eq '' || ${$revs{$rev}{lines}}[$i][1] eq '') {
					claim_line($i, $rev, $revs{$rev}{lines}, %revinfo);
				}
			}
		}
	}
}


sub git_rev_list {
	my ($rev, $file) = @_;

	my $revlist;
	if ($rev_file) {
		open($revlist, '<' . $rev_file)
		    or die "Failed to open $rev_file : $!";
	} else {
		$revlist = $repo->command_output_pipe('rev-list', '--parents', '--remove-empty', $rev, '--', $file);
	}

	my @revs;
	while(my $line = <$revlist>) {
		chomp $line;
		my ($rev, @parents) = split /\s+/, $line;
		push @revs, [ $rev, @parents ];
	}
	$repo->command_close_pipe($revlist);

	printf("0 revs found for rev %s (%s)\n", $rev, $file) if (@revs == 0);
	return @revs;
}

sub find_parent_renames {
	my ($rev, $file) = @_;

	my $patch = $repo->command_output_pipe('diff-tree', '-M50', '-r', '--name-status', '-z', $rev);

	local $/ = "\0";
	my %bound;
	my $junk = <$patch>;
	while (my $change = <$patch>) {
		chomp $change;
		my $filename = <$patch>;
		if (!defined $filename) {
			next;
		}
		chomp $filename;

		if ($change =~ m/^[AMD]$/ ) {
			next;
		} elsif ($change =~ m/^R/ ) {
			my $oldfilename = $filename;
			$filename = <$patch>;
			chomp $filename;
			if ( $file eq $filename ) {
				my $parent = git_find_parent($rev, $oldfilename);
				@bound{'rev','filename'} = ($parent, $oldfilename);
				last;
			}
		}
	}
	$repo->command_close_pipe($patch);

	return \%bound;
}


sub git_find_parent {
	my ($rev, $filename) = @_;

	my $parentline = $repo->command_oneline('rev-list', '--remove-empty',
			'--parents', '--max-count=1', $rev, '--', $filename);
	my ($revfound, $parent) = split m/\s+/, $parentline;

	return $parent;
}

sub git_find_all_parents {
	my ($rev) = @_;

	my $parentline = $repo->command_oneline("rev-list","--remove-empty", "--parents","--max-count=1","$rev");
	my ($origrev, @parents) = split m/\s+/, $parentline;

	return @parents;
}

sub git_merge_base {
	my ($rev1, $rev2) = @_;

	my $base = $repo->command_oneline("merge-base", $rev1, $rev2);
	return $base;
}

# Construct a set of pseudo parents that are in the same order,
# and the same quantity as the real parents,
# but whose SHA1s are as similar to the logical parents
# as possible.
sub get_pseudo_parents {
	my ($all, $fake) = @_;

	my @all = @$all;
	my @fake = @$fake;

	my @pseudo;

	my %fake = map {$_ => 1} @fake;
	my %seenfake;

	my $fakeidx = 0;
	foreach my $p (@all) {
		if (exists $fake{$p}) {
			if ($fake[$fakeidx] ne $p) {
				die sprintf("parent mismatch: %s != %s\nall:%s\nfake:%s\n",
					    $fake[$fakeidx], $p,
					    join(", ", @all),
					    join(", ", @fake),
					   );
			}

			push @pseudo, $p;
			$fakeidx++;
			$seenfake{$p}++;

		} else {
			my $base = git_merge_base($fake[$fakeidx], $p);
			if ($base ne $fake[$fakeidx]) {
				die sprintf("Result of merge-base doesn't match fake: %s,%s != %s\n",
				       $fake[$fakeidx], $p, $base);
			}

			# The details of how we parse the diffs
			# mean that we cannot have a duplicate
			# revision in the list, so if we've already
			# seen the revision we would normally add, just use
			# the actual revision.
			if ($seenfake{$base}) {
				push @pseudo, $p;
			} else {
				push @pseudo, $base;
				$seenfake{$base}++;
			}
		}
	}

	return @pseudo;
}


# Get a diff between the current revision and a parent.
# Record the commit information that results.
sub git_diff_parse {
	my ($parents, $rev, %revinfo) = @_;

	my @pseudo_parents;
	my @command = ("diff-tree");
	my $revision_spec;

	if (scalar @$parents == 1) {

		$revision_spec = join("..", $parents->[0], $rev);
		@pseudo_parents = @$parents;
	} else {
		my @all_parents = git_find_all_parents($rev);

		if (@all_parents !=  @$parents) {
			@pseudo_parents = get_pseudo_parents(\@all_parents, $parents);
		} else {
			@pseudo_parents = @$parents;
		}

		$revision_spec = $rev;
		push @command, "-c";
	}

	my @filenames = ( $revs{$rev}{'filename'} );

	foreach my $parent (@$parents) {
		push @filenames, $revs{$parent}{'filename'};
	}

	push @command, "-p", "-M", $revision_spec, "--", @filenames;


	my $diff = $repo->command_output_pipe(@command);

	_git_diff_parse($diff, \@pseudo_parents, $rev, %revinfo);

	$repo->command_close_pipe($diff);
}

sub _git_diff_parse {
	my ($diff, $parents, $rev, %revinfo) = @_;

	my $ri = 0;

	my $slines = $revs{$rev}{'lines'};
	my (%plines, %pi);

	my $gotheader = 0;
	my ($remstart);
	my $parent_count = @$parents;

	my $diff_header_regexp = "^@";
	$diff_header_regexp .= "@" x @$parents;
	$diff_header_regexp .= ' -\d+,\d+' x @$parents;
	$diff_header_regexp .= ' \+(\d+),\d+';
	$diff_header_regexp .= " " . ("@" x @$parents);

	my %claim_regexps;
	my $allparentplus = '^' . '\\+' x @$parents . '(.*)$';

	{
		my $i = 0;
		foreach my $parent (@$parents) {

			$pi{$parent} = 0;
			my $r = '^' . '.' x @$parents . '(.*)$';
			my $p = $r;
			substr($p,$i+1, 1) = '\\+';

			my $m = $r;
			substr($m,$i+1, 1) = '-';

			$claim_regexps{$parent}{plus} = $p;
			$claim_regexps{$parent}{minus} = $m;

			$plines{$parent} = [];

			$i++;
		}
	}

	DIFF:
	while(<$diff>) {
		chomp;
		#printf("%d:%s:\n", $gotheader, $_);
		if (m/$diff_header_regexp/) {
			$remstart = $1 - 1;
			# (0-based arrays)

			$gotheader = 1;

			foreach my $parent (@$parents) {
				for (my $i = $ri; $i < $remstart; $i++) {
					$plines{$parent}[$pi{$parent}++] = $slines->[$i];
				}
			}
			$ri = $remstart;

			next DIFF;

		} elsif (!$gotheader) {
			# Skip over the leadin.
			next DIFF;
		}

		if (m/^\\/) {
			;
			# Skip \No newline at end of file.
			# But this can be internationalized, so only look
			# for an initial \

		} else {
			my %claims = ();
			my $negclaim = 0;
			my $allclaimed = 0;
			my $line;

			if (m/$allparentplus/) {
				claim_line($ri, $rev, $slines, %revinfo);
				$allclaimed = 1;

			}

			PARENT:
			foreach my $parent (keys %claim_regexps) {
				my $m = $claim_regexps{$parent}{minus};
				my $p = $claim_regexps{$parent}{plus};

				if (m/$m/) {
					$line = $1;
					$plines{$parent}[$pi{$parent}++] = [ $line, '', '', '', 0 ];
					$negclaim++;

				} elsif (m/$p/) {
					$line = $1;
					if (get_line($slines, $ri) eq $line) {
						# Found a match, claim
						$claims{$parent}++;

					} else {
						die sprintf("Sync error: %d\n|%s\n|%s\n%s => %s\n",
								$ri, $line,
								get_line($slines, $ri),
								$rev, $parent);
					}
				}
			}

			if (%claims) {
				foreach my $parent (@$parents) {
					next if $claims{$parent} || $allclaimed;
					$plines{$parent}[$pi{$parent}++] = $slines->[$ri];
					    #[ $line, '', '', '', 0 ];
				}
				$ri++;

			} elsif ($negclaim) {
				next DIFF;

			} else {
				if (substr($_,scalar @$parents) ne get_line($slines,$ri) ) {
				        foreach my $parent (@$parents) {
						printf("parent %s is on line %d\n", $parent, $pi{$parent});
					}

					my @context;
					for (my $i = -2; $i < 2; $i++) {
						push @context, get_line($slines, $ri + $i);
					}
					my $context = join("\n", @context);

					my $justline = substr($_, scalar @$parents);
					die sprintf("Line %d, does not match:\n|%s|\n|%s|\n%s\n",
						    $ri,
						    $justline,
						    $context);
				}
				foreach my $parent (@$parents) {
					$plines{$parent}[$pi{$parent}++] = $slines->[$ri];
				}
				$ri++;
			}
		}
	}

	for (my $i = $ri; $i < @{$slines} ; $i++) {
		foreach my $parent (@$parents) {
			push @{$plines{$parent}}, $slines->[$ri];
		}
		$ri++;
	}

	foreach my $parent (@$parents) {
		$revs{$parent}{lines} = $plines{$parent};
	}

	return;
}

sub get_line {
	my ($lines, $index) = @_;

	return ref $lines->[$index] ne '' ? $lines->[$index][0] : $lines->[$index];
}

sub git_cat_file {
	my ($rev, $filename) = @_;
	return () unless defined $rev && defined $filename;

	my $blob = git_ls_tree($rev, $filename);
	die "Failed to find a blob for $filename in rev $rev\n" if !defined $blob;

	my @lines = split(/\n/, $repo->get_object('blob', $blob));
	pop @lines unless $lines[$#lines]; # Trailing newline
	return @lines;
}

sub git_ls_tree {
	my ($rev, $filename) = @_;

	my $lstree = $repo->command_output_pipe('ls-tree', $rev, $filename);
	my ($mode, $type, $blob, $tfilename);
	while(<$lstree>) {
		chomp;
		($mode, $type, $blob, $tfilename) = split(/\s+/, $_, 4);
		last if ($tfilename eq $filename);
	}
	$repo->command_close_pipe($lstree);

	return $blob if ($tfilename eq $filename);
	die "git-ls-tree failed to find blob for $filename";
}



sub claim_line {
	my ($floffset, $rev, $lines, %revinfo) = @_;
	my $oline = get_line($lines, $floffset);
	@{$lines->[$floffset]} = ( $oline, $rev,
		$revinfo{'author'}, $revinfo{'author_date'} );
	#printf("Claiming line %d with rev %s: '%s'\n",
	#		$floffset, $rev, $oline) if 1;
}

sub git_commit_info {
	my ($rev) = @_;
	my $commit = $repo->get_object('commit', $rev);

	my %info;
	while ($commit =~ /(.*?)\n/g) {
		my $line = $1;
		if ($line =~ s/^author //) {
			@info{'author', 'author_email', 'author_date'} = $repo->ident($line);
		} elsif ($line =~ s/^committer//) {
			@info{'committer', 'committer_email', 'committer_date'} = $repo->ident($line);
		}
	}

	return %info;
}

sub format_date {
	if ($rawtime) {
		return $_[0];
	}
	my ($timestamp, $timezone) = split(' ', $_[0]);
	my $minutes = abs($timezone);
	$minutes = int($minutes / 100) * 60 + ($minutes % 100);
	if ($timezone < 0) {
	    $minutes = -$minutes;
	}
	my $t = $timestamp + $minutes * 60;
	return strftime("%Y-%m-%d %H:%M:%S " . $timezone, gmtime($t));
}
