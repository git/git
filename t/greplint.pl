#!/usr/bin/env perl

# Detect bare 'grep' used as a test assertion where 'test_grep'
# should be used, and '! test_grep' where 'test_grep !' should
# be used.
#
# The shared shell parser tokenizes test bodies so that 'grep'
# inside heredocs, command substitutions like $(grep ...), and
# quoted strings is collapsed into a single token and never seen
# by our check.  A line-oriented approach would need to track
# heredoc delimiters, nested $() depth, and cross-line pipe
# state to avoid false positives on patterns like:
#
#   write_script foo.sh <<-\EOF
#   grep pattern file    # data, not an assertion
#   EOF
#
# The Lexer already handles these.

use warnings;
use strict;
use File::Basename;
do(dirname($0) . "/lib-shell-parser.pl")
	or die "$0: failed to load lib-shell-parser.pl: $@$!\n";

my $exit_code = 0;

# GrepLintParser inherits ScriptParser's ability to find
# test_expect_success/failure blocks and call check_test()
# on each body.  We override check_test() to walk the token
# stream looking for bare grep assertions.
package GrepLintParser;

our @ISA = ('ScriptParser');

# After these tokens, the next token is a command word.
# For example, in 'echo foo && grep bar file', the 'grep'
# after '&&' is at command position and should be flagged.
my %cmd_start = map { $_ => 1 } qw(&& || ; ;; do then else elif), "\n", '{', '(';

# Tokens indicating grep's output is piped or redirected.
my %filter_op = map { $_ => 1 } qw(| > >> <);

# A token is at "command word" position if the shell would
# interpret it as a program name rather than an argument.
# Only 'grep' at command position is an assertion we should
# flag; 'grep' as an argument ('test_must_fail grep') or
# value ('for cmd in grep sed') is not.
sub is_command_word {
	my ($tokens, $pos) = @_;
	return 1 if $pos == 0;
	for (my $j = $pos - 1; $j >= 0; $j--) {
		my $t = $tokens->[$j]->[0];
		# After a separator or pipe, a new command starts.
		return 1 if $cmd_start{$t} || $t eq '|';
		# After '}' or ')', what follows is a separator or
		# redirect on the compound command, not a new command.
		return 0 if $t eq '}' || $t eq ')';
		# '!' is a prefix that does not consume command
		# position; keep scanning to find what precedes it.
		next if $t eq '!';
		# Any other word means we are past the command word.
		return 0;
	}
	return 1;
}

# Some bare greps are intentional (e.g. file may not exist,
# data filter).  A '# lint-ok' annotation on the source line
# suppresses the warning.
sub lint_ok {
	my ($raw_lines, $ln) = @_;
	if ($ln < 1 || $ln > @$raw_lines) {
		warn "lint_ok: line number $ln out of range (1.." .
		    scalar(@$raw_lines) . ")\n";
		return 0;
	}
	return $raw_lines->[$ln - 1] =~ /lint-ok/;
}

# Grep is a filter (not an assertion) if it receives piped
# input or sends its output to a pipe or redirect.  Check
# both directions from grep's position in the token stream.
sub is_filter {
	my ($tokens, $pos) = @_;
	# Backward: is grep receiving piped input?
	# Newlines don't break pipes ('cmd |\n grep' is one
	# pipeline), so skip past them.
	for (my $j = $pos - 1; $j >= 0; $j--) {
		my $t = $tokens->[$j]->[0];
		return 1 if $t eq '|';
		next if $t eq "\n";
		last if $cmd_start{$t} || $t eq '}' || $t eq ')';
	}
	# Forward: is grep piping or redirecting output?
	# Unlike the backward scan, we do not skip newlines here:
	# a bare newline is a command boundary, and redirects or
	# pipes must appear on the same line as grep (or after a
	# line continuation, which the Lexer consumes).
	for (my $j = $pos + 1; $j < @$tokens; $j++) {
		my $t = $tokens->[$j]->[0];
		return 0 if $cmd_start{$t};
		return 1 if $filter_op{$t};
	}
	return 0;
}

