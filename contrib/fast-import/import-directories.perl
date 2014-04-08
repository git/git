#!/usr/bin/perl
#
# Copyright 2008-2009 Peter Krefting <peter@softwolves.pp.se>
#
# ------------------------------------------------------------------------
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
#
# ------------------------------------------------------------------------

=pod

=head1 NAME

import-directories - Import bits and pieces to Git.

=head1 SYNOPSIS

B<import-directories.perl> F<configfile> F<outputfile>

=head1 DESCRIPTION

Script to import arbitrary projects version controlled by the "copy the
source directory to a new location and edit it there"-version controlled
projects into version control. Handles projects with arbitrary branching
and version trees, taking a file describing the inputs and generating a
file compatible with the L<git-fast-import(1)> format.

=head1 CONFIGURATION FILE

=head2 Format

The configuration file is based on the standard I<.ini> format.

 ; Comments start with semi-colons
 [section]
 key=value

Please see below for information on how to escape special characters.

=head2 Global configuration

Global configuration is done in the B<[config]> section, which should be
the first section in the file. Configuration can be changed by
repeating configuration sections later on.

 [config]
 ; configure conversion of CRLFs. "convert" means that all CRLFs
 ; should be converted into LFs (suitable for the core.autocrlf
 ; setting set to true in Git). "none" means that all data is
 ; treated as binary.
 crlf=convert

=head2 Revision configuration

Each revision that is to be imported is described in three
sections. Revisions should be defined in topological order, so
that a revision's parent has always been defined when a new revision
is introduced. All the sections for one revision must be defined
before defining the next revision.

Each revision is assigned a unique numerical identifier. The
numbers do not need to be consecutive, nor monotonically
increasing.

For instance, if your configuration file contains only the two
revisions 4711 and 42, where 4711 is the initial commit, the
only requirement is that 4711 is completely defined before 42.

=pod

=head3 Revision description section

A section whose section name is just an integer gives meta-data
about the revision.

 [3]
 ; author sets the author of the revisions
 author=Peter Krefting <peter@softwolves.pp.se>
 ; branch sets the branch that the revision should be committed to
 branch=master
 ; parent describes the revision that is the parent of this commit
 ; (optional)
 parent=1
 ; merges describes a revision that is merged into this commit
 ; (optional; can be repeated)
 merges=2
 ; selects one file to take the timestamp from
 ; (optional; if unspecified, the most recent file from the .files
 ;  section is used)
 timestamp=3/source.c

=head3 Revision contents section

A section whose section name is an integer followed by B<.files>
describe all the files included in this revision. If a file that
was available previously is not included in this revision, it will
be removed.

If an on-disk revision is incomplete, you can point to files from
a previous revision. There are no restrictions on where the source
files are located, nor on their names.

 [3.files]
 ; the key is the path inside the repository, the value is the path
 ; as seen from the importer script.
 source.c=ver-3.00/source.c
 source.h=ver-2.99/source.h
 readme.txt=ver-3.00/introduction to the project.txt

File names are treated as byte strings (but please see below on
quoting rules), and should be stored in the configuration file in
the encoding that should be used in the generated repository.

=head3 Revision commit message section

A section whose section name is an integer followed by B<.message>
gives the commit message. This section is read verbatim, up until
the beginning of the next section. As such, a commit message may not
contain a line that begins with an opening square bracket ("[") and
ends with a closing square bracket ("]"), unless they are surrounded
by whitespace or other characters.

 [3.message]
 Implement foobar.
 ; trailing blank lines are ignored.

=cut

# Globals
use strict;
use warnings;
use integer;
my $crlfmode = 0;
my @revs;
my (%revmap, %message, %files, %author, %branch, %parent, %merges, %time, %timesource);
my $sectiontype = 0;
my $rev = 0;
my $mark = 1;

