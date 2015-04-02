package Git::SVN::Editor;
use vars qw/@ISA $_rmdir $_cp_similarity $_find_copies_harder $_rename_limit/;
use strict;
use warnings;
use SVN::Core;
use SVN::Delta;
use Carp qw/croak/;
use Git qw/command command_oneline command_noisy command_output_pipe
           command_input_pipe command_close_pipe
           command_bidi_pipe command_close_bidi_pipe/;
BEGIN {
	@ISA = qw(SVN::Delta::Editor);
}

sub new {
	my ($class, $opts) = @_;
	foreach (qw/svn_path r ra tree_a tree_b log editor_cb/) {
		die "$_ required!\n" unless (defined $opts->{$_});
	}

	my $pool = SVN::Pool->new;
	my $mods = generate_diff($opts->{tree_a}, $opts->{tree_b});
	my $types = check_diff_paths($opts->{ra}, $opts->{svn_path},
	                             $opts->{r}, $mods);

	# $opts->{ra} functions should not be used after this:
	my @ce  = $opts->{ra}->get_commit_editor($opts->{log},
	                                        $opts->{editor_cb}, $pool);
	my $self = SVN::Delta::Editor->new(@ce, $pool);
	bless $self, $class;
	foreach (qw/svn_path r tree_a tree_b/) {
		$self->{$_} = $opts->{$_};
	}
	$self->{url} = $opts->{ra}->{url};
	$self->{mods} = $mods;
	$self->{types} = $types;
	$self->{pool} = $pool;
	$self->{bat} = { '' => $self->open_root($self->{r}, $self->{pool}) };
	$self->{rm} = { };
	$self->{path_prefix} = length $self->{svn_path} ?
	                       "$self->{svn_path}/" : '';
	$self->{config} = $opts->{config};
	$self->{mergeinfo} = $opts->{mergeinfo};
	return $self;
}

