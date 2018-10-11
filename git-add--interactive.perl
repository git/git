#!/usr/bin/perl

use 5.008;
use strict;
use warnings;
use Git qw(unquote_path);
use Git::I18N;

binmode(STDOUT, ":raw");

my $repo = Git->repository();

my $menu_use_color = $repo->get_colorbool('color.interactive');
my ($prompt_color, $header_color, $help_color) =
	$menu_use_color ? (
		$repo->get_color('color.interactive.prompt', 'bold blue'),
		$repo->get_color('color.interactive.header', 'bold'),
		$repo->get_color('color.interactive.help', 'red bold'),
	) : ();
my $error_color = ();
if ($menu_use_color) {
	my $help_color_spec = ($repo->config('color.interactive.help') or
				'red bold');
	$error_color = $repo->get_color('color.interactive.error',
					$help_color_spec);
}

my $diff_use_color = $repo->get_colorbool('color.diff');
my ($fraginfo_color) =
	$diff_use_color ? (
		$repo->get_color('color.diff.frag', 'cyan'),
	) : ();
my ($diff_context_color) =
	$diff_use_color ? (
		$repo->get_color($repo->config('color.diff.context') ? 'color.diff.context' : 'color.diff.plain', ''),
	) : ();
my ($diff_old_color) =
	$diff_use_color ? (
		$repo->get_color('color.diff.old', 'red'),
	) : ();
my ($diff_new_color) =
	$diff_use_color ? (
		$repo->get_color('color.diff.new', 'green'),
	) : ();

my $normal_color = $repo->get_color("", "reset");

my $diff_algorithm = $repo->config('diff.algorithm');
my $diff_filter = $repo->config('interactive.difffilter');

my $use_readkey = 0;
my $use_termcap = 0;
my %term_escapes;

sub ReadMode;
sub ReadKey;
if ($repo->config_bool("interactive.singlekey")) {
	eval {
		require Term::ReadKey;
		Term::ReadKey->import;
		$use_readkey = 1;
	};
	if (!$use_readkey) {
		print STDERR "missing Term::ReadKey, disabling interactive.singlekey\n";
	}
	eval {
		require Term::Cap;
		my $termcap = Term::Cap->Tgetent;
		foreach (values %$termcap) {
			$term_escapes{$_} = 1 if /^\e/;
		}
		$use_termcap = 1;
	};
}

sub colored {
	my $color = shift;
	my $string = join("", @_);

	if (defined $color) {
		# Put a color code at the beginning of each line, a reset at the end
		# color after newlines that are not at the end of the string
		$string =~ s/(\n+)(.)/$1$color$2/g;
		# reset before newlines
		$string =~ s/(\n+)/$normal_color$1/g;
		# codes at beginning and end (if necessary):
		$string =~ s/^/$color/;
		$string =~ s/$/$normal_color/ unless $string =~ /\n$/;
	}
	return $string;
}

# command line options
my $patch_mode_only;
my $patch_mode;
my $patch_mode_revision;

sub apply_patch;
sub apply_patch_for_checkout_commit;
sub apply_patch_for_stash;

my %patch_modes = (
	'stage' => {
		DIFF => 'diff-files -p',
		APPLY => sub { apply_patch 'apply --cached', @_; },
		APPLY_CHECK => 'apply --cached',
		FILTER => 'file-only',
		IS_REVERSE => 0,
	},
	'stash' => {
		DIFF => 'diff-index -p HEAD',
		APPLY => sub { apply_patch 'apply --cached', @_; },
		APPLY_CHECK => 'apply --cached',
		FILTER => undef,
		IS_REVERSE => 0,
	},
	'reset_head' => {
		DIFF => 'diff-index -p --cached',
		APPLY => sub { apply_patch 'apply -R --cached', @_; },
		APPLY_CHECK => 'apply -R --cached',
		FILTER => 'index-only',
		IS_REVERSE => 1,
	},
	'reset_nothead' => {
		DIFF => 'diff-index -R -p --cached',
		APPLY => sub { apply_patch 'apply --cached', @_; },
		APPLY_CHECK => 'apply --cached',
		FILTER => 'index-only',
		IS_REVERSE => 0,
	},
	'checkout_index' => {
		DIFF => 'diff-files -p',
		APPLY => sub { apply_patch 'apply -R', @_; },
		APPLY_CHECK => 'apply -R',
		FILTER => 'file-only',
		IS_REVERSE => 1,
	},
	'checkout_head' => {
		DIFF => 'diff-index -p',
		APPLY => sub { apply_patch_for_checkout_commit '-R', @_ },
		APPLY_CHECK => 'apply -R',
		FILTER => undef,
		IS_REVERSE => 1,
	},
	'checkout_nothead' => {
		DIFF => 'diff-index -R -p',
		APPLY => sub { apply_patch_for_checkout_commit '', @_ },
		APPLY_CHECK => 'apply',
		FILTER => undef,
		IS_REVERSE => 0,
	},
	'worktree_head' => {
		DIFF => 'diff-index -p',
		APPLY => sub { apply_patch 'apply -R', @_ },
		APPLY_CHECK => 'apply -R',
		FILTER => undef,
		IS_REVERSE => 1,
	},
	'worktree_nothead' => {
		DIFF => 'diff-index -R -p',
		APPLY => sub { apply_patch 'apply', @_ },
		APPLY_CHECK => 'apply',
		FILTER => undef,
		IS_REVERSE => 0,
	},
);

$patch_mode = 'stage';
my %patch_mode_flavour = %{$patch_modes{$patch_mode}};

