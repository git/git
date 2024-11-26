package DiffHighlight;

require v5.26;
use warnings FATAL => 'all';
use strict;

# Use the correct value for both UNIX and Windows (/dev/null vs nul)
use File::Spec;

my $NULL = File::Spec->devnull();

# Highlight by reversing foreground and background. You could do
# other things like bold or underline if you prefer.
my @OLD_HIGHLIGHT = (
	color_config('color.diff-highlight.oldnormal'),
	color_config('color.diff-highlight.oldhighlight', "\x1b[7m"),
	color_config('color.diff-highlight.oldreset', "\x1b[27m")
);
my @NEW_HIGHLIGHT = (
	color_config('color.diff-highlight.newnormal', $OLD_HIGHLIGHT[0]),
	color_config('color.diff-highlight.newhighlight', $OLD_HIGHLIGHT[1]),
	color_config('color.diff-highlight.newreset', $OLD_HIGHLIGHT[2])
);

my $RESET = "\x1b[m";
my $COLOR = qr/\x1b\[[0-9;]*m/;
my $BORING = qr/$COLOR|\s/;

my @removed;
my @added;
my $in_hunk;
my $graph_indent = 0;

our $line_cb = sub { print @_ };
our $flush_cb = sub { local $| = 1 };

# Count the visible width of a string, excluding any terminal color sequences.
sub visible_width {
	local $_ = shift;
	my $ret = 0;
	while (length) {
		if (s/^$COLOR//) {
			# skip colors
		} elsif (s/^.//) {
			$ret++;
		}
	}
	return $ret;
}

# Return a substring of $str, omitting $len visible characters from the
# beginning, where terminal color sequences do not count as visible.
sub visible_substr {
	my ($str, $len) = @_;
	while ($len > 0) {
		if ($str =~ s/^$COLOR//) {
			next
		}
		$str =~ s/^.//;
		$len--;
	}
	return $str;
}

sub handle_line {
	my $orig = shift;
	local $_ = $orig;

	# match a graph line that begins a commit
	if (/^(?:$COLOR?\|$COLOR?[ ])* # zero or more leading "|" with space
	         $COLOR?\*$COLOR?[ ]   # a "*" with its trailing space
	      (?:$COLOR?\|$COLOR?[ ])* # zero or more trailing "|"
	                         [ ]*  # trailing whitespace for merges
	    /x) {
		my $graph_prefix = $&;

		# We must flush before setting graph indent, since the
		# new commit may be indented differently from what we
		# queued.
		flush();
		$graph_indent = visible_width($graph_prefix);

	} elsif ($graph_indent) {
		if (length($_) < $graph_indent) {
			$graph_indent = 0;
		} else {
			$_ = visible_substr($_, $graph_indent);
		}
	}

	if (!$in_hunk) {
		$line_cb->($orig);
		$in_hunk = /^$COLOR*\@\@ /;
	}
	elsif (/^$COLOR*-/) {
		push @removed, $orig;
	}
	elsif (/^$COLOR*\+/) {
		push @added, $orig;
	}
	else {
		flush();
		$line_cb->($orig);
		$in_hunk = /^$COLOR*[\@ ]/;
	}

	# Most of the time there is enough output to keep things streaming,
	# but for something like "git log -Sfoo", you can get one early
	# commit and then many seconds of nothing. We want to show
	# that one commit as soon as possible.
	#
	# Since we can receive arbitrary input, there's no optimal
	# place to flush. Flushing on a blank line is a heuristic that
	# happens to match git-log output.
	if (/^$/) {
		$flush_cb->();
	}
}

sub flush {
	# Flush any queued hunk (this can happen when there is no trailing
	# context in the final diff of the input).
	show_hunk(\@removed, \@added);
	@removed = ();
	@added = ();
}

sub highlight_stdin {
	while (<STDIN>) {
		handle_line($_);
	}
	flush();
}

# Ideally we would feed the default as a human-readable color to
# git-config as the fallback value. But diff-highlight does
# not otherwise depend on git at all, and there are reports
# of it being used in other settings. Let's handle our own
# fallback, which means we will work even if git can't be run.
sub color_config {
	my ($key, $default) = @_;
	my $s = `git config --get-color $key 2>$NULL`;
	return length($s) ? $s : $default;
}

sub show_hunk {
	my ($a, $b) = @_;

	# If one side is empty, then there is nothing to compare or highlight.
	if (!@$a || !@$b) {
		$line_cb->(@$a, @$b);
		return;
	}

	# If we have mismatched numbers of lines on each side, we could try to
	# be clever and match up similar lines. But for now we are simple and
	# stupid, and only handle multi-line hunks that remove and add the same
	# number of lines.
	if (@$a != @$b) {
		$line_cb->(@$a, @$b);
		return;
	}

	my @queue;
	for (my $i = 0; $i < @$a; $i++) {
		my ($rm, $add) = highlight_pair($a->[$i], $b->[$i]);
		$line_cb->($rm);
		push @queue, $add;
	}
	$line_cb->(@queue);
}

sub highlight_pair {
	my @a = split_line(shift);
	my @b = split_line(shift);

	# Find common prefix, taking care to skip any ansi
	# color codes.
	my $seen_plusminus;
	my ($pa, $pb) = (0, 0);
	while ($pa < @a && $pb < @b) {
		if ($a[$pa] =~ /$COLOR/) {
			$pa++;
		}
		elsif ($b[$pb] =~ /$COLOR/) {
			$pb++;
		}
		elsif ($a[$pa] eq $b[$pb]) {
			$pa++;
			$pb++;
		}
		elsif (!$seen_plusminus && $a[$pa] eq '-' && $b[$pb] eq '+') {
			$seen_plusminus = 1;
			$pa++;
			$pb++;
		}
		else {
			last;
		}
	}

	# Find common suffix, ignoring colors.
	my ($sa, $sb) = ($#a, $#b);
	while ($sa >= $pa && $sb >= $pb) {
		if ($a[$sa] =~ /$COLOR/) {
			$sa--;
		}
		elsif ($b[$sb] =~ /$COLOR/) {
			$sb--;
		}
		elsif ($a[$sa] eq $b[$sb]) {
			$sa--;
			$sb--;
		}
		else {
			last;
		}
	}

	if (is_pair_interesting(\@a, $pa, $sa, \@b, $pb, $sb)) {
		return highlight_line(\@a, $pa, $sa, \@OLD_HIGHLIGHT),
		       highlight_line(\@b, $pb, $sb, \@NEW_HIGHLIGHT);
	}
	else {
		return join('', @a),
		       join('', @b);
	}
}

# we split either by $COLOR or by character. This has the side effect of
# leaving in graph cruft. It works because the graph cruft does not contain "-"
# or "+"
sub split_line {
	local $_ = shift;
	return utf8::decode($_) ?
		map { utf8::encode($_); $_ }
			map { /$COLOR/ ? $_ : (split //) }
			split /($COLOR+)/ :
		map { /$COLOR/ ? $_ : (split //) }
		split /($COLOR+)/;
}

sub highlight_line {
	my ($line, $prefix, $suffix, $theme) = @_;

	my $start = join('', @{$line}[0..($prefix-1)]);
	my $mid = join('', @{$line}[$prefix..$suffix]);
	my $end = join('', @{$line}[($suffix+1)..$#$line]);

	# If we have a "normal" color specified, then take over the whole line.
	# Otherwise, we try to just manipulate the highlighted bits.
	if (defined $theme->[0]) {
		s/$COLOR//g for ($start, $mid, $end);
		chomp $end;
		return join('',
			$theme->[0], $start, $RESET,
			$theme->[1], $mid, $RESET,
			$theme->[0], $end, $RESET,
			"\n"
		);
	} else {
		return join('',
			$start,
			$theme->[1], $mid, $theme->[2],
			$end
		);
	}
}

# Pairs are interesting to highlight only if we are going to end up
# highlighting a subset (i.e., not the whole line). Otherwise, the highlighting
# is just useless noise. We can detect this by finding either a matching prefix
# or suffix (disregarding boring bits like whitespace and colorization).
sub is_pair_interesting {
	my ($a, $pa, $sa, $b, $pb, $sb) = @_;
	my $prefix_a = join('', @$a[0..($pa-1)]);
	my $prefix_b = join('', @$b[0..($pb-1)]);
	my $suffix_a = join('', @$a[($sa+1)..$#$a]);
	my $suffix_b = join('', @$b[($sb+1)..$#$b]);

	return visible_substr($prefix_a, $graph_indent) !~ /^$COLOR*-$BORING*$/ ||
	       visible_substr($prefix_b, $graph_indent) !~ /^$COLOR*\+$BORING*$/ ||
	       $suffix_a !~ /^$BORING*$/ ||
	       $suffix_b !~ /^$BORING*$/;
}
