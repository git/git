#!/usr/bin/perl

## tar archive frontend for but-fast-import
##
## For example:
##
##  mkdir project; cd project; but init
##  perl import-tars.perl *.tar.bz2
##  but whatchanged import-tars
##
## Use --metainfo to specify the extension for a meta data file, where
## import-tars can read the cummit message and optionally author and
## cummitter information.
##
##  echo 'This is the cummit message' > myfile.tar.bz2.msg
##  perl import-tars.perl --metainfo=msg myfile.tar.bz2

use strict;
use Getopt::Long;

my $metaext = '';

die "usage: import-tars [--metainfo=extension] *.tar.{gz,bz2,lzma,xz,Z}\n"
	unless GetOptions('metainfo=s' => \$metaext) && @ARGV;

my $branch_name = 'import-tars';
my $branch_ref = "refs/heads/$branch_name";
my $author_name = $ENV{'BUT_AUTHOR_NAME'} || 'T Ar Creator';
my $author_email = $ENV{'BUT_AUTHOR_EMAIL'} || 'tar@example.com';
my $cummitter_name = $ENV{'BUT_CUMMITTER_NAME'} || `but config --get user.name`;
my $cummitter_email = $ENV{'BUT_CUMMITTER_EMAIL'} || `but config --get user.email`;

chomp($cummitter_name, $cummitter_email);

open(FI, '|-', 'but', 'fast-import', '--quiet')
	or die "Unable to start but fast-import: $!\n";