sub run_cmd_pipe {
	if ($^O eq 'MSWin32') {
		my @invalid = grep {m/[":*]/} @_;
		die "$^O does not support: @invalid\n" if @invalid;
		my @args = map { m/ /o ? "\"$_\"": $_ } @_;
		return qx{@args};
	} elsif (($^O eq 'MSWin32' || $^O eq 'msys') && (scalar @_ > 200) &&
			grep $_ eq '--', @_) {
		use File::Temp qw(tempfile);
		my ($fhargs, $filename) =
			tempfile('git-args-XXXXXX', UNLINK => 1);

		my $cmd = 'cat '.$filename.' | xargs -0 -s 20000 ';
		while ($_[0] ne '--') {
			$cmd = $cmd . shift(@_) . ' ';
		}

		shift(@_);
		print $fhargs join("\0", @_);
		close($fhargs);

		my $fh = undef;
		open($fh, '-|', $cmd) or die;
		return <$fh>;
	} else {
		my $fh = undef;
		open($fh, '-|', @_) or die;
		my @out = <$fh>;
		close $fh || die "Cannot close @_ ($!)";
		return @out;
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
		unquote_path($_);
	}
	run_cmd_pipe(qw(git ls-files --others --exclude-standard --), @ARGV);
}

# TRANSLATORS: you can adjust this to align "git add -i" status menu
my $status_fmt = __('%12s %12s %s');
my $status_head = sprintf($status_fmt, __('staged'), __('unstaged'), __('path'));

{
	my $initial;
	sub is_initial_commit {
		$initial = system('git rev-parse HEAD -- >/dev/null 2>&1') != 0
			unless defined $initial;
		return $initial;
	}
}

{
	my $empty_tree;
	sub get_empty_tree {
		return $empty_tree if defined $empty_tree;

		($empty_tree) = run_cmd_pipe(qw(git hash-object -t tree /dev/null));
		chomp $empty_tree;
		return $empty_tree;
	}
}

sub get_diff_reference {
	my $ref = shift;
	if (defined $ref and $ref ne 'HEAD') {
		return $ref;
	} elsif (is_initial_commit()) {
		return get_empty_tree();
	} else {
		return 'HEAD';
	}
}

# Returns list of hashes, contents of each of which are:
# VALUE:	pathname
# BINARY:	is a binary path
# INDEX:	is index different from HEAD?
# FILE:		is file different from index?
# INDEX_ADDDEL:	is it add/delete between HEAD and index?
# FILE_ADDDEL:	is it add/delete between index and file?
# UNMERGED:	is the path unmerged

sub list_modified {
	my ($only) = @_;
	my (%data, @return);
	my ($add, $del, $adddel, $file);

	my $reference = get_diff_reference($patch_mode_revision);
	for (run_cmd_pipe(qw(git diff-index --cached
			     --numstat --summary), $reference,
			     '--', @ARGV)) {
		if (($add, $del, $file) =
		    /^([-\d]+)	([-\d]+)	(.*)/) {
			my ($change, $bin);
			$file = unquote_path($file);
			if ($add eq '-' && $del eq '-') {
				$change = __('binary');
				$bin = 1;
			}
			else {
				$change = "+$add/-$del";
			}
			$data{$file} = {
				INDEX => $change,
				BINARY => $bin,
				FILE => __('nothing'),
			}
		}
		elsif (($adddel, $file) =
		       /^ (create|delete) mode [0-7]+ (.*)$/) {
			$file = unquote_path($file);
			$data{$file}{INDEX_ADDDEL} = $adddel;
		}
	}

	for (run_cmd_pipe(qw(git diff-files --ignore-submodules=dirty --numstat --summary --raw --), @ARGV)) {
		if (($add, $del, $file) =
		    /^([-\d]+)	([-\d]+)	(.*)/) {
			$file = unquote_path($file);
			my ($change, $bin);
			if ($add eq '-' && $del eq '-') {
				$change = __('binary');
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
			$file = unquote_path($file);
			$data{$file}{FILE_ADDDEL} = $adddel;
		}
		elsif (/^:[0-7]+ [0-7]+ [0-9a-f]+ [0-9a-f]+ (.)	(.*)$/) {
			$file = unquote_path($2);
			if (!exists $data{$file}) {
				$data{$file} = +{
					INDEX => __('unchanged'),
					BINARY => 0,
				};
			}
			if ($1 eq 'U') {
				$data{$file}{UNMERGED} = 1;
			}
		}
	}

	for (sort keys %data) {
		my $it = $data{$_};

		if ($only) {
			if ($only eq 'index-only') {
				next if ($it->{INDEX} eq __('unchanged'));
			}
			if ($only eq 'file-only') {
				next if ($it->{FILE} eq __('nothing'));
			}
		}
		push @return, +{
			VALUE => $_,
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

# inserts string into trie and updates count for each character
sub update_trie {
	my ($trie, $string) = @_;
	foreach (split //, $string) {
		$trie = $trie->{$_} ||= {COUNT => 0};
		$trie->{COUNT}++;
	}
}

# returns an array of tuples (prefix, remainder)
sub find_unique_prefixes {
	my @stuff = @_;
	my @return = ();

	# any single prefix exceeding the soft limit is omitted
	# if any prefix exceeds the hard limit all are omitted
	# 0 indicates no limit
	my $soft_limit = 0;
	my $hard_limit = 3;

	# build a trie modelling all possible options
	my %trie;
	foreach my $print (@stuff) {
		if ((ref $print) eq 'ARRAY') {
			$print = $print->[0];
		}
		elsif ((ref $print) eq 'HASH') {
			$print = $print->{VALUE};
		}
		update_trie(\%trie, $print);
		push @return, $print;
	}

	# use the trie to find the unique prefixes
	for (my $i = 0; $i < @return; $i++) {
		my $ret = $return[$i];
		my @letters = split //, $ret;
		my %search = %trie;
		my ($prefix, $remainder);
		my $j;
		for ($j = 0; $j < @letters; $j++) {
			my $letter = $letters[$j];
			if ($search{$letter}{COUNT} == 1) {
				$prefix = substr $ret, 0, $j + 1;
				$remainder = substr $ret, $j + 1;
				last;
			}
			else {
				my $prefix = substr $ret, 0, $j;
				return ()
				    if ($hard_limit && $j + 1 > $hard_limit);
			}
			%search = %{$search{$letter}};
		}
		if (ord($letters[0]) > 127 ||
		    ($soft_limit && $j + 1 > $soft_limit)) {
			$prefix = undef;
			$remainder = $ret;
		}
		$return[$i] = [$prefix, $remainder];
	}
	return @return;
}

# filters out prefixes which have special meaning to list_and_choose()
sub is_valid_prefix {
	my $prefix = shift;
	return (defined $prefix) &&
	    !($prefix =~ /[\s,]/) && # separators
	    !($prefix =~ /^-/) &&    # deselection
	    !($prefix =~ /^\d+/) &&  # selection
	    ($prefix ne '*') &&      # "all" wildcard
	    ($prefix ne '?');        # prompt help
}

# given a prefix/remainder tuple return a string with the prefix highlighted
# for now use square brackets; later might use ANSI colors (underline, bold)
sub highlight_prefix {
	my $prefix = shift;
	my $remainder = shift;

	if (!defined $prefix) {
		return $remainder;
	}

	if (!is_valid_prefix($prefix)) {
		return "$prefix$remainder";
	}

	if (!$menu_use_color) {
		return "[$prefix]$remainder";
	}

	return "$prompt_color$prefix$normal_color$remainder";
}

sub error_msg {
	print STDERR colored $error_color, @_;
}

sub list_and_choose {
	my ($opts, @stuff) = @_;
	my (@chosen, @return);
	if (!@stuff) {
	    return @return;
	}
	my $i;
	my @prefixes = find_unique_prefixes(@stuff) unless $opts->{LIST_ONLY};

      TOPLOOP:
	while (1) {
		my $last_lf = 0;

		if ($opts->{HEADER}) {
			my $indent = $opts->{LIST_FLAT} ? "" : "     ";
			print colored $header_color, "$indent$opts->{HEADER}\n";
		}
		for ($i = 0; $i < @stuff; $i++) {
			my $chosen = $chosen[$i] ? '*' : ' ';
			my $print = $stuff[$i];
			my $ref = ref $print;
			my $highlighted = highlight_prefix(@{$prefixes[$i]})
			    if @prefixes;
			if ($ref eq 'ARRAY') {
				$print = $highlighted || $print->[0];
			}
			elsif ($ref eq 'HASH') {
				my $value = $highlighted || $print->{VALUE};
				$print = sprintf($status_fmt,
				    $print->{INDEX},
				    $print->{FILE},
				    $value);
			}
			else {
				$print = $highlighted || $print;
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

		print colored $prompt_color, $opts->{PROMPT};
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
		if ($line eq '?') {
			$opts->{SINGLETON} ?
			    singleton_prompt_help_cmd() :
			    prompt_help_cmd();
			next TOPLOOP;
		}
		for my $choice (split(/[\s,]+/, $line)) {
			my $choose = 1;
			my ($bottom, $top);

			# Input that begins with '-'; unchoose
			if ($choice =~ s/^-//) {
				$choose = 0;
			}
			# A range can be specified like 5-7 or 5-.
			if ($choice =~ /^(\d+)-(\d*)$/) {
				($bottom, $top) = ($1, length($2) ? $2 : 1 + @stuff);
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
					error_msg sprintf(__("Huh (%s)?\n"), $choice);
					next TOPLOOP;
				}
			}
			if ($opts->{SINGLETON} && $bottom != $top) {
				error_msg sprintf(__("Huh (%s)?\n"), $choice);
				next TOPLOOP;
			}
			for ($i = $bottom-1; $i <= $top-1; $i++) {
				next if (@stuff <= $i || $i < 0);
				$chosen[$i] = $choose;
			}
		}
		last if ($opts->{IMMEDIATE} || $line eq '*');
	}
	for ($i = 0; $i < @stuff; $i++) {
		if ($chosen[$i]) {
			push @return, $stuff[$i];
		}
	}
	return @return;
}

sub singleton_prompt_help_cmd {
	print colored $help_color, __ <<'EOF' ;
Prompt help:
1          - select a numbered item
foo        - select item based on unique prefix
           - (empty) select nothing
EOF
}

sub prompt_help_cmd {
	print colored $help_color, __ <<'EOF' ;
Prompt help:
1          - select a single item
3-5        - select a range of items
2-3,6-9    - select multiple ranges
foo        - select item based on unique prefix
-...       - unselect specified items
*          - choose all items
           - (empty) finish selecting
EOF
}

sub status_cmd {
	list_and_choose({ LIST_ONLY => 1, HEADER => $status_head },
			list_modified());
	print "\n";
}

sub say_n_paths {
	my $did = shift @_;
	my $cnt = scalar @_;
	if ($did eq 'added') {
		printf(__n("added %d path\n", "added %d paths\n",
			   $cnt), $cnt);
	} elsif ($did eq 'updated') {
		printf(__n("updated %d path\n", "updated %d paths\n",
			   $cnt), $cnt);
	} elsif ($did eq 'reverted') {
		printf(__n("reverted %d path\n", "reverted %d paths\n",
			   $cnt), $cnt);
	} else {
		printf(__n("touched %d path\n", "touched %d paths\n",
			   $cnt), $cnt);
	}
}

sub update_cmd {
	my @mods = list_modified('file-only');
	return if (!@mods);

	my @update = list_and_choose({ PROMPT => __('Update'),
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
	my @update = list_and_choose({ PROMPT => __('Revert'),
				       HEADER => $status_head, },
				     list_modified());
	if (@update) {
		if (is_initial_commit()) {
			system(qw(git rm --cached),
				map { $_->{VALUE} } @update);
		}
		else {
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
					printf(__("note: %s is untracked now.\n"), $_->{VALUE});
				}
			}
		}
		refresh();
		say_n_paths('reverted', @update);
	}
	print "\n";
}

sub add_untracked_cmd {
	my @add = list_and_choose({ PROMPT => __('Add untracked') },
				  list_untracked());
	if (@add) {
		system(qw(git update-index --add --), @add);
		say_n_paths('added', @add);
	} else {
		print __("No untracked files.\n");
	}
	print "\n";
}

sub run_git_apply {
	my $cmd = shift;
	my $fh;
	open $fh, '| git ' . $cmd . " --allow-overlap";
	print $fh @_;
	return close $fh;
}

sub parse_diff {
	my ($path) = @_;
	my @diff_cmd = split(" ", $patch_mode_flavour{DIFF});
	if (defined $diff_algorithm) {
		splice @diff_cmd, 1, 0, "--diff-algorithm=${diff_algorithm}";
	}
	if (defined $patch_mode_revision) {
		push @diff_cmd, get_diff_reference($patch_mode_revision);
	}
	my @diff = run_cmd_pipe("git", @diff_cmd, qw(--no-color --), $path);
	my @colored = ();
	if ($diff_use_color) {
		my @display_cmd = ("git", @diff_cmd, qw(--color --), $path);
		if (defined $diff_filter) {
			# quotemeta is overkill, but sufficient for shell-quoting
			my $diff = join(' ', map { quotemeta } @display_cmd);
			@display_cmd = ("$diff | $diff_filter");
		}

		@colored = run_cmd_pipe(@display_cmd);
	}
	my (@hunk) = { TEXT => [], DISPLAY => [], TYPE => 'header' };

	if (@colored && @colored != @diff) {
		print STDERR
		  "fatal: mismatched output from interactive.diffFilter\n",
		  "hint: Your filter must maintain a one-to-one correspondence\n",
		  "hint: between its input and output lines.\n";
		exit 1;
	}

	for (my $i = 0; $i < @diff; $i++) {
		if ($diff[$i] =~ /^@@ /) {
			push @hunk, { TEXT => [], DISPLAY => [],
				TYPE => 'hunk' };
		}
		push @{$hunk[-1]{TEXT}}, $diff[$i];
		push @{$hunk[-1]{DISPLAY}},
			(@colored ? $colored[$i] : $diff[$i]);
	}
	return @hunk;
}

sub parse_diff_header {
	my $src = shift;

	my $head = { TEXT => [], DISPLAY => [], TYPE => 'header' };
	my $mode = { TEXT => [], DISPLAY => [], TYPE => 'mode' };
	my $deletion = { TEXT => [], DISPLAY => [], TYPE => 'deletion' };
	my $addition;

	for (my $i = 0; $i < @{$src->{TEXT}}; $i++) {
		if ($src->{TEXT}->[$i] =~ /^new file/) {
			$addition = 1;
			$head->{TYPE} = 'addition';
		}
		my $dest =
		   $src->{TEXT}->[$i] =~ /^(old|new) mode (\d+)$/ ? $mode :
		   $src->{TEXT}->[$i] =~ /^deleted file/ ? $deletion :
		   $head;
		push @{$dest->{TEXT}}, $src->{TEXT}->[$i];
		push @{$dest->{DISPLAY}}, $src->{DISPLAY}->[$i];
	}
	return ($head, $mode, $deletion, $addition);
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

sub format_hunk_header {
	my ($o_ofs, $o_cnt, $n_ofs, $n_cnt) = @_;
	return ("@@ -$o_ofs" .
		(($o_cnt != 1) ? ",$o_cnt" : '') .
		" +$n_ofs" .
		(($n_cnt != 1) ? ",$n_cnt" : '') .
		" @@\n");
}

sub split_hunk {
	my ($text, $display) = @_;
	my @split = ();
	if (!defined $display) {
		$display = $text;
	}
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
			DISPLAY => [],
			TYPE => 'hunk',
			OLD => $o_ofs,
			NEW => $n_ofs,
			OCNT => 0,
			NCNT => 0,
			ADDDEL => 0,
			POSTCTX => 0,
			USE => undef,
		};

		while (++$i < @$text) {
			my $line = $text->[$i];
			my $display = $display->[$i];
			if ($line =~ /^\\/) {
				push @{$this->{TEXT}}, $line;
				push @{$this->{DISPLAY}}, $display;
				next;
			}
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
				push @{$this->{DISPLAY}}, $display;
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
			push @{$this->{DISPLAY}}, $display;
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

		my $head = format_hunk_header($o_ofs, $o_cnt, $n_ofs, $n_cnt);
		my $display_head = $head;
		unshift @{$hunk->{TEXT}}, $head;
		if ($diff_use_color) {
			$display_head = colored($fraginfo_color, $head);
		}
		unshift @{$hunk->{DISPLAY}}, $display_head;
	}
	return @split;
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
		} elsif ($line =~ /^\\/) {
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
		} elsif ($line =~ /^\\/) {
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
	my $head = format_hunk_header($o0_ofs, $o_cnt, $n0_ofs, $n_cnt);
	@{$prev->{TEXT}} = ($head, @line);
}

sub coalesce_overlapping_hunks {
	my (@in) = @_;
	my @out = ();

	my ($last_o_ctx, $last_was_dirty);
	my $ofs_delta = 0;

	for (@in) {
		if ($_->{TYPE} ne 'hunk') {
			push @out, $_;
			next;
		}
		my $text = $_->{TEXT};
		my ($o_ofs, $o_cnt, $n_ofs, $n_cnt) =
						parse_hunk_header($text->[0]);
		unless ($_->{USE}) {
			$ofs_delta += $o_cnt - $n_cnt;
			# If this hunk has been edited then subtract
			# the delta that is due to the edit.
			if ($_->{OFS_DELTA}) {
				$ofs_delta -= $_->{OFS_DELTA};
			}
			next;
		}
		if ($ofs_delta) {
			if ($patch_mode_flavour{IS_REVERSE}) {
				$o_ofs -= $ofs_delta;
			} else {
				$n_ofs += $ofs_delta;
			}
			$_->{TEXT}->[0] = format_hunk_header($o_ofs, $o_cnt,
							     $n_ofs, $n_cnt);
		}
		# If this hunk was edited then adjust the offset delta
		# to reflect the edit.
		if ($_->{OFS_DELTA}) {
			$ofs_delta += $_->{OFS_DELTA};
		}
		if (defined $last_o_ctx &&
		    $o_ofs <= $last_o_ctx &&
		    !$_->{DIRTY} &&
		    !$last_was_dirty) {
			merge_hunk($out[-1], $_);
		}
		else {
			push @out, $_;
		}
		$last_o_ctx = find_last_o_ctx($out[-1]);
		$last_was_dirty = $_->{DIRTY};
	}
	return @out;
}

sub reassemble_patch {
	my $head = shift;
	my @patch;

	# Include everything in the header except the beginning of the diff.
	push @patch, (grep { !/^[-+]{3}/ } @$head);

	# Then include any headers from the hunk lines, which must
	# come before any actual hunk.
	while (@_ && $_[0] !~ /^@/) {
		push @patch, shift;
	}

	# Then begin the diff.
	push @patch, grep { /^[-+]{3}/ } @$head;

	# And then the actual hunks.
	push @patch, @_;

	return @patch;
}

sub color_diff {
	return map {
		colored((/^@/  ? $fraginfo_color :
			 /^\+/ ? $diff_new_color :
			 /^-/  ? $diff_old_color :
			 $diff_context_color),
			$_);
	} @_;
}

my %edit_hunk_manually_modes = (
	stage => N__(
"If the patch applies cleanly, the edited hunk will immediately be
marked for staging."),
	stash => N__(
"If the patch applies cleanly, the edited hunk will immediately be
marked for stashing."),
	reset_head => N__(
"If the patch applies cleanly, the edited hunk will immediately be
marked for unstaging."),
	reset_nothead => N__(
"If the patch applies cleanly, the edited hunk will immediately be
marked for applying."),
	checkout_index => N__(
"If the patch applies cleanly, the edited hunk will immediately be
marked for discarding."),
	checkout_head => N__(
"If the patch applies cleanly, the edited hunk will immediately be
marked for discarding."),
	checkout_nothead => N__(
"If the patch applies cleanly, the edited hunk will immediately be
marked for applying."),
	worktree_head => N__(
"If the patch applies cleanly, the edited hunk will immediately be
marked for discarding."),
	worktree_nothead => N__(
"If the patch applies cleanly, the edited hunk will immediately be
marked for applying."),
);

sub recount_edited_hunk {
	local $_;
	my ($oldtext, $newtext) = @_;
	my ($o_cnt, $n_cnt) = (0, 0);
	for (@{$newtext}[1..$#{$newtext}]) {
		my $mode = substr($_, 0, 1);
		if ($mode eq '-') {
			$o_cnt++;
		} elsif ($mode eq '+') {
			$n_cnt++;
		} elsif ($mode eq ' ' or $mode eq "\n") {
			$o_cnt++;
			$n_cnt++;
		}
	}
	my ($o_ofs, undef, $n_ofs, undef) =
					parse_hunk_header($newtext->[0]);
	$newtext->[0] = format_hunk_header($o_ofs, $o_cnt, $n_ofs, $n_cnt);
	my (undef, $orig_o_cnt, undef, $orig_n_cnt) =
					parse_hunk_header($oldtext->[0]);
	# Return the change in the number of lines inserted by this hunk
	return $orig_o_cnt - $orig_n_cnt - $o_cnt + $n_cnt;
}

sub edit_hunk_manually {
	my ($oldtext) = @_;

	my $hunkfile = $repo->repo_path . "/addp-hunk-edit.diff";
	my $fh;
	open $fh, '>', $hunkfile
		or die sprintf(__("failed to open hunk edit file for writing: %s"), $!);
	print $fh Git::comment_lines __("Manual hunk edit mode -- see bottom for a quick guide.\n");
	print $fh @$oldtext;
	my $is_reverse = $patch_mode_flavour{IS_REVERSE};
	my ($remove_plus, $remove_minus) = $is_reverse ? ('-', '+') : ('+', '-');
	my $comment_line_char = Git::get_comment_line_char;
	print $fh Git::comment_lines sprintf(__ <<EOF, $remove_minus, $remove_plus, $comment_line_char),
---
To remove '%s' lines, make them ' ' lines (context).
To remove '%s' lines, delete them.
Lines starting with %s will be removed.
EOF
__($edit_hunk_manually_modes{$patch_mode}),
# TRANSLATORS: 'it' refers to the patch mentioned in the previous messages.
__ <<EOF2 ;
If it does not apply cleanly, you will be given an opportunity to
edit again.  If all lines of the hunk are removed, then the edit is
aborted and the hunk is left unchanged.
EOF2
	close $fh;

	chomp(my ($editor) = run_cmd_pipe(qw(git var GIT_EDITOR)));
	system('sh', '-c', $editor.' "$@"', $editor, $hunkfile);

	if ($? != 0) {
		return undef;
	}

	open $fh, '<', $hunkfile
		or die sprintf(__("failed to open hunk edit file for reading: %s"), $!);
	my @newtext = grep { !/^\Q$comment_line_char\E/ } <$fh>;
	close $fh;
	unlink $hunkfile;

	# Abort if nothing remains
	if (!grep { /\S/ } @newtext) {
		return undef;
	}

	# Reinsert the first hunk header if the user accidentally deleted it
	if ($newtext[0] !~ /^@/) {
		unshift @newtext, $oldtext->[0];
	}
	return \@newtext;
}

sub diff_applies {
	return run_git_apply($patch_mode_flavour{APPLY_CHECK} . ' --check',
			     map { @{$_->{TEXT}} } @_);
}

sub _restore_terminal_and_die {
	ReadMode 'restore';
	print "\n";
	exit 1;
}

sub prompt_single_character {
	if ($use_readkey) {
		local $SIG{TERM} = \&_restore_terminal_and_die;
		local $SIG{INT} = \&_restore_terminal_and_die;
		ReadMode 'cbreak';
		my $key = ReadKey 0;
		ReadMode 'restore';
		if ($use_termcap and $key eq "\e") {
			while (!defined $term_escapes{$key}) {
				my $next = ReadKey 0.5;
				last if (!defined $next);
				$key .= $next;
			}
			$key =~ s/\e/^[/;
		}
		print "$key" if defined $key;
		print "\n";
		return $key;
	} else {
		return <STDIN>;
	}
}

sub prompt_yesno {
	my ($prompt) = @_;
	while (1) {
		print colored $prompt_color, $prompt;
		my $line = prompt_single_character;
		return undef unless defined $line;
		return 0 if $line =~ /^n/i;
		return 1 if $line =~ /^y/i;
	}
}

sub edit_hunk_loop {
	my ($head, $hunks, $ix) = @_;
	my $hunk = $hunks->[$ix];
	my $text = $hunk->{TEXT};

	while (1) {
		my $newtext = edit_hunk_manually($text);
		if (!defined $newtext) {
			return undef;
		}
		my $newhunk = {
			TEXT => $newtext,
			TYPE => $hunk->{TYPE},
			USE => 1,
			DIRTY => 1,
		};
		$newhunk->{OFS_DELTA} = recount_edited_hunk($text, $newtext);
		# If this hunk has already been edited then add the
		# offset delta of the previous edit to get the real
		# delta from the original unedited hunk.
		$hunk->{OFS_DELTA} and
				$newhunk->{OFS_DELTA} += $hunk->{OFS_DELTA};
		if (diff_applies($head,
				 @{$hunks}[0..$ix-1],
				 $newhunk,
				 @{$hunks}[$ix+1..$#{$hunks}])) {
			$newhunk->{DISPLAY} = [color_diff(@{$newtext})];
			return $newhunk;
		}
		else {
			prompt_yesno(
				# TRANSLATORS: do not translate [y/n]
				# The program will only accept that input
				# at this point.
				# Consider translating (saying "no" discards!) as
				# (saying "n" for "no" discards!) if the translation
				# of the word "no" does not start with n.
				__('Your edited hunk does not apply. Edit again '
				   . '(saying "no" discards!) [y/n]? ')
				) or return undef;
		}
	}
}

my %help_patch_modes = (
	stage => N__(
"y - stage this hunk
n - do not stage this hunk
q - quit; do not stage this hunk or any of the remaining ones
a - stage this hunk and all later hunks in the file
d - do not stage this hunk or any of the later hunks in the file"),
	stash => N__(
"y - stash this hunk
n - do not stash this hunk
q - quit; do not stash this hunk or any of the remaining ones
a - stash this hunk and all later hunks in the file
d - do not stash this hunk or any of the later hunks in the file"),
	reset_head => N__(
"y - unstage this hunk
n - do not unstage this hunk
q - quit; do not unstage this hunk or any of the remaining ones
a - unstage this hunk and all later hunks in the file
d - do not unstage this hunk or any of the later hunks in the file"),
	reset_nothead => N__(
"y - apply this hunk to index
n - do not apply this hunk to index
q - quit; do not apply this hunk or any of the remaining ones
a - apply this hunk and all later hunks in the file
d - do not apply this hunk or any of the later hunks in the file"),
	checkout_index => N__(
"y - discard this hunk from worktree
n - do not discard this hunk from worktree
q - quit; do not discard this hunk or any of the remaining ones
a - discard this hunk and all later hunks in the file
d - do not discard this hunk or any of the later hunks in the file"),
	checkout_head => N__(
"y - discard this hunk from index and worktree
n - do not discard this hunk from index and worktree
q - quit; do not discard this hunk or any of the remaining ones
a - discard this hunk and all later hunks in the file
d - do not discard this hunk or any of the later hunks in the file"),
	checkout_nothead => N__(
"y - apply this hunk to index and worktree
n - do not apply this hunk to index and worktree
q - quit; do not apply this hunk or any of the remaining ones
a - apply this hunk and all later hunks in the file
d - do not apply this hunk or any of the later hunks in the file"),
	worktree_head => N__(
"y - discard this hunk from worktree
n - do not discard this hunk from worktree
q - quit; do not discard this hunk or any of the remaining ones
a - discard this hunk and all later hunks in the file
d - do not discard this hunk or any of the later hunks in the file"),
	worktree_nothead => N__(
"y - apply this hunk to worktree
n - do not apply this hunk to worktree
q - quit; do not apply this hunk or any of the remaining ones
a - apply this hunk and all later hunks in the file
d - do not apply this hunk or any of the later hunks in the file"),
);

sub help_patch_cmd {
	local $_;
	my $other = $_[0] . ",?";
	print colored $help_color, __($help_patch_modes{$patch_mode}), "\n",
		map { "$_\n" } grep {
			my $c = quotemeta(substr($_, 0, 1));
			$other =~ /,$c/
		} split "\n", __ <<EOF ;
g - select a hunk to go to
/ - search for a hunk matching the given regex
j - leave this hunk undecided, see next undecided hunk
J - leave this hunk undecided, see next hunk
k - leave this hunk undecided, see previous undecided hunk
K - leave this hunk undecided, see previous hunk
s - split the current hunk into smaller hunks
e - manually edit the current hunk
? - print help
EOF
}

sub apply_patch {
	my $cmd = shift;
	my $ret = run_git_apply $cmd, @_;
	if (!$ret) {
		print STDERR @_;
	}
	return $ret;
}

sub apply_patch_for_checkout_commit {
	my $reverse = shift;
	my $applies_index = run_git_apply 'apply '.$reverse.' --cached --check', @_;
	my $applies_worktree = run_git_apply 'apply '.$reverse.' --check', @_;

	if ($applies_worktree && $applies_index) {
		run_git_apply 'apply '.$reverse.' --cached', @_;
		run_git_apply 'apply '.$reverse, @_;
		return 1;
	} elsif (!$applies_index) {
		print colored $error_color, __("The selected hunks do not apply to the index!\n");
		if (prompt_yesno __("Apply them to the worktree anyway? ")) {
			return run_git_apply 'apply '.$reverse, @_;
		} else {
			print colored $error_color, __("Nothing was applied.\n");
			return 0;
		}
	} else {
		print STDERR @_;
		return 0;
	}
}

sub patch_update_cmd {
	my @all_mods = list_modified($patch_mode_flavour{FILTER});
	error_msg sprintf(__("ignoring unmerged: %s\n"), $_->{VALUE})
		for grep { $_->{UNMERGED} } @all_mods;
	@all_mods = grep { !$_->{UNMERGED} } @all_mods;

	my @mods = grep { !($_->{BINARY}) } @all_mods;
	my @them;

	if (!@mods) {
		if (@all_mods) {
			print STDERR __("Only binary files changed.\n");
		} else {
			print STDERR __("No changes.\n");
		}
		return 0;
	}
	if ($patch_mode_only) {
		@them = @mods;
	}
	else {
		@them = list_and_choose({ PROMPT => __('Patch update'),
					  HEADER => $status_head, },
					@mods);
	}
	for (@them) {
		return 0 if patch_update_file($_->{VALUE});
	}
}

# Generate a one line summary of a hunk.
sub summarize_hunk {
	my $rhunk = shift;
	my $summary = $rhunk->{TEXT}[0];

	# Keep the line numbers, discard extra context.
	$summary =~ s/@@(.*?)@@.*/$1 /s;
	$summary .= " " x (20 - length $summary);

	# Add some user context.
	for my $line (@{$rhunk->{TEXT}}) {
		if ($line =~ m/^[+-].*\w/) {
			$summary .= $line;
			last;
		}
	}

	chomp $summary;
	return substr($summary, 0, 80) . "\n";
}


# Print a one-line summary of each hunk in the array ref in
# the first argument, starting with the index in the 2nd.
sub display_hunks {
	my ($hunks, $i) = @_;
	my $ctr = 0;
	$i ||= 0;
	for (; $i < @$hunks && $ctr < 20; $i++, $ctr++) {
		my $status = " ";
		if (defined $hunks->[$i]{USE}) {
			$status = $hunks->[$i]{USE} ? "+" : "-";
		}
		printf "%s%2d: %s",
			$status,
			$i + 1,
			summarize_hunk($hunks->[$i]);
	}
	return $i;
}

my %patch_update_prompt_modes = (
	stage => {
		mode => N__("Stage mode change [y,n,q,a,d%s,?]? "),
		deletion => N__("Stage deletion [y,n,q,a,d%s,?]? "),
		addition => N__("Stage addition [y,n,q,a,d%s,?]? "),
		hunk => N__("Stage this hunk [y,n,q,a,d%s,?]? "),
	},
	stash => {
		mode => N__("Stash mode change [y,n,q,a,d%s,?]? "),
		deletion => N__("Stash deletion [y,n,q,a,d%s,?]? "),
		addition => N__("Stash addition [y,n,q,a,d%s,?]? "),
		hunk => N__("Stash this hunk [y,n,q,a,d%s,?]? "),
	},
	reset_head => {
		mode => N__("Unstage mode change [y,n,q,a,d%s,?]? "),
		deletion => N__("Unstage deletion [y,n,q,a,d%s,?]? "),
		addition => N__("Unstage addition [y,n,q,a,d%s,?]? "),
		hunk => N__("Unstage this hunk [y,n,q,a,d%s,?]? "),
	},
	reset_nothead => {
		mode => N__("Apply mode change to index [y,n,q,a,d%s,?]? "),
		deletion => N__("Apply deletion to index [y,n,q,a,d%s,?]? "),
		addition => N__("Apply addition to index [y,n,q,a,d%s,?]? "),
		hunk => N__("Apply this hunk to index [y,n,q,a,d%s,?]? "),
	},
	checkout_index => {
		mode => N__("Discard mode change from worktree [y,n,q,a,d%s,?]? "),
		deletion => N__("Discard deletion from worktree [y,n,q,a,d%s,?]? "),
		addition => N__("Discard addition from worktree [y,n,q,a,d%s,?]? "),
		hunk => N__("Discard this hunk from worktree [y,n,q,a,d%s,?]? "),
	},
	checkout_head => {
		mode => N__("Discard mode change from index and worktree [y,n,q,a,d%s,?]? "),
		deletion => N__("Discard deletion from index and worktree [y,n,q,a,d%s,?]? "),
		addition => N__("Discard addition from index and worktree [y,n,q,a,d%s,?]? "),
		hunk => N__("Discard this hunk from index and worktree [y,n,q,a,d%s,?]? "),
	},
	checkout_nothead => {
		mode => N__("Apply mode change to index and worktree [y,n,q,a,d%s,?]? "),
		deletion => N__("Apply deletion to index and worktree [y,n,q,a,d%s,?]? "),
		addition => N__("Apply addition to index and worktree [y,n,q,a,d%s,?]? "),
		hunk => N__("Apply this hunk to index and worktree [y,n,q,a,d%s,?]? "),
	},
	worktree_head => {
		mode => N__("Discard mode change from worktree [y,n,q,a,d%s,?]? "),
		deletion => N__("Discard deletion from worktree [y,n,q,a,d%s,?]? "),
		addition => N__("Discard addition from worktree [y,n,q,a,d%s,?]? "),
		hunk => N__("Discard this hunk from worktree [y,n,q,a,d%s,?]? "),
	},
	worktree_nothead => {
		mode => N__("Apply mode change to worktree [y,n,q,a,d%s,?]? "),
		deletion => N__("Apply deletion to worktree [y,n,q,a,d%s,?]? "),
		addition => N__("Apply addition to worktree [y,n,q,a,d%s,?]? "),
		hunk => N__("Apply this hunk to worktree [y,n,q,a,d%s,?]? "),
	},
);

sub patch_update_file {
	my $quit = 0;
	my ($ix, $num);
	my $path = shift;
	my ($head, @hunk) = parse_diff($path);
	($head, my $mode, my $deletion, my $addition) = parse_diff_header($head);
	for (@{$head->{DISPLAY}}) {
		print;
	}

	if (@{$mode->{TEXT}}) {
		unshift @hunk, $mode;
	}
	if (@{$deletion->{TEXT}}) {
		foreach my $hunk (@hunk) {
			push @{$deletion->{TEXT}}, @{$hunk->{TEXT}};
			push @{$deletion->{DISPLAY}}, @{$hunk->{DISPLAY}};
		}
		@hunk = ($deletion);
	}

	$num = scalar @hunk;
	$ix = 0;

	while (1) {
		my ($prev, $next, $other, $undecided, $i);
		$other = '';

		last if ($ix and !$num);
		if ($num <= $ix) {
			$ix = 0;
		}
		for ($i = 0; $i < $ix; $i++) {
			if (!defined $hunk[$i]{USE}) {
				$prev = 1;
				$other .= ',k';
				last;
			}
		}
		if ($ix) {
			$other .= ',K';
		}
		for ($i = $ix + 1; $i < $num; $i++) {
			if (!defined $hunk[$i]{USE}) {
				$next = 1;
				$other .= ',j';
				last;
			}
		}
		if ($ix < $num - 1) {
			$other .= ',J';
		}
		if ($num > 1) {
			$other .= ',g,/';
		}
		for ($i = 0; $i < $num; $i++) {
			if (!defined $hunk[$i]{USE}) {
				$undecided = 1;
				last;
			}
		}
		last if (!$undecided && ($num || !$addition));

		if ($num) {
			if ($hunk[$ix]{TYPE} eq 'hunk' &&
			    hunk_splittable($hunk[$ix]{TEXT})) {
				$other .= ',s';
			}
			if ($hunk[$ix]{TYPE} eq 'hunk') {
				$other .= ',e';
			}
			for (@{$hunk[$ix]{DISPLAY}}) {
				print;
			}
		}
		my $type = $num ? $hunk[$ix]{TYPE} : $head->{TYPE};
		print colored $prompt_color, "(", ($ix+1), "/", ($num ? $num : 1), ") ",
			sprintf(__($patch_update_prompt_modes{$patch_mode}{$type}), $other);

		my $line = prompt_single_character;
		last unless defined $line;
		if ($line) {
			if ($line =~ /^y/i) {
				if ($num) {
					$hunk[$ix]{USE} = 1;
				} else {
					$head->{USE} = 1;
				}
			}
			elsif ($line =~ /^n/i) {
				if ($num) {
					$hunk[$ix]{USE} = 0;
				} else {
					$head->{USE} = 0;
				}
			}
			elsif ($line =~ /^a/i) {
				if ($num) {
					while ($ix < $num) {
						if (!defined $hunk[$ix]{USE}) {
							$hunk[$ix]{USE} = 1;
						}
						$ix++;
					}
				} else {
					$head->{USE} = 1;
					$ix++;
				}
				next;
			}
			elsif ($line =~ /^g(.*)/) {
				my $response = $1;
				unless ($other =~ /g/) {
					error_msg __("No other hunks to goto\n");
					next;
				}
				my $no = $ix > 10 ? $ix - 10 : 0;
				while ($response eq '') {
					$no = display_hunks(\@hunk, $no);
					if ($no < $num) {
						print __("go to which hunk (<ret> to see more)? ");
					} else {
						print __("go to which hunk? ");
					}
					$response = <STDIN>;
					if (!defined $response) {
						$response = '';
					}
					chomp $response;
				}
				if ($response !~ /^\s*\d+\s*$/) {
					error_msg sprintf(__("Invalid number: '%s'\n"),
							     $response);
				} elsif (0 < $response && $response <= $num) {
					$ix = $response - 1;
				} else {
					error_msg sprintf(__n("Sorry, only %d hunk available.\n",
							      "Sorry, only %d hunks available.\n", $num), $num);
				}
				next;
			}
			elsif ($line =~ /^d/i) {
				if ($num) {
					while ($ix < $num) {
						if (!defined $hunk[$ix]{USE}) {
							$hunk[$ix]{USE} = 0;
						}
						$ix++;
					}
				} else {
					$head->{USE} = 0;
					$ix++;
				}
				next;
			}
			elsif ($line =~ /^q/i) {
				if ($num) {
					for ($i = 0; $i < $num; $i++) {
						if (!defined $hunk[$i]{USE}) {
							$hunk[$i]{USE} = 0;
						}
					}
				} elsif (!defined $head->{USE}) {
					$head->{USE} = 0;
				}
				$quit = 1;
				last;
			}
			elsif ($line =~ m|^/(.*)|) {
				my $regex = $1;
				unless ($other =~ m|/|) {
					error_msg __("No other hunks to search\n");
					next;
				}
				if ($regex eq "") {
					print colored $prompt_color, __("search for regex? ");
					$regex = <STDIN>;
					if (defined $regex) {
						chomp $regex;
					}
				}
				my $search_string;
				eval {
					$search_string = qr{$regex}m;
				};
				if ($@) {
					my ($err,$exp) = ($@, $1);
					$err =~ s/ at .*git-add--interactive line \d+, <STDIN> line \d+.*$//;
					error_msg sprintf(__("Malformed search regexp %s: %s\n"), $exp, $err);
					next;
				}
				my $iy = $ix;
				while (1) {
					my $text = join ("", @{$hunk[$iy]{TEXT}});
					last if ($text =~ $search_string);
					$iy++;
					$iy = 0 if ($iy >= $num);
					if ($ix == $iy) {
						error_msg __("No hunk matches the given pattern\n");
						last;
					}
				}
				$ix = $iy;
				next;
			}
			elsif ($line =~ /^K/) {
				if ($other =~ /K/) {
					$ix--;
				}
				else {
					error_msg __("No previous hunk\n");
				}
				next;
			}
			elsif ($line =~ /^J/) {
				if ($other =~ /J/) {
					$ix++;
				}
				else {
					error_msg __("No next hunk\n");
				}
				next;
			}
			elsif ($line =~ /^k/) {
				if ($other =~ /k/) {
					while (1) {
						$ix--;
						last if (!$ix ||
							 !defined $hunk[$ix]{USE});
					}
				}
				else {
					error_msg __("No previous hunk\n");
				}
				next;
			}
			elsif ($line =~ /^j/) {
				if ($other !~ /j/) {
					error_msg __("No next hunk\n");
					next;
				}
			}
			elsif ($line =~ /^s/) {
				unless ($other =~ /s/) {
					error_msg __("Sorry, cannot split this hunk\n");
					next;
				}
				my @split = split_hunk($hunk[$ix]{TEXT}, $hunk[$ix]{DISPLAY});
				if (1 < @split) {
					print colored $header_color, sprintf(
						__n("Split into %d hunk.\n",
						    "Split into %d hunks.\n",
						    scalar(@split)), scalar(@split));
				}
				splice (@hunk, $ix, 1, @split);
				$num = scalar @hunk;
				next;
			}
			elsif ($line =~ /^e/) {
				unless ($other =~ /e/) {
					error_msg __("Sorry, cannot edit this hunk\n");
					next;
				}
				my $newhunk = edit_hunk_loop($head, \@hunk, $ix);
				if (defined $newhunk) {
					splice @hunk, $ix, 1, $newhunk;
				}
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

	@hunk = coalesce_overlapping_hunks(@hunk) if ($num);

	my $n_lofs = 0;
	my @result = ();
	for (@hunk) {
		if ($_->{USE}) {
			push @result, @{$_->{TEXT}};
		}
	}

	if (@result or $head->{USE}) {
		my @patch = reassemble_patch($head->{TEXT}, @result);
		my $apply_routine = $patch_mode_flavour{APPLY};
		&$apply_routine(@patch);
		refresh();
	}

	print "\n";
	return $quit;
}

sub diff_cmd {
	my @mods = list_modified('index-only');
	@mods = grep { !($_->{BINARY}) } @mods;
	return if (!@mods);
	my (@them) = list_and_choose({ PROMPT => __('Review diff'),
				     IMMEDIATE => 1,
				     HEADER => $status_head, },
				   @mods);
	return if (!@them);
	my $reference = (is_initial_commit()) ? get_empty_tree() : 'HEAD';
	system(qw(git diff -p --cached), $reference, '--',
		map { $_->{VALUE} } @them);
}

sub quit_cmd {
	print __("Bye.\n");
	exit(0);
}

sub help_cmd {
# TRANSLATORS: please do not translate the command names
# 'status', 'update', 'revert', etc.
	print colored $help_color, __ <<'EOF' ;
status        - show paths with changes
update        - add working tree state to the staged set of changes
revert        - revert staged set of changes back to the HEAD version
patch         - pick hunks and update selectively
diff          - view diff between HEAD and index
add untracked - add contents of untracked files to the staged set of changes
EOF
}

sub process_args {
	return unless @ARGV;
	my $arg = shift @ARGV;
	if ($arg =~ /--patch(?:=(.*))?/) {
		if (defined $1) {
			if ($1 eq 'reset') {
				$patch_mode = 'reset_head';
				$patch_mode_revision = 'HEAD';
				$arg = shift @ARGV or die __("missing --");
				if ($arg ne '--') {
					$patch_mode_revision = $arg;

					# NEEDSWORK: Instead of comparing to the literal "HEAD",
					# compare the commit objects instead so that other ways of
					# saying the same thing (such as "@") are also handled
					# appropriately.
					#
					# This applies to the cases below too.
					$patch_mode = ($arg eq 'HEAD' ?
						       'reset_head' : 'reset_nothead');
					$arg = shift @ARGV or die __("missing --");
				}
			} elsif ($1 eq 'checkout') {
				$arg = shift @ARGV or die __("missing --");
				if ($arg eq '--') {
					$patch_mode = 'checkout_index';
				} else {
					$patch_mode_revision = $arg;
					$patch_mode = ($arg eq 'HEAD' ?
						       'checkout_head' : 'checkout_nothead');
					$arg = shift @ARGV or die __("missing --");
				}
			} elsif ($1 eq 'worktree') {
				$arg = shift @ARGV or die __("missing --");
				if ($arg eq '--') {
					$patch_mode = 'checkout_index';
				} else {
					$patch_mode_revision = $arg;
					$patch_mode = ($arg eq 'HEAD' ?
						       'worktree_head' : 'worktree_nothead');
					$arg = shift @ARGV or die __("missing --");
				}
			} elsif ($1 eq 'stage' or $1 eq 'stash') {
				$patch_mode = $1;
				$arg = shift @ARGV or die __("missing --");
			} else {
				die sprintf(__("unknown --patch mode: %s"), $1);
			}
		} else {
			$patch_mode = 'stage';
			$arg = shift @ARGV or die __("missing --");
		}
		die sprintf(__("invalid argument %s, expecting --"),
			       $arg) unless $arg eq "--";
		%patch_mode_flavour = %{$patch_modes{$patch_mode}};
		$patch_mode_only = 1;
	}
	elsif ($arg ne "--") {
		die sprintf(__("invalid argument %s, expecting --"), $arg);
	}
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
		my ($it) = list_and_choose({ PROMPT => __('What now'),
					     SINGLETON => 1,
					     LIST_FLAT => 4,
					     HEADER => __('*** Commands ***'),
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

process_args();
refresh();
if ($patch_mode_only) {
	patch_update_cmd();
}
else {
	status_cmd();
	main_loop();
}