sub generate_diff {
	my ($tree_a, $tree_b) = @_;
	my @diff_tree = qw(diff-tree -z -r);
	if ($_cp_similarity) {
		push @diff_tree, "-C$_cp_similarity";
	} else {
		push @diff_tree, '-C';
	}
	push @diff_tree, '--find-copies-harder' if $_find_copies_harder;
	push @diff_tree, "-l$_rename_limit" if defined $_rename_limit;
	push @diff_tree, $tree_a, $tree_b;
	my ($diff_fh, $ctx) = command_output_pipe(@diff_tree);
	local $/ = "\0";
	my $state = 'meta';
	my @mods;
	while (<$diff_fh>) {
		chomp $_; # this gets rid of the trailing "\0"
		if ($state eq 'meta' && /^:(\d{6})\s(\d{6})\s
					($::sha1)\s($::sha1)\s
					([MTCRAD])\d*$/xo) {
			push @mods, {	mode_a => $1, mode_b => $2,
					sha1_a => $3, sha1_b => $4,
					chg => $5 };
			if ($5 =~ /^(?:C|R)$/) {
				$state = 'file_a';
			} else {
				$state = 'file_b';
			}
		} elsif ($state eq 'file_a') {
			my $x = $mods[$#mods] or croak "Empty array\n";
			if ($x->{chg} !~ /^(?:C|R)$/) {
				croak "Error parsing $_, $x->{chg}\n";
			}
			$x->{file_a} = $_;
			$state = 'file_b';
		} elsif ($state eq 'file_b') {
			my $x = $mods[$#mods] or croak "Empty array\n";
			if (exists $x->{file_a} && $x->{chg} !~ /^(?:C|R)$/) {
				croak "Error parsing $_, $x->{chg}\n";
			}
			if (!exists $x->{file_a} && $x->{chg} =~ /^(?:C|R)$/) {
				croak "Error parsing $_, $x->{chg}\n";
			}
			$x->{file_b} = $_;
			$state = 'meta';
		} else {
			croak "Error parsing $_\n";
		}
	}
	command_close_pipe($diff_fh, $ctx);
	\@mods;
}

sub check_diff_paths {
	my ($ra, $pfx, $rev, $mods) = @_;
	my %types;
	$pfx .= '/' if length $pfx;

	sub type_diff_paths {
		my ($ra, $types, $path, $rev) = @_;
		my @p = split m#/+#, $path;
		my $c = shift @p;
		unless (defined $types->{$c}) {
			$types->{$c} = $ra->check_path($c, $rev);
		}
		while (@p) {
			$c .= '/' . shift @p;
			next if defined $types->{$c};
			$types->{$c} = $ra->check_path($c, $rev);
		}
	}

	foreach my $m (@$mods) {
		foreach my $f (qw/file_a file_b/) {
			next unless defined $m->{$f};
			my ($dir) = ($m->{$f} =~ m#^(.*?)/?(?:[^/]+)$#);
			if (length $pfx.$dir && ! defined $types{$dir}) {
				type_diff_paths($ra, \%types, $pfx.$dir, $rev);
			}
		}
	}
	\%types;
}

sub split_path {
	return ($_[0] =~ m#^(.*?)/?([^/]+)$#);
}

sub repo_path {
	my ($self, $path) = @_;
	if (my $enc = $self->{pathnameencoding}) {
		require Encode;
		Encode::from_to($path, $enc, 'UTF-8');
	}
	$self->{path_prefix}.(defined $path ? $path : '');
}

sub url_path {
	my ($self, $path) = @_;
	if ($self->{url} =~ m#^https?://#) {
		# characters are taken from subversion/libsvn_subr/path.c
		$path =~ s#([^~a-zA-Z0-9_./!$&'()*+,-])#sprintf("%%%02X",ord($1))#eg;
	}
	$self->{url} . '/' . $self->repo_path($path);
}

sub rmdirs {
	my ($self) = @_;
	my $rm = $self->{rm};
	delete $rm->{''}; # we never delete the url we're tracking
	return unless %$rm;

	foreach (keys %$rm) {
		my @d = split m#/#, $_;
		my $c = shift @d;
		$rm->{$c} = 1;
		while (@d) {
			$c .= '/' . shift @d;
			$rm->{$c} = 1;
		}
	}
	delete $rm->{$self->{svn_path}};
	delete $rm->{''}; # we never delete the url we're tracking
	return unless %$rm;

	my ($fh, $ctx) = command_output_pipe(qw/ls-tree --name-only -r -z/,
	                                     $self->{tree_b});
	local $/ = "\0";
	while (<$fh>) {
		chomp;
		my @dn = split m#/#, $_;
		while (pop @dn) {
			delete $rm->{join '/', @dn};
		}
		unless (%$rm) {
			close $fh;
			return;
		}
	}
	command_close_pipe($fh, $ctx);

	my ($r, $p, $bat) = ($self->{r}, $self->{pool}, $self->{bat});
	foreach my $d (sort { $b =~ tr#/#/# <=> $a =~ tr#/#/# } keys %$rm) {
		$self->close_directory($bat->{$d}, $p);
		my ($dn) = ($d =~ m#^(.*?)/?(?:[^/]+)$#);
		print "\tD+\t$d/\n" unless $::_q;
		$self->SUPER::delete_entry($d, $r, $bat->{$dn}, $p);
		delete $bat->{$d};
	}
}

sub open_or_add_dir {
	my ($self, $full_path, $baton, $deletions) = @_;
	my $t = $self->{types}->{$full_path};
	if (!defined $t) {
		die "$full_path not known in r$self->{r} or we have a bug!\n";
	}
	{
		no warnings 'once';
		# SVN::Node::none and SVN::Node::file are used only once,
		# so we're shutting up Perl's warnings about them.
		if ($t == $SVN::Node::none || defined($deletions->{$full_path})) {
			return $self->add_directory($full_path, $baton,
			    undef, -1, $self->{pool});
		} elsif ($t == $SVN::Node::dir) {
			return $self->open_directory($full_path, $baton,
			    $self->{r}, $self->{pool});
		} # no warnings 'once'
		print STDERR "$full_path already exists in repository at ",
		    "r$self->{r} and it is not a directory (",
		    ($t == $SVN::Node::file ? 'file' : 'unknown'),"/$t)\n";
	} # no warnings 'once'
	exit 1;
}

sub ensure_path {
	my ($self, $path, $deletions) = @_;
	my $bat = $self->{bat};
	my $repo_path = $self->repo_path($path);
	return $bat->{''} unless (length $repo_path);

	my @p = split m#/+#, $repo_path;
	my $c = shift @p;
	$bat->{$c} ||= $self->open_or_add_dir($c, $bat->{''}, $deletions);
	while (@p) {
		my $c0 = $c;
		$c .= '/' . shift @p;
		$bat->{$c} ||= $self->open_or_add_dir($c, $bat->{$c0}, $deletions);
	}
	return $bat->{$c};
}

# Subroutine to convert a globbing pattern to a regular expression.
# From perl cookbook.
sub glob2pat {
	my $globstr = shift;
	my %patmap = ('*' => '.*', '?' => '.', '[' => '[', ']' => ']');
	$globstr =~ s{(.)} { $patmap{$1} || "\Q$1" }ge;
	return '^' . $globstr . '$';
}

sub check_autoprop {
	my ($self, $pattern, $properties, $file, $fbat) = @_;
	# Convert the globbing pattern to a regular expression.
	my $regex = glob2pat($pattern);
	# Check if the pattern matches the file name.
	if($file =~ m/($regex)/) {
		# Parse the list of properties to set.
		my @props = split(/;/, $properties);
		foreach my $prop (@props) {
			# Parse 'name=value' syntax and set the property.
			if ($prop =~ /([^=]+)=(.*)/) {
				my ($n,$v) = ($1,$2);
				for ($n, $v) {
					s/^\s+//; s/\s+$//;
				}
				$self->change_file_prop($fbat, $n, $v);
			}
		}
	}
}

sub apply_autoprops {
	my ($self, $file, $fbat) = @_;
	my $conf_t = ${$self->{config}}{'config'};
	no warnings 'once';
	# Check [miscellany]/enable-auto-props in svn configuration.
	if (SVN::_Core::svn_config_get_bool(
		$conf_t,
		$SVN::_Core::SVN_CONFIG_SECTION_MISCELLANY,
		$SVN::_Core::SVN_CONFIG_OPTION_ENABLE_AUTO_PROPS,
		0)) {
		# Auto-props are enabled.  Enumerate them to look for matches.
		my $callback = sub {
			$self->check_autoprop($_[0], $_[1], $file, $fbat);
		};
		SVN::_Core::svn_config_enumerate(
			$conf_t,
			$SVN::_Core::SVN_CONFIG_SECTION_AUTO_PROPS,
			$callback);
	}
}

sub check_attr {
	my ($attr,$path) = @_;
	my $val = command_oneline("check-attr", $attr, "--", $path);
	if ($val) { $val =~ s/^[^:]*:\s*[^:]*:\s*(.*)\s*$/$1/; }
	return $val;
}

sub apply_manualprops {
	my ($self, $file, $fbat) = @_;
	my $pending_properties = check_attr( "svn-properties", $file );
	if ($pending_properties eq "") { return; }
	# Parse the list of properties to set.
	my @props = split(/;/, $pending_properties);
	# TODO: get existing properties to compare to
	# - this fails for add so currently not done
	# my $existing_props = ::get_svnprops($file);
	my $existing_props = {};
	# TODO: caching svn properties or storing them in .gitattributes
	# would make that faster
	foreach my $prop (@props) {
		# Parse 'name=value' syntax and set the property.
		if ($prop =~ /([^=]+)=(.*)/) {
			my ($n,$v) = ($1,$2);
			for ($n, $v) {
				s/^\s+//; s/\s+$//;
			}
			my $existing = $existing_props->{$n};
			if (!defined($existing) || $existing ne $v) {
			    $self->change_file_prop($fbat, $n, $v);
			}
		}
	}
}

sub A {
	my ($self, $m, $deletions) = @_;
	my ($dir, $file) = split_path($m->{file_b});
	my $pbat = $self->ensure_path($dir, $deletions);
	my $fbat = $self->add_file($self->repo_path($m->{file_b}), $pbat,
					undef, -1);
	print "\tA\t$m->{file_b}\n" unless $::_q;
	$self->apply_autoprops($file, $fbat);
	$self->apply_manualprops($m->{file_b}, $fbat);
	$self->chg_file($fbat, $m);
	$self->close_file($fbat,undef,$self->{pool});
}

sub C {
	my ($self, $m, $deletions) = @_;
	my ($dir, $file) = split_path($m->{file_b});
	my $pbat = $self->ensure_path($dir, $deletions);
	# workaround for a bug in svn serf backend (v1.8.5 and below):
	# store third argument to ->add_file() in a local variable, to make it
	# have the same lifetime as $fbat
	my $upa = $self->url_path($m->{file_a});
	my $fbat = $self->add_file($self->repo_path($m->{file_b}), $pbat,
				$upa, $self->{r});
	print "\tC\t$m->{file_a} => $m->{file_b}\n" unless $::_q;
	$self->apply_manualprops($m->{file_b}, $fbat);
	$self->chg_file($fbat, $m);
	$self->close_file($fbat,undef,$self->{pool});
}

sub delete_entry {
	my ($self, $path, $pbat) = @_;
	my $rpath = $self->repo_path($path);
	my ($dir, $file) = split_path($rpath);
	$self->{rm}->{$dir} = 1;
	$self->SUPER::delete_entry($rpath, $self->{r}, $pbat, $self->{pool});
}

sub R {
	my ($self, $m, $deletions) = @_;
	my ($dir, $file) = split_path($m->{file_b});
	my $pbat = $self->ensure_path($dir, $deletions);
	# workaround for a bug in svn serf backend, see comment in C() above
	my $upa = $self->url_path($m->{file_a});
	my $fbat = $self->add_file($self->repo_path($m->{file_b}), $pbat,
				$upa, $self->{r});
	print "\tR\t$m->{file_a} => $m->{file_b}\n" unless $::_q;
	$self->apply_autoprops($file, $fbat);
	$self->apply_manualprops($m->{file_b}, $fbat);
	$self->chg_file($fbat, $m);
	$self->close_file($fbat,undef,$self->{pool});

	($dir, $file) = split_path($m->{file_a});
	$pbat = $self->ensure_path($dir, $deletions);
	$self->delete_entry($m->{file_a}, $pbat);
}

sub M {
	my ($self, $m, $deletions) = @_;
	my ($dir, $file) = split_path($m->{file_b});
	my $pbat = $self->ensure_path($dir, $deletions);
	my $fbat = $self->open_file($self->repo_path($m->{file_b}),
				$pbat,$self->{r},$self->{pool});
	print "\t$m->{chg}\t$m->{file_b}\n" unless $::_q;
	$self->apply_manualprops($m->{file_b}, $fbat);
	$self->chg_file($fbat, $m);
	$self->close_file($fbat,undef,$self->{pool});
}

sub T {
	my ($self, $m, $deletions) = @_;

	# Work around subversion issue 4091: toggling the "is a
	# symlink" property requires removing and re-adding a
	# file or else "svn up" on affected clients trips an
	# assertion and aborts.
	if (($m->{mode_b} =~ /^120/ && $m->{mode_a} !~ /^120/) ||
	    ($m->{mode_b} !~ /^120/ && $m->{mode_a} =~ /^120/)) {
		$self->D({
			mode_a => $m->{mode_a}, mode_b => '000000',
			sha1_a => $m->{sha1_a}, sha1_b => '0' x 40,
			chg => 'D', file_b => $m->{file_b}
		}, $deletions);
		$self->A({
			mode_a => '000000', mode_b => $m->{mode_b},
			sha1_a => '0' x 40, sha1_b => $m->{sha1_b},
			chg => 'A', file_b => $m->{file_b}
		}, $deletions);
		return;
	}

	$self->M($m, $deletions);
}

sub change_file_prop {
	my ($self, $fbat, $pname, $pval) = @_;
	$self->SUPER::change_file_prop($fbat, $pname, $pval, $self->{pool});
}

sub change_dir_prop {
	my ($self, $pbat, $pname, $pval) = @_;
	$self->SUPER::change_dir_prop($pbat, $pname, $pval, $self->{pool});
}

sub _chg_file_get_blob ($$$$) {
	my ($self, $fbat, $m, $which) = @_;
	my $fh = $::_repository->temp_acquire("git_blob_$which");
	if ($m->{"mode_$which"} =~ /^120/) {
		print $fh 'link ' or croak $!;
		$self->change_file_prop($fbat,'svn:special','*');
	} elsif ($m->{mode_a} =~ /^120/ && $m->{"mode_$which"} !~ /^120/) {
		$self->change_file_prop($fbat,'svn:special',undef);
	}
	my $blob = $m->{"sha1_$which"};
	return ($fh,) if ($blob =~ /^0{40}$/);
	my $size = $::_repository->cat_blob($blob, $fh);
	croak "Failed to read object $blob" if ($size < 0);
	$fh->flush == 0 or croak $!;
	seek $fh, 0, 0 or croak $!;

	my $exp = ::md5sum($fh);
	seek $fh, 0, 0 or croak $!;
	return ($fh, $exp);
}

sub chg_file {
	my ($self, $fbat, $m) = @_;
	if ($m->{mode_b} =~ /755$/ && $m->{mode_a} !~ /755$/) {
		$self->change_file_prop($fbat,'svn:executable','*');
	} elsif ($m->{mode_b} !~ /755$/ && $m->{mode_a} =~ /755$/) {
		$self->change_file_prop($fbat,'svn:executable',undef);
	}
	my ($fh_a, $exp_a) = _chg_file_get_blob $self, $fbat, $m, 'a';
	my ($fh_b, $exp_b) = _chg_file_get_blob $self, $fbat, $m, 'b';
	my $pool = SVN::Pool->new;
	my $atd = $self->apply_textdelta($fbat, $exp_a, $pool);
	if (-s $fh_a) {
		my $txstream = SVN::TxDelta::new ($fh_a, $fh_b, $pool);
		my $res = SVN::TxDelta::send_txstream($txstream, @$atd, $pool);
		if (defined $res) {
			die "Unexpected result from send_txstream: $res\n",
			    "(SVN::Core::VERSION: $SVN::Core::VERSION)\n";
		}
	} else {
		my $got = SVN::TxDelta::send_stream($fh_b, @$atd, $pool);
		die "Checksum mismatch\nexpected: $exp_b\ngot: $got\n"
		    if ($got ne $exp_b);
	}
	Git::temp_release($fh_b, 1);
	Git::temp_release($fh_a, 1);
	$pool->clear;
}

sub D {
	my ($self, $m, $deletions) = @_;
	my ($dir, $file) = split_path($m->{file_b});
	my $pbat = $self->ensure_path($dir, $deletions);
	print "\tD\t$m->{file_b}\n" unless $::_q;
	$self->delete_entry($m->{file_b}, $pbat);
}

sub close_edit {
	my ($self) = @_;
	my ($p,$bat) = ($self->{pool}, $self->{bat});
	foreach (sort { $b =~ tr#/#/# <=> $a =~ tr#/#/# } keys %$bat) {
		next if $_ eq '';
		$self->close_directory($bat->{$_}, $p);
	}
	$self->close_directory($bat->{''}, $p);
	$self->SUPER::close_edit($p);
	$p->clear;
}

sub abort_edit {
	my ($self) = @_;
	$self->SUPER::abort_edit($self->{pool});
}

sub DESTROY {
	my $self = shift;
	$self->SUPER::DESTROY(@_);
	$self->{pool}->clear;
}

# this drives the editor
sub apply_diff {
	my ($self) = @_;
	my $mods = $self->{mods};
	my %o = ( D => 0, C => 1, R => 2, A => 3, M => 4, T => 5 );
	my %deletions;

	foreach my $m (@$mods) {
		if ($m->{chg} eq "D") {
			$deletions{$m->{file_b}} = 1;
		}
	}

	foreach my $m (sort { $o{$a->{chg}} <=> $o{$b->{chg}} } @$mods) {
		my $f = $m->{chg};
		if (defined $o{$f}) {
			$self->$f($m, \%deletions);
		} else {
			fatal("Invalid change type: $f");
		}
	}

	if (defined($self->{mergeinfo})) {
		$self->change_dir_prop($self->{bat}{''}, "svn:mergeinfo",
			               $self->{mergeinfo});
	}
	$self->rmdirs if $_rmdir;
	if (@$mods == 0 && !defined($self->{mergeinfo})) {
		$self->abort_edit;
	} else {
		$self->close_edit;
	}
	return scalar @$mods;
}

1;
__END__

=head1 NAME

Git::SVN::Editor - commit driver for "git svn set-tree" and dcommit

=head1 SYNOPSIS

	use Git::SVN::Editor;
	use Git::SVN::Ra;

	my $ra = Git::SVN::Ra->new($url);
	my %opts = (
		r => 19,
		log => "log message",
		ra => $ra,
		config => SVN::Core::config_get_config($svn_config_dir),
		tree_a => "$commit^",
		tree_b => "$commit",
		editor_cb => sub { print "Committed r$_[0]\n"; },
		mergeinfo => "/branches/foo:1-10",
		svn_path => "trunk"
	);
	Git::SVN::Editor->new(\%opts)->apply_diff or print "No changes\n";

	my $re = Git::SVN::Editor::glob2pat("trunk/*");
	if ($branchname =~ /$re/) {
		print "matched!\n";
	}

=head1 DESCRIPTION

This module is an implementation detail of the "git svn" command.
Do not use it unless you are developing git-svn.

This module adapts the C<SVN::Delta::Editor> object returned by
C<SVN::Delta::get_commit_editor> and drives it to convey the
difference between two git tree objects to a remote Subversion
repository.

The interface will change as git-svn evolves.

=head1 DEPENDENCIES

Subversion perl bindings,
the core L<Carp> module,
and git's L<Git> helper module.

C<Git::SVN::Editor> has not been tested using callers other than
B<git-svn> itself.

=head1 SEE ALSO

L<SVN::Delta>,
L<Git::SVN::Fetcher>.

=head1 INCOMPATIBILITIES

None reported.

=head1 BUGS

None.