# Map a body-relative line number to a file line number.
# For double-quoted bodies, backslash-continuation lines
# (\<newline>) are consumed by the Lexer without appearing
# in the body text, so the inner parser sees fewer lines
# than the source file has.  We walk the source lines to
# count continuations and adjust accordingly.
sub body_to_file_line {
	my ($body_lineno, $body_token, $raw_lines, $body_start) = @_;
	my $body_text = $body_token->[0];
	my $body_end_line = $body_token->[4];
	unless ($body_start && $body_start >= 1) {
		warn "body_start is not a positive integer\n";
		return $body_lineno;
	}
	my $file_lineno = $body_lineno + $body_start - 1;
	# Only double-quoted bodies have line splices.
	return $file_lineno unless $body_text =~ /^"/;
	my $adj = 0;
	my $lines_seen = 0;
	unless ($body_end_line && $body_end_line >= $body_start) {
		warn "body_end_line is not set for double-quoted body\n";
		return $file_lineno;
	}
	my $end = $body_end_line;
	if ($end > @$raw_lines) {
		warn "body_end_line ($end) exceeds file length (" .
		    scalar(@$raw_lines) . ")\n";
		return $file_lineno;
	}
	my $src_ln = $body_start;
	while ($src_ln <= $end && $lines_seen < $body_lineno) {
		my $line = $raw_lines->[$src_ln - 1];
		# Odd trailing backslashes = continuation (\<nl>).
		# Even = escaped backslashes (\\), not a continuation.
		if ($line =~ /(\\*)$/ && length($1) % 2 == 1) {
			$adj++;
		} else {
			$lines_seen++;
		}
		$src_ln++;
	}
	if ($lines_seen < $body_lineno) {
		warn "body_lineno ($body_lineno) not found within body range " .
		    "($body_start..$end)\n";
	}
	return $file_lineno + $adj;
}

# ScriptParser calls this for each test body found in the script.
sub check_test {
	my $self = shift @_;
	my $title = ScriptParser::unwrap(shift @_);
	my $body_token = shift @_;
	my $body_start = $body_token->[3];
	my $body = ScriptParser::unwrap($body_token);
	# Handle heredoc-style test bodies:
	#   test_expect_success 'title' - <<\EOF
	#   grep pattern file
	#   EOF
	# The '-' signals that the body follows as a heredoc.
	if ($body eq '-') {
		my $herebody = shift @_;
		if ($herebody) {
			$body = $herebody->{content};
			$body_start = $herebody->{start_line};
		}
	}
	return unless $body;

	my $raw_lines = $self->{raw_lines};

	# The outer parser gives us the body as an opaque string.
	# Parse it to get individual tokens with command boundaries.
	my $parser = ShellParser->new(\$body);
	my @tokens = $parser->parse();

	my $file = $self->{file};

	for (my $i = 0; $i < @tokens; $i++) {
		my $text = $tokens[$i]->[0];
		next unless is_command_word(\@tokens, $i);

		my $token_lineno = $tokens[$i]->[3];
		unless (defined($token_lineno) && $token_lineno >= 1) {
			warn "token has no line number\n";
			next;
		}
		my $file_lineno = body_to_file_line(
			$token_lineno,
			$body_token, $raw_lines, $body_start);

		# '!' negates the exit code without consuming command
		# position.  '! test_grep' is an anti-pattern because
		# test_grep only prints diagnostics on grep failure,
		# and '!' inverts after that decision is already made.
		if ($text eq '!') {
			if ($i + 1 < @tokens &&
			    $tokens[$i + 1]->[0] eq 'test_grep' &&
			    !lint_ok($raw_lines, $file_lineno)) {
				print "$file:$file_lineno: error: ",
				    'use "test_grep !" instead of ',
				    '"! test_grep"', "\n";
				$exit_code = 1;
			}
			next;
		}

		# Bare grep as a command (not a filter) is a test
		# assertion that should use test_grep for better
		# failure diagnostics.
		if ($text eq 'grep' &&
		    !is_filter(\@tokens, $i) &&
		    !lint_ok($raw_lines, $file_lineno)) {
			print "$file:$file_lineno: error: ",
			    "bare grep outside pipeline ",
			    "(use test_grep)\n";
			$exit_code = 1;
		}
	}
}

package main;

for my $file (@ARGV) {
	open(my $fh, '<:unix:crlf', $file) or die "$0: $file: $!\n";
	my @raw_lines = <$fh>;
	close $fh;
	my $s = join('', @raw_lines);
	my $parser = GrepLintParser->new(\$s);
	$parser->{file} = $file;
	$parser->{raw_lines} = \@raw_lines;
	$parser->parse();
}
exit $exit_code;