# Check command line
if ($#ARGV < 1 || $ARGV[0] =~ /^--?h/)
{
    exec('perldoc', $0);
    exit 1;
}

# Open configuration
my $config = $ARGV[0];
open CFG, '<', $config or die "Cannot open configuration file \"$config\": ";

# Open output
my $output = $ARGV[1];
open OUT, '>', $output or die "Cannot create output file \"$output\": ";
binmode OUT;

LINE: while (my $line = <CFG>)
{
	$line =~ s/\r?\n$//;
	next LINE if $sectiontype != 4 && $line eq '';
	next LINE if $line =~ /^;/;
	my $oldsectiontype = $sectiontype;
	my $oldrev = $rev;

	# Sections
	if ($line =~ m"^\[(config|(\d+)(|\.files|\.message))\]$")
	{
		if ($1 eq 'config')
		{
			$sectiontype = 1;
		}
		elsif ($3 eq '')
		{
			$sectiontype = 2;
			$rev = $2;
			# Create a new revision
			die "Duplicate rev: $line\n " if defined $revmap{$rev};
			print "Reading revision $rev\n";
			push @revs, $rev;
			$revmap{$rev} = $mark ++;
			$time{$revmap{$rev}} = 0;
		}
		elsif ($3 eq '.files')
		{
			$sectiontype = 3;
			$rev = $2;
			die "Revision mismatch: $line\n " unless $rev == $oldrev;
		}
		elsif ($3 eq '.message')
		{
			$sectiontype = 4;
			$rev = $2;
			die "Revision mismatch: $line\n " unless $rev == $oldrev;
		}
		else
		{
			die "Internal parse error: $line\n ";
		}
		next LINE;
	}

	# Parse data
	if ($sectiontype != 4)
	{
		# Key and value
		if ($line =~ m"^\s*([^\s].*=.*[^\s])\s*$")
		{
			my ($key, $value) = &parsekeyvaluepair($1);
			# Global configuration
			if (1 == $sectiontype)
			{
				if ($key eq 'crlf')
				{
					$crlfmode = 1, next LINE if $value eq 'convert';
					$crlfmode = 0, next LINE if $value eq 'none';
				}
				die "Unknown configuration option: $line\n ";
			}
			# Revision specification
			if (2 == $sectiontype)
			{
				my $current = $revmap{$rev};
				$author{$current} = $value, next LINE if $key eq 'author';
				$branch{$current} = $value, next LINE if $key eq 'branch';
				$parent{$current} = $value, next LINE if $key eq 'parent';
				$timesource{$current} = $value, next LINE if $key eq 'timestamp';
				push(@{$merges{$current}}, $value), next LINE if $key eq 'merges';
				die "Unknown revision option: $line\n ";
			}
			# Filespecs
			if (3 == $sectiontype)
			{
				# Add the file and create a marker
				die "File not found: $line\n " unless -f $value;
				my $current = $revmap{$rev};
				${$files{$current}}{$key} = $mark;
				my $time = &fileblob($value, $crlfmode, $mark ++);

				# Update revision timestamp if more recent than other
				# files seen, or if this is the file we have selected
				# to take the time stamp from using the "timestamp"
				# directive.
				if ((defined $timesource{$current} && $timesource{$current} eq $value)
				    || $time > $time{$current})
				{
					$time{$current} = $time;
				}
			}
		}
		else
		{
			die "Parse error: $line\n ";
		}
	}
	else
	{
		# Commit message
		my $current = $revmap{$rev};
		if (defined $message{$current})
		{
			$message{$current} .= "\n";
		}
		$message{$current} .= $line;
	}
}
close CFG;

# Start spewing out data for git-fast-import
foreach my $commit (@revs)
{
	# Progress
	print OUT "progress Creating revision $commit\n";

	# Create commit header
	my $mark = $revmap{$commit};

	# Branch and commit id
	print OUT "commit refs/heads/", $branch{$mark}, "\nmark :", $mark, "\n";

	# Author and timestamp
	die "No timestamp defined for $commit (no files?)\n" unless defined $time{$mark};
	print OUT "committer ", $author{$mark}, " ", $time{$mark}, " +0100\n";

	# Commit message
	die "No message defined for $commit\n" unless defined $message{$mark};
	my $message = $message{$mark};
	$message =~ s/\n$//; # Kill trailing empty line
	print OUT "data ", length($message), "\n", $message, "\n";

	# Parent and any merges
	print OUT "from :", $revmap{$parent{$mark}}, "\n" if defined $parent{$mark};
	if (defined $merges{$mark})
	{
		foreach my $merge (@{$merges{$mark}})
		{
			print OUT "merge :", $revmap{$merge}, "\n";
		}
	}

	# Output file marks
	print OUT "deleteall\n"; # start from scratch
	foreach my $file (sort keys %{$files{$mark}})
	{
		print OUT "M 644 :", ${$files{$mark}}{$file}, " $file\n";
	}
	print OUT "\n";
}

# Create one file blob
sub fileblob
{
	my ($filename, $crlfmode, $mark) = @_;

	# Import the file
	print OUT "progress Importing $filename\nblob\nmark :$mark\n";
	open FILE, '<', $filename or die "Cannot read $filename\n ";
	binmode FILE;
	my ($size, $mtime) = (stat(FILE))[7,9];
	my $file;
	read FILE, $file, $size;
	close FILE;
	$file =~ s/\r\n/\n/g if $crlfmode;
	print OUT "data ", length($file), "\n", $file, "\n";

	return $mtime;
}

# Parse a key=value pair
sub parsekeyvaluepair
{
=pod

=head2 Escaping special characters

Key and value strings may be enclosed in quotes, in which case
whitespace inside the quotes is preserved. Additionally, an equal
sign may be included in the key by preceding it with a backslash.
For example:

 "key1 "=value1
 key2=" value2"
 key\=3=value3
 key4=value=4
 "key5""=value5

Here the first key is "key1 " (note the trailing white-space) and the
second value is " value2" (note the leading white-space). The third
key contains an equal sign "key=3" and so does the fourth value, which
does not need to be escaped. The fifth key contains a trailing quote,
which does not need to be escaped since it is inside a surrounding
quote.

=cut
	my $pair = shift;

	# Separate key and value by the first non-quoted equal sign
	my ($key, $value);
	if ($pair =~ /^(.*[^\\])=(.*)$/)
	{
		($key, $value) = ($1, $2)
	}
	else
	{
		die "Parse error: $pair\n ";
	}

	# Unquote and unescape the key and value separately
	return (&unescape($key), &unescape($value));
}

# Unquote and unescape
sub unescape
{
	my $string = shift;

	# First remove enclosing quotes. Backslash before the trailing
	# quote leaves both.
	if ($string =~ /^"(.*[^\\])"$/)
	{
		$string = $1;
	}

	# Second remove any backslashes inside the unquoted string.
	# For later: Handle special sequences like \t ?
	$string =~ s/\\(.)/$1/g;

	return $string;
}

__END__

=pod

=head1 EXAMPLES

B<import-directories.perl> F<project.import>

=head1 AUTHOR

Copyright 2008-2009 Peter Krefting E<lt>peter@softwolves.pp.se>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation.

=cut
