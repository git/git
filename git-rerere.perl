#!/usr/bin/perl
#
# REuse REcorded REsolve.  This tool records a conflicted automerge
# result and its hand resolution, and helps to resolve future
# automerge that results in the same conflict.
#
# To enable this feature, create a directory 'rr-cache' under your
# .git/ directory.

use Digest;
use File::Path;
use File::Copy;

my $git_dir = $::ENV{GIT_DIR} || ".git";
my $rr_dir = "$git_dir/rr-cache";
my $merge_rr = "$git_dir/rr-cache/MERGE_RR";

my %merge_rr = ();

sub read_rr {
	if (!-f $merge_rr) {
		%merge_rr = ();
		return;
	}
	my $in;
	local $/ = "\0";
	open $in, "<$merge_rr" or die "$!: $merge_rr";
	while (<$in>) {
		chomp;
		my ($name, $path) = /^([0-9a-f]{40})\t(.*)$/s;
		$merge_rr{$path} = $name;
	}
	close $in;
}

sub write_rr {
	my $out;
	open $out, ">$merge_rr" or die "$!: $merge_rr";
	for my $path (sort keys %merge_rr) {
		my $name = $merge_rr{$path};
		print $out "$name\t$path\0";
	}
	close $out;
}

sub compute_conflict_name {
	my ($path) = @_;
	my @side = ();
	my $in;
	open $in, "<$path"  or die "$!: $path";

	my $sha1 = Digest->new("SHA-1");
	my $hunk = 0;
	while (<$in>) {
		if (/^<<<<<<< .*/) {
			$hunk++;
			@side = ([], undef);
		}
		elsif (/^=======$/) {
			$side[1] = [];
		}
		elsif (/^>>>>>>> .*/) {
			my ($one, $two);
			$one = join('', @{$side[0]});
			$two = join('', @{$side[1]});
			if ($two le $one) {
				($one, $two) = ($two, $one);
			}
			$sha1->add($one);
			$sha1->add("\0");
			$sha1->add($two);
			$sha1->add("\0");
			@side = ();
		}
		elsif (@side == 0) {
			next;
		}
		elsif (defined $side[1]) {
			push @{$side[1]}, $_;
		}
		else {
			push @{$side[0]}, $_;
		}
	}
	close $in;
	return ($sha1->hexdigest, $hunk);
}

sub record_preimage {
	my ($path, $name) = @_;
	my @side = ();
	my ($in, $out);
	open $in, "<$path"  or die "$!: $path";
	open $out, ">$name" or die "$!: $name";

	while (<$in>) {
		if (/^<<<<<<< .*/) {
			@side = ([], undef);
		}
		elsif (/^=======$/) {
			$side[1] = [];
		}
		elsif (/^>>>>>>> .*/) {
			my ($one, $two);
			$one = join('', @{$side[0]});
			$two = join('', @{$side[1]});
			if ($two le $one) {
				($one, $two) = ($two, $one);
			}
			print $out "<<<<<<<\n";
			print $out $one;
			print $out "=======\n";
			print $out $two;
			print $out ">>>>>>>\n";
			@side = ();
		}
		elsif (@side == 0) {
			print $out $_;
		}
		elsif (defined $side[1]) {
			push @{$side[1]}, $_;
		}
		else {
			push @{$side[0]}, $_;
		}
	}
	close $out;
	close $in;
}

sub find_conflict {
	my $in;
	local $/ = "\0";
	my $pid = open($in, '-|');
	die "$!" unless defined $pid;
	if (!$pid) {
		exec(qw(git ls-files -z -u)) or die "$!: ls-files";
	}
	my %path = ();
	my @path = ();
	while (<$in>) {
		chomp;
		my ($mode, $sha1, $stage, $path) =
		    /^([0-7]+) ([0-9a-f]{40}) ([123])\t(.*)$/s;
		$path{$path} |= (1 << $stage);
	}
	close $in;
	while (my ($path, $status) = each %path) {
		if ($status == 14) { push @path, $path; }
	}
	return @path;
}

