#!/usr/bin/perl
use strict;
use warnings;
use IO::Pty;
use File::Copy;

# Run @$argv in the background with stdout redirected to $out.
sub start_child {
	my ($argv, $out) = @_;
	my $pid = fork;
	if (not defined $pid) {
		die "fork failed: $!"
	} elsif ($pid == 0) {
		open STDOUT, ">&", $out;
		close $out;
		exec(@$argv) or die "cannot exec '$argv->[0]': $!"
	}
	return $pid;
}

# Wait for $pid to finish.
sub finish_child {
	# Simplified from wait_or_whine() in run-command.c.
	my ($pid) = @_;

	my $waiting = waitpid($pid, 0);
	if ($waiting < 0) {
		die "waitpid failed: $!";
	} elsif ($? & 127) {
		my $code = $? & 127;
		warn "died of signal $code";
		return $code - 128;
	} else {
		return $? >> 8;
	}
}

sub xsendfile {
	my ($out, $in) = @_;

	# Note: the real sendfile() cannot read from a terminal.

	# It is unspecified by POSIX whether reads
	# from a disconnected terminal will return
	# EIO (as in AIX 4.x, IRIX, and Linux) or
	# end-of-file.  Either is fine.
	copy($in, $out, 4096) or $!{EIO} or die "cannot copy from child: $!";
}

if ($#ARGV < 1) {
	die "usage: test-terminal program args";
}
my $master = new IO::Pty;
my $slave = $master->slave;
my $pid = start_child(\@ARGV, $slave);
close $slave;
xsendfile(\*STDOUT, $master);
exit(finish_child($pid));
