#!/usr/bin/perl -w

use strict;

sub run_cmd_pipe {
	if ($^O eq 'MSWin32') {
		my @invalid = grep {m/[":*]/} @_;
		die "$^O does not support: @invalid\n" if @invalid;
		my @args = map { m/ /o ? "\"$_\"": $_ } @_;
		return qx{@args};
	} else {
		my $fh = undef;
		open($fh, '-|', @_) or die;
		return <$fh>;
	}
}

my ($GIT_DIR) = run_cmd_pipe(qw(git rev-parse --git-dir));

if (!defined $GIT_DIR) {
	exit(1); # rev-parse would have already said "not a git repo"
}
chomp($GIT_DIR);

sub refresh {
	my $fh;
	open $fh, 'git update-index --refresh |'
	    or die;
	while (<$fh>) {
		;# ignore 'needs update'
	}
	close $fh;
}

sub list_untracked {
	map {
		chomp $_;
		$_;
	}
	run_cmd_pipe(qw(git ls-files --others --exclude-standard --), @_);
}

my $status_fmt = '%12s %12s %s';
my $status_head = sprintf($status_fmt, 'staged', 'unstaged', 'path');

# Returns list of hashes, contents of each of which are:
# PRINT:	print message
# VALUE:	pathname
# BINARY:	is a binary path
# INDEX:	is index different from HEAD?
# FILE:		is file different from index?
# INDEX_ADDDEL:	is it add/delete between HEAD and index?
# FILE_ADDDEL:	is it add/delete between index and file?

sub list_modified {
	my ($only) = @_;
	my (%data, @return);
	my ($add, $del, $adddel, $file);

	for (run_cmd_pipe(qw(git diff-index --cached
			     --numstat --summary HEAD))) {
		if (($add, $del, $file) =
		    /^([-\d]+)	([-\d]+)	(.*)/) {
			my ($change, $bin);
			if ($add eq '-' && $del eq '-') {
				$change = 'binary';
				$bin = 1;
			}
			else {
				$change = "+$add/-$del";
			}
			$data{$file} = {
				INDEX => $change,
				BINARY => $bin,
				FILE => 'nothing',
			}
		}
		elsif (($adddel, $file) =
		       /^ (create|delete) mode [0-7]+ (.*)$/) {
			$data{$file}{INDEX_ADDDEL} = $adddel;
		}
	}

	for (run_cmd_pipe(qw(git diff-files --numstat --summary))) {
		if (($add, $del, $file) =
		    /^([-\d]+)	([-\d]+)	(.*)/) {
			if (!exists $data{$file}) {
				$data{$file} = +{
					INDEX => 'unchanged',
					BINARY => 0,
				};
			}
			my ($change, $bin);
			if ($add eq '-' && $del eq '-') {
				$change = 'binary';
				$bin = 1;
			}
			else {
				$change = "+$add/-$del";
			}
			$data{$file}{FILE} = $change;
			if ($bin) {
				$data{$file}{BINARY} = 1;
			}
		}
		elsif (($adddel, $file) =
		       /^ (create|delete) mode [0-7]+ (.*)$/) {
			$data{$file}{FILE_ADDDEL} = $adddel;
		}
	}

	for (sort keys %data) {
		my $it = $data{$_};

		if ($only) {
			if ($only eq 'index-only') {
				next if ($it->{INDEX} eq 'unchanged');
			}
			if ($only eq 'file-only') {
				next if ($it->{FILE} eq 'nothing');
			}
		}
		push @return, +{
			VALUE => $_,
			PRINT => (sprintf $status_fmt,
				  $it->{INDEX}, $it->{FILE}, $_),
			%$it,
		};
	}
	return @return;
}

sub find_unique {
	my ($string, @stuff) = @_;
	my $found = undef;
	for (my $i = 0; $i < @stuff; $i++) {
		my $it = $stuff[$i];
		my $hit = undef;
		if (ref $it) {
			if ((ref $it) eq 'ARRAY') {
				$it = $it->[0];
			}
			else {
				$it = $it->{VALUE};
			}
		}
		eval {
			if ($it =~ /^$string/) {
				$hit = 1;
			};
		};
		if (defined $hit && defined $found) {
			return undef;
		}
		if ($hit) {
			$found = $i + 1;
		}
	}
	return $found;
}

sub list_and_choose {
	my ($opts, @stuff) = @_;
	my (@chosen, @return);
	my $i;

      TOPLOOP:
	while (1) {
		my $last_lf = 0;

		if ($opts->{HEADER}) {
			if (!$opts->{LIST_FLAT}) {
				print "     ";
			}
			print "$opts->{HEADER}\n";
		}
		for ($i = 0; $i < @stuff; $i++) {
			my $chosen = $chosen[$i] ? '*' : ' ';
			my $print = $stuff[$i];
			if (ref $print) {
				if ((ref $print) eq 'ARRAY') {
					$print = $print->[0];
				}
				else {
					$print = $print->{PRINT};
				}
			}
			printf("%s%2d: %s", $chosen, $i+1, $print);
			if (($opts->{LIST_FLAT}) &&
			    (($i + 1) % ($opts->{LIST_FLAT}))) {
				print "\t";
				$last_lf = 0;
			}
			else {
				print "\n";
				$last_lf = 1;
			}
		}
		if (!$last_lf) {
			print "\n";
		}

		return if ($opts->{LIST_ONLY});

		print $opts->{PROMPT};
		if ($opts->{SINGLETON}) {
			print "> ";
		}
		else {
			print ">> ";
		}
		my $line = <STDIN>;
		if (!$line) {
			print "\n";
			$opts->{ON_EOF}->() if $opts->{ON_EOF};
			last;
		}
		chomp $line;
		last if $line eq '';
		for my $choice (split(/[\s,]+/, $line)) {
			my $choose = 1;
			my ($bottom, $top);

			# Input that begins with '-'; unchoose
			if ($choice =~ s/^-//) {
				$choose = 0;
			}
			# A range can be specified like 5-7
			if ($choice =~ /^(\d+)-(\d+)$/) {
				($bottom, $top) = ($1, $2);
			}
			elsif ($choice =~ /^\d+$/) {
				$bottom = $top = $choice;
			}
			elsif ($choice eq '*') {
				$bottom = 1;
				$top = 1 + @stuff;
			}
			else {
				$bottom = $top = find_unique($choice, @stuff);
				if (!defined $bottom) {
					print "Huh ($choice)?\n";
					next TOPLOOP;
				}
			}
			if ($opts->{SINGLETON} && $bottom != $top) {
				print "Huh ($choice)?\n";
				next TOPLOOP;
			}
			for ($i = $bottom-1; $i <= $top-1; $i++) {
				next if (@stuff <= $i || $i < 0);
				$chosen[$i] = $choose;
			}
		}
		last if ($opts->{IMMEDIATE});
	}
	for ($i = 0; $i < @stuff; $i++) {
		if ($chosen[$i]) {
			push @return, $stuff[$i];
		}
	}
	return @return;
}

sub status_cmd {
	list_and_choose({ LIST_ONLY => 1, HEADER => $status_head },
			list_modified());
	print "\n";
}

sub say_n_paths {
	my $did = shift @_;
	my $cnt = scalar @_;
	print "$did ";
	if (1 < $cnt) {
		print "$cnt paths\n";
	}
	else {
		print "one path\n";
	}
}

sub update_cmd {
	my @mods = list_modified('file-only');
	return if (!@mods);

	my @update = list_and_choose({ PROMPT => 'Update',
				       HEADER => $status_head, },
				     @mods);
	if (@update) {
		system(qw(git update-index --add --remove --),
		       map { $_->{VALUE} } @update);
		say_n_paths('updated', @update);
	}
	print "\n";
}

sub revert_cmd {
	my @update = list_and_choose({ PROMPT => 'Revert',
				       HEADER => $status_head, },
				     list_modified());
	if (@update) {
		my @lines = run_cmd_pipe(qw(git ls-tree HEAD --),
					 map { $_->{VALUE} } @update);
		my $fh;
		open $fh, '| git update-index --index-info'
		    or die;
		for (@lines) {
			print $fh $_;
		}
		close($fh);
		for (@update) {
			if ($_->{INDEX_ADDDEL} &&
			    $_->{INDEX_ADDDEL} eq 'create') {
				system(qw(git update-index --force-remove --),
				       $_->{VALUE});
				print "note: $_->{VALUE} is untracked now.\n";
			}
		}
		refresh();
		say_n_paths('reverted', @update);
	}
	print "\n";
}

sub add_untracked_cmd {
	my @add = list_and_choose({ PROMPT => 'Add untracked' },
				  list_untracked());
	if (@add) {
		system(qw(git update-index --add --), @add);
		say_n_paths('added', @add);
	}
	print "\n";
}

sub parse_diff {
	my ($path) = @_;
	my @diff = run_cmd_pipe(qw(git diff-files -p --), $path);
	my (@hunk) = { TEXT => [] };

	for (@diff) {
		if (/^@@ /) {
			push @hunk, { TEXT => [] };
		}
		push @{$hunk[-1]{TEXT}}, $_;
	}
	return @hunk;
}

sub hunk_splittable {
	my ($text) = @_;

	my @s = split_hunk($text);
	return (1 < @s);
}

sub parse_hunk_header {
	my ($line) = @_;
	my ($o_ofs, $o_cnt, $n_ofs, $n_cnt) =
	    $line =~ /^@@ -(\d+)(?:,(\d+))? \+(\d+)(?:,(\d+))? @@/;
	$o_cnt = 1 unless defined $o_cnt;
	$n_cnt = 1 unless defined $n_cnt;
	return ($o_ofs, $o_cnt, $n_ofs, $n_cnt);
}

sub split_hunk {
	my ($text) = @_;
	my @split = ();

	# If there are context lines in the middle of a hunk,
	# it can be split, but we would need to take care of
	# overlaps later.

	my ($o_ofs, undef, $n_ofs) = parse_hunk_header($text->[0]);
	my $hunk_start = 1;

      OUTER:
	while (1) {
		my $next_hunk_start = undef;
		my $i = $hunk_start - 1;
		my $this = +{
			TEXT => [],
			OLD => $o_ofs,
			NEW => $n_ofs,
			OCNT => 0,
			NCNT => 0,
			ADDDEL => 0,
			POSTCTX => 0,
		};

		while (++$i < @$text) {
			my $line = $text->[$i];
			if ($line =~ /^ /) {
				if ($this->{ADDDEL} &&
				    !defined $next_hunk_start) {
					# We have seen leading context and
					# adds/dels and then here is another
					# context, which is trailing for this
					# split hunk and leading for the next
					# one.
					$next_hunk_start = $i;
				}
				push @{$this->{TEXT}}, $line;
				$this->{OCNT}++;
				$this->{NCNT}++;
				if (defined $next_hunk_start) {
					$this->{POSTCTX}++;
				}
				next;
			}

			# add/del
			if (defined $next_hunk_start) {
				# We are done with the current hunk and
				# this is the first real change for the
				# next split one.
				$hunk_start = $next_hunk_start;
				$o_ofs = $this->{OLD} + $this->{OCNT};
				$n_ofs = $this->{NEW} + $this->{NCNT};
				$o_ofs -= $this->{POSTCTX};
				$n_ofs -= $this->{POSTCTX};
				push @split, $this;
				redo OUTER;
			}
			push @{$this->{TEXT}}, $line;
			$this->{ADDDEL}++;
			if ($line =~ /^-/) {
				$this->{OCNT}++;
			}
			else {
				$this->{NCNT}++;
			}
		}

		push @split, $this;
		last;
	}

	for my $hunk (@split) {
		$o_ofs = $hunk->{OLD};
		$n_ofs = $hunk->{NEW};
		my $o_cnt = $hunk->{OCNT};
		my $n_cnt = $hunk->{NCNT};

		my $head = ("@@ -$o_ofs" .
			    (($o_cnt != 1) ? ",$o_cnt" : '') .
			    " +$n_ofs" .
			    (($n_cnt != 1) ? ",$n_cnt" : '') .
			    " @@\n");
		unshift @{$hunk->{TEXT}}, $head;
	}
	return map { $_->{TEXT} } @split;
}

sub find_last_o_ctx {
	my ($it) = @_;
	my $text = $it->{TEXT};
	my ($o_ofs, $o_cnt) = parse_hunk_header($text->[0]);
	my $i = @{$text};
	my $last_o_ctx = $o_ofs + $o_cnt;
	while (0 < --$i) {
		my $line = $text->[$i];
		if ($line =~ /^ /) {
			$last_o_ctx--;
			next;
		}
		last;
	}
	return $last_o_ctx;
}

sub merge_hunk {
	my ($prev, $this) = @_;
	my ($o0_ofs, $o0_cnt, $n0_ofs, $n0_cnt) =
	    parse_hunk_header($prev->{TEXT}[0]);
	my ($o1_ofs, $o1_cnt, $n1_ofs, $n1_cnt) =
	    parse_hunk_header($this->{TEXT}[0]);

	my (@line, $i, $ofs, $o_cnt, $n_cnt);
	$ofs = $o0_ofs;
	$o_cnt = $n_cnt = 0;
	for ($i = 1; $i < @{$prev->{TEXT}}; $i++) {
		my $line = $prev->{TEXT}[$i];
		if ($line =~ /^\+/) {
			$n_cnt++;
			push @line, $line;
			next;
		}

		last if ($o1_ofs <= $ofs);

		$o_cnt++;
		$ofs++;
		if ($line =~ /^ /) {
			$n_cnt++;
		}
		push @line, $line;
	}

	for ($i = 1; $i < @{$this->{TEXT}}; $i++) {
		my $line = $this->{TEXT}[$i];
		if ($line =~ /^\+/) {
			$n_cnt++;
			push @line, $line;
			next;
		}
		$ofs++;
		$o_cnt++;
		if ($line =~ /^ /) {
			$n_cnt++;
		}
		push @line, $line;
	}
	my $head = ("@@ -$o0_ofs" .
		    (($o_cnt != 1) ? ",$o_cnt" : '') .
		    " +$n0_ofs" .
		    (($n_cnt != 1) ? ",$n_cnt" : '') .
		    " @@\n");
	@{$prev->{TEXT}} = ($head, @line);
}

sub coalesce_overlapping_hunks {
	my (@in) = @_;
	my @out = ();

	my ($last_o_ctx);

	for (grep { $_->{USE} } @in) {
		my $text = $_->{TEXT};
		my ($o_ofs) = parse_hunk_header($text->[0]);
		if (defined $last_o_ctx &&
		    $o_ofs <= $last_o_ctx) {
			merge_hunk($out[-1], $_);
		}
		else {
			push @out, $_;
		}
		$last_o_ctx = find_last_o_ctx($out[-1]);
	}
	return @out;
}

sub help_patch_cmd {
	print <<\EOF ;
y - stage this hunk
n - do not stage this hunk
a - stage this and all the remaining hunks
d - do not stage this hunk nor any of the remaining hunks
j - leave this hunk undecided, see next undecided hunk
J - leave this hunk undecided, see next hunk
k - leave this hunk undecided, see previous undecided hunk
K - leave this hunk undecided, see previous hunk
s - split the current hunk into smaller hunks
EOF
}

sub patch_update_cmd {
	my @mods = list_modified('file-only');
	@mods = grep { !($_->{BINARY}) } @mods;
	return if (!@mods);

	my ($it) = list_and_choose({ PROMPT => 'Patch update',
				     SINGLETON => 1,
				     IMMEDIATE => 1,
				     HEADER => $status_head, },
				   @mods);
	return if (!$it);

	my ($ix, $num);
	my $path = $it->{VALUE};
	my ($head, @hunk) = parse_diff($path);
	for (@{$head->{TEXT}}) {
		print;
	}
	$num = scalar @hunk;
	$ix = 0;

	while (1) {
		my ($prev, $next, $other, $undecided, $i);
		$other = '';

		if ($num <= $ix) {
			$ix = 0;
		}
		for ($i = 0; $i < $ix; $i++) {
			if (!defined $hunk[$i]{USE}) {
				$prev = 1;
				$other .= '/k';
				last;
			}
		}
		if ($ix) {
			$other .= '/K';
		}
		for ($i = $ix + 1; $i < $num; $i++) {
			if (!defined $hunk[$i]{USE}) {
				$next = 1;
				$other .= '/j';
				last;
			}
		}
		if ($ix < $num - 1) {
			$other .= '/J';
		}
		for ($i = 0; $i < $num; $i++) {
			if (!defined $hunk[$i]{USE}) {
				$undecided = 1;
				last;
			}
		}
		last if (!$undecided);

		if (hunk_splittable($hunk[$ix]{TEXT})) {
			$other .= '/s';
		}
		for (@{$hunk[$ix]{TEXT}}) {
			print;
		}
		print "Stage this hunk [y/n/a/d$other/?]? ";
		my $line = <STDIN>;
		if ($line) {
			if ($line =~ /^y/i) {
				$hunk[$ix]{USE} = 1;
			}
			elsif ($line =~ /^n/i) {
				$hunk[$ix]{USE} = 0;
			}
			elsif ($line =~ /^a/i) {
				while ($ix < $num) {
					if (!defined $hunk[$ix]{USE}) {
						$hunk[$ix]{USE} = 1;
					}
					$ix++;
				}
				next;
			}
			elsif ($line =~ /^d/i) {
				while ($ix < $num) {
					if (!defined $hunk[$ix]{USE}) {
						$hunk[$ix]{USE} = 0;
					}
					$ix++;
				}
				next;
			}
			elsif ($other =~ /K/ && $line =~ /^K/) {
				$ix--;
				next;
			}
			elsif ($other =~ /J/ && $line =~ /^J/) {
				$ix++;
				next;
			}
			elsif ($other =~ /k/ && $line =~ /^k/) {
				while (1) {
					$ix--;
					last if (!$ix ||
						 !defined $hunk[$ix]{USE});
				}
				next;
			}
			elsif ($other =~ /j/ && $line =~ /^j/) {
				while (1) {
					$ix++;
					last if ($ix >= $num ||
						 !defined $hunk[$ix]{USE});
				}
				next;
			}
			elsif ($other =~ /s/ && $line =~ /^s/) {
				my @split = split_hunk($hunk[$ix]{TEXT});
				if (1 < @split) {
					print "Split into ",
					scalar(@split), " hunks.\n";
				}
				splice(@hunk, $ix, 1,
				       map { +{ TEXT => $_, USE => undef } }
				       @split);
				$num = scalar @hunk;
				next;
			}
			else {
				help_patch_cmd($other);
				next;
			}
			# soft increment
			while (1) {
				$ix++;
				last if ($ix >= $num ||
					 !defined $hunk[$ix]{USE});
			}
		}
	}

	@hunk = coalesce_overlapping_hunks(@hunk);

	my $n_lofs = 0;
	my @result = ();
	for (@hunk) {
		my $text = $_->{TEXT};
		my ($o_ofs, $o_cnt, $n_ofs, $n_cnt) =
		    parse_hunk_header($text->[0]);

		if (!$_->{USE}) {
			# We would have added ($n_cnt - $o_cnt) lines
			# to the postimage if we were to use this hunk,
			# but we didn't.  So the line number that the next
			# hunk starts at would be shifted by that much.
			$n_lofs -= ($n_cnt - $o_cnt);
			next;
		}
		else {
			if ($n_lofs) {
				$n_ofs += $n_lofs;
				$text->[0] = ("@@ -$o_ofs" .
					      (($o_cnt != 1)
					       ? ",$o_cnt" : '') .
					      " +$n_ofs" .
					      (($n_cnt != 1)
					       ? ",$n_cnt" : '') .
					      " @@\n");
			}
			for (@$text) {
				push @result, $_;
			}
		}
	}

	if (@result) {
		my $fh;

		open $fh, '| git apply --cached';
		for (@{$head->{TEXT}}, @result) {
			print $fh $_;
		}
		if (!close $fh) {
			for (@{$head->{TEXT}}, @result) {
				print STDERR $_;
			}
		}
		refresh();
	}

	print "\n";
}

sub diff_cmd {
	my @mods = list_modified('index-only');
	@mods = grep { !($_->{BINARY}) } @mods;
	return if (!@mods);
	my (@them) = list_and_choose({ PROMPT => 'Review diff',
				     IMMEDIATE => 1,
				     HEADER => $status_head, },
				   @mods);
	return if (!@them);
	system(qw(git diff-index -p --cached HEAD --),
	       map { $_->{VALUE} } @them);
}

sub quit_cmd {
	print "Bye.\n";
	exit(0);
}

sub help_cmd {
	print <<\EOF ;
status        - show paths with changes
update        - add working tree state to the staged set of changes
revert        - revert staged set of changes back to the HEAD version
patch         - pick hunks and update selectively
diff	      - view diff between HEAD and index
add untracked - add contents of untracked files to the staged set of changes
EOF
}

sub main_loop {
	my @cmd = ([ 'status', \&status_cmd, ],
		   [ 'update', \&update_cmd, ],
		   [ 'revert', \&revert_cmd, ],
		   [ 'add untracked', \&add_untracked_cmd, ],
		   [ 'patch', \&patch_update_cmd, ],
		   [ 'diff', \&diff_cmd, ],
		   [ 'quit', \&quit_cmd, ],
		   [ 'help', \&help_cmd, ],
	);
	while (1) {
		my ($it) = list_and_choose({ PROMPT => 'What now',
					     SINGLETON => 1,
					     LIST_FLAT => 4,
					     HEADER => '*** Commands ***',
					     ON_EOF => \&quit_cmd,
					     IMMEDIATE => 1 }, @cmd);
		if ($it) {
			eval {
				$it->[1]->();
			};
			if ($@) {
				print "$@";
			}
		}
	}
}

refresh();
status_cmd();
main_loop();