sub merge {
	my ($name, $path) = @_;
	record_preimage($path, "$rr_dir/$name/thisimage");
	unless (system('git', 'merge-file', map { "$rr_dir/$name/${_}image" }
		       qw(this pre post))) {
		my $in;
		open $in, "<$rr_dir/$name/thisimage" or
		    die "$!: $name/thisimage";
		my $out;
		open $out, ">$path" or die "$!: $path";
		while (<$in>) { print $out $_; }
		close $in;
		close $out;
		return 1;
	}
	return 0;
}

sub garbage_collect_rerere {
	# We should allow specifying these from the command line and
	# that is why the caller gives @ARGV to us, but I am lazy.

	my $cutoff_noresolve = 15; # two weeks
	my $cutoff_resolve = 60; # two months
	my @to_remove;
	while (<$rr_dir/*/preimage>) {
		my ($dir) = /^(.*)\/preimage$/;
		my $cutoff = ((-f "$dir/postimage")
			      ? $cutoff_resolve
			      : $cutoff_noresolve);
		my $age = -M "$_";
		if ($cutoff <= $age) {
			push @to_remove, $dir;
		}
	}
	if (@to_remove) {
		rmtree(\@to_remove);
	}
}

-d "$rr_dir" || exit(0);

read_rr();

if (@ARGV) {
	my $arg = shift @ARGV;
	if ($arg eq 'clear') {
		for my $path (keys %merge_rr) {
			my $name = $merge_rr{$path};
			if (-d "$rr_dir/$name" &&
			    ! -f "$rr_dir/$name/postimage") {
				rmtree(["$rr_dir/$name"]);
			}
		}
		unlink $merge_rr;
	}
	elsif ($arg eq 'status') {
		for my $path (keys %merge_rr) {
			print $path, "\n";
		}
	}
	elsif ($arg eq 'diff') {
		for my $path (keys %merge_rr) {
			my $name = $merge_rr{$path};
			system('diff', ((@ARGV == 0) ? ('-u') : @ARGV),
				'-L', "a/$path", '-L', "b/$path",
				"$rr_dir/$name/preimage", $path);
		}
	}
	elsif ($arg eq 'gc') {
		garbage_collect_rerere(@ARGV);
	}
	else {
		die "$0 unknown command: $arg\n";
	}
	exit 0;
}

my %conflict = map { $_ => 1 } find_conflict();

# MERGE_RR records paths with conflicts immediately after merge
# failed.  Some of the conflicted paths might have been hand resolved
# in the working tree since then, but the initial run would catch all
# and register their preimages.

for my $path (keys %conflict) {
	# This path has conflict.  If it is not recorded yet,
	# record the pre-image.
	if (!exists $merge_rr{$path}) {
		my ($name, $hunk) = compute_conflict_name($path);
		next unless ($hunk);
		$merge_rr{$path} = $name;
		if (! -d "$rr_dir/$name") {
			mkpath("$rr_dir/$name", 0, 0777);
			print STDERR "Recorded preimage for '$path'\n";
			record_preimage($path, "$rr_dir/$name/preimage");
		}
	}
}

# Now some of the paths that had conflicts earlier might have been
# hand resolved.  Others may be similar to a conflict already that
# was resolved before.

for my $path (keys %merge_rr) {
	my $name = $merge_rr{$path};

	# We could resolve this automatically if we have images.
	if (-f "$rr_dir/$name/preimage" &&
	    -f "$rr_dir/$name/postimage") {
		if (merge($name, $path)) {
			print STDERR "Resolved '$path' using previous resolution.\n";
			# Then we do not have to worry about this path
			# anymore.
			delete $merge_rr{$path};
			next;
		}
	}

	# Let's see if we have resolved it.
	(undef, my $hunk) = compute_conflict_name($path);
	next if ($hunk);

	print STDERR "Recorded resolution for '$path'.\n";
	copy($path, "$rr_dir/$name/postimage");
	# And we do not have to worry about this path anymore.
	delete $merge_rr{$path};
}

# Write out the rest.
write_rr();
