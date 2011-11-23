#!/usr/bin/perl

use Gtk2 -init;
use Gtk2::SimpleList;

my $hash;
my $fn;
if ( @ARGV == 1 ) {
	$hash = "HEAD";
	$fn = shift;
} elsif ( @ARGV == 2 ) {
	$hash = shift;
	$fn = shift;
} else {
	die "Usage blameview [<rev>] <filename>";
}

Gtk2::Rc->parse_string(<<'EOS');
style "treeview_style"
{
  GtkTreeView::vertical-separator = 0
}
class "GtkTreeView" style "treeview_style"
EOS

my $window = Gtk2::Window->new('toplevel');
$window->signal_connect(destroy => sub { Gtk2->main_quit });
my $vpan = Gtk2::VPaned->new();
$window->add($vpan);
my $scrolled_window = Gtk2::ScrolledWindow->new;
$vpan->pack1($scrolled_window, 1, 1);
my $fileview = Gtk2::SimpleList->new(
    'Commit' => 'text',
    'FileLine' => 'text',
    'Data' => 'text'
);
$scrolled_window->add($fileview);
$fileview->get_column(0)->set_spacing(0);
$fileview->set_size_request(1024, 768);
$fileview->set_rules_hint(1);
$fileview->signal_connect (row_activated => sub {
		my ($sl, $path, $column) = @_;
		my $row_ref = $sl->get_row_data_from_path ($path);
		system("blameview @$row_ref[0]~1 $fn &");
		});

my $commitwindow = Gtk2::ScrolledWindow->new();
$commitwindow->set_policy ('GTK_POLICY_AUTOMATIC','GTK_POLICY_AUTOMATIC');
$vpan->pack2($commitwindow, 1, 1);
my $commit_text = Gtk2::TextView->new();
my $commit_buffer = Gtk2::TextBuffer->new();
$commit_text->set_buffer($commit_buffer);
$commitwindow->add($commit_text);

$fileview->signal_connect (cursor_changed => sub {
		my ($sl) = @_;
		my ($path, $focus_column) = $sl->get_cursor();
		my $row_ref = $sl->get_row_data_from_path ($path);
		my $c_fh;
		open($c_fh,  '-|', "git cat-file commit @$row_ref[0]")
					or die "unable to find commit @$row_ref[0]";
		my @buffer = <$c_fh>;
		$commit_buffer->set_text("@buffer");
		close($c_fh);
		});

my $fh;
open($fh, '-|', "git cat-file blob $hash:$fn")
  or die "unable to open $fn: $!";

while(<$fh>) {
  chomp;
  $fileview->{data}->[$.] = ['HEAD', "$fn:$.", $_];
}

my $blame;
open($blame, '-|', qw(git blame --incremental --), $fn, $hash)
    or die "cannot start git-blame $fn";

Glib::IO->add_watch(fileno($blame), 'in', \&read_blame_line);

$window->show_all;
Gtk2->main;
exit 0;

my %commitinfo = ();

sub flush_blame_line {
	my ($attr) = @_;

	return unless defined $attr;

	my ($commit, $s_lno, $lno, $cnt) =
	    @{$attr}{qw(COMMIT S_LNO LNO CNT)};

	my ($filename, $author, $author_time, $author_tz) =
	    @{$commitinfo{$commit}}{qw(FILENAME AUTHOR AUTHOR-TIME AUTHOR-TZ)};
	my $info = $author . ' ' . format_time($author_time, $author_tz);

	for(my $i = 0; $i < $cnt; $i++) {
		@{$fileview->{data}->[$lno+$i-1]}[0,1,2] =
		(substr($commit, 0, 8), $filename . ':' . ($s_lno+$i));
	}
}

my $buf;
my $current;
sub read_blame_line {

	my $r = sysread($blame, $buf, 1024, length($buf));
	die "I/O error" unless defined $r;

	if ($r == 0) {
		flush_blame_line($current);
		$current = undef;
		return 0;
	}

	while ($buf =~ s/([^\n]*)\n//) {
		my $line = $1;

		if (($commit, $s_lno, $lno, $cnt) =
		    ($line =~ /^([0-9a-f]{40}) (\d+) (\d+) (\d+)$/)) {
			flush_blame_line($current);
			$current = +{
				COMMIT => $1,
				S_LNO => $2,
				LNO => $3,
				CNT => $4,
			};
			next;
		}

		# extended attribute values
		if ($line =~ /^(author|author-mail|author-time|author-tz|committer|committer-mail|committer-time|committer-tz|summary|filename) (.*)$/) {
			my $commit = $current->{COMMIT};
			$commitinfo{$commit}{uc($1)} = $2;
			next;
		}
	}
	return 1;
}

sub format_time {
  my $time = shift;
  my $tz = shift;

  my $minutes = $tz < 0 ? 0-$tz : $tz;
  $minutes = ($minutes / 100)*60 + ($minutes % 100);
  $minutes = $tz < 0 ? 0-$minutes : $minutes;
  $time += $minutes * 60;
  my @t = gmtime($time);
  return sprintf('%04d-%02d-%02d %02d:%02d:%02d %s',
		 $t[5] + 1900, @t[4,3,2,1,0], $tz);
}