foreach my $tar_file (@ARGV)
{
	my $cummit_time = time;
	$tar_file =~ m,([^/]+)$,;
	my $tar_name = $1;

	if ($tar_name =~ s/\.(tar\.gz|tgz)$//) {
		open(I, '-|', 'gunzip', '-c', $tar_file)
			or die "Unable to gunzip -c $tar_file: $!\n";
	} elsif ($tar_name =~ s/\.(tar\.bz2|tbz2)$//) {
		open(I, '-|', 'bunzip2', '-c', $tar_file)
			or die "Unable to bunzip2 -c $tar_file: $!\n";
	} elsif ($tar_name =~ s/\.tar\.Z$//) {
		open(I, '-|', 'uncompress', '-c', $tar_file)
			or die "Unable to uncompress -c $tar_file: $!\n";
	} elsif ($tar_name =~ s/\.(tar\.(lzma|xz)|(tlz|txz))$//) {
		open(I, '-|', 'xz', '-dc', $tar_file)
			or die "Unable to xz -dc $tar_file: $!\n";
	} elsif ($tar_name =~ s/\.tar$//) {
		open(I, $tar_file) or die "Unable to open $tar_file: $!\n";
	} else {
		die "Unrecognized compression format: $tar_file\n";
	}

	my $author_time = 0;
	my $next_mark = 1;
	my $have_top_dir = 1;
	my ($top_dir, %files);

	my $next_path = '';

	while (read(I, $_, 512) == 512) {
		my ($name, $mode, $uid, $gid, $size, $mtime,
			$chksum, $typeflag, $linkname, $magic,
			$version, $uname, $gname, $devmajor, $devminor,
			$prefix) = unpack 'Z100 Z8 Z8 Z8 Z12 Z12
			Z8 Z1 Z100 Z6
			Z2 Z32 Z32 Z8 Z8 Z*', $_;

		unless ($next_path eq '') {
			# Recover name from previous extended header
			$name = $next_path;
			$next_path = '';
		}

		last unless length($name);
		if ($name eq '././@LongLink') {
			# GNU tar extension
			if (read(I, $_, 512) != 512) {
				die ('Short archive');
			}
			$name = unpack 'Z257', $_;
			next unless $name;

			my $dummy;
			if (read(I, $_, 512) != 512) {
				die ('Short archive');
			}
			($dummy, $mode, $uid, $gid, $size, $mtime,
			$chksum, $typeflag, $linkname, $magic,
			$version, $uname, $gname, $devmajor, $devminor,
			$prefix) = unpack 'Z100 Z8 Z8 Z8 Z12 Z12
			Z8 Z1 Z100 Z6
			Z2 Z32 Z32 Z8 Z8 Z*', $_;
		}
		$mode = oct $mode;
		$size = oct $size;
		$mtime = oct $mtime;
		next if $typeflag == 5; # directory

		if ($typeflag eq 'x') { # extended header
			# If extended header, check for path
			my $pax_header = '';
			while ($size > 0 && read(I, $_, 512) == 512) {
				$pax_header = $pax_header . substr($_, 0, $size);
				$size -= 512;
			}

			my @lines = split /\n/, $pax_header;
			foreach my $line (@lines) {
				my ($len, $entry) = split / /, $line;
				my ($key, $value) = split /=/, $entry;
				if ($key eq 'path') {
					$next_path = $value;
				}
			}
			next;
		} elsif ($name =~ m{/\z}) { # directory
			next;
		} elsif ($typeflag != 1) { # handle hard links later
			print FI "blob\n", "mark :$next_mark\n";
			if ($typeflag == 2) { # symbolic link
				print FI "data ", length($linkname), "\n",
					$linkname;
				$mode = 0120000;
			} else {
				print FI "data $size\n";
				while ($size > 0 && read(I, $_, 512) == 512) {
					print FI substr($_, 0, $size);
					$size -= 512;
				}
			}
			print FI "\n";
		}

		next if ($typeflag eq 'g'); # ignore global header

		my $path;
		if ($prefix) {
			$path = "$prefix/$name";
		} else {
			$path = "$name";
		}

		if ($typeflag == 1) { # hard link
			$linkname = "$prefix/$linkname" if $prefix;
			$files{$path} = [ $files{$linkname}->[0], $mode ];
		} else {
			$files{$path} = [$next_mark++, $mode];
		}

		$author_time = $mtime if $mtime > $author_time;
		$path =~ m,^([^/]+)/,;
		$top_dir = $1 unless $top_dir;
		$have_top_dir = 0 if $top_dir ne $1;
	}

	my $cummit_msg = "Imported from $tar_file.";
	my $this_cummitter_name = $cummitter_name;
	my $this_cummitter_email = $cummitter_email;
	my $this_author_name = $author_name;
	my $this_author_email = $author_email;
	if ($metaext ne '') {
		# Optionally read a cummit message from <filename.tar>.msg
		# Add a line on the form "cummitter: name <e-mail>" to override
		# the cummitter and "Author: name <e-mail>" to override the
		# author for this tar ball.
		if (open MSG, '<', "${tar_file}.${metaext}") {
			my $header_done = 0;
			$cummit_msg = '';
			while (<MSG>) {
				if (!$header_done && /^cummitter:\s+([^<>]*)\s+<(.*)>\s*$/i) {
					$this_cummitter_name = $1;
					$this_cummitter_email = $2;
				} elsif (!$header_done && /^Author:\s+([^<>]*)\s+<(.*)>\s*$/i) {
					$this_author_name = $1;
					$this_author_email = $2;
				} elsif (!$header_done && /^$/) { # empty line ends header.
					$header_done = 1;
				} else {
					$cummit_msg .= $_;
					$header_done = 1;
				}
			}
			close MSG;
		}
	}

	print FI <<EOF;
cummit $branch_ref
author $this_author_name <$this_author_email> $author_time +0000
cummitter $this_cummitter_name <$this_cummitter_email> $cummit_time +0000
data <<END_OF_CUMMIT_MESSAGE
$cummit_msg
END_OF_CUMMIT_MESSAGE

deleteall
EOF

	foreach my $path (keys %files)
	{
		my ($mark, $mode) = @{$files{$path}};
		$path =~ s,^([^/]+)/,, if $have_top_dir;
		$mode = $mode & 0111 ? 0755 : 0644 unless $mode == 0120000;
		printf FI "M %o :%i %s\n", $mode, $mark, $path;
	}
	print FI "\n";

	print FI <<EOF;
tag $tar_name
from $branch_ref
tagger $author_name <$author_email> $author_time +0000
data <<END_OF_TAG_MESSAGE
Package $tar_name
END_OF_TAG_MESSAGE

EOF

	close I;
}
close FI;
