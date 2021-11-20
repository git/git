#!/usr/bin/env perl
#
# Copyright (c) 2021-2022 Eric Sunshine <sunshine@sunshineco.com>
#
# This tool scans shell scripts for test definitions and checks those tests for
# problems, such as broken &&-chains, which might hide bugs in the tests
# themselves or in behaviors being exercised by the tests.
#
# Input arguments are pathnames of shell scripts containing test definitions,
# or globs referencing a collection of scripts. For each problem discovered,
# the pathname of the script containing the test is printed along with the test
# name and the test body with a `?!FOO?!` annotation at the location of each
# detected problem, where "FOO" is a tag such as "AMP" which indicates a broken
# &&-chain. Returns zero if no problems are discovered, otherwise non-zero.

use warnings;
use strict;
use File::Glob;
use Getopt::Long;

my $show_stats;
my $emit_all;

# Lexer tokenizes POSIX shell scripts. It is roughly modeled after section 2.3
# "Token Recognition" of POSIX chapter 2 "Shell Command Language". Although
# similar to lexical analyzers for other languages, this one differs in a few
# substantial ways due to quirks of the shell command language.
#
# For instance, in many languages, newline is just whitespace like space or
# TAB, but in shell a newline is a command separator, thus a distinct lexical
# token. A newline is significant and returned as a distinct token even at the
# end of a shell comment.
#
# In other languages, `1+2` would typically be scanned as three tokens
# (`1`, `+`, and `2`), but in shell it is a single token. However, the similar
# `1 + 2`, which embeds whitepace, is scanned as three token in shell, as well.
# In shell, several characters with special meaning lose that meaning when not
# surrounded by whitespace. For instance, the negation operator `!` is special
# when standing alone surrounded by whitespace; whereas in `foo!uucp` it is
# just a plain character in the longer token "foo!uucp". In many other
# languages, `"string"/foo:'string'` might be scanned as five tokens ("string",
# `/`, `foo`, `:`, and 'string'), but in shell, it is just a single token.
#
# The lexical analyzer for the shell command language is also somewhat unusual
# in that it recursively invokes the parser to handle the body of `$(...)`
# expressions which can contain arbitrary shell code. Such expressions may be
# encountered both inside and outside of double-quoted strings.
#
# The lexical analyzer is responsible for consuming shell here-doc bodies which
# extend from the line following a `<<TAG` operator until a line consisting
# solely of `TAG`. Here-doc consumption begins when a newline is encountered.
# It is legal for multiple here-doc `<<TAG` operators to be present on a single
# line, in which case their bodies must be present one following the next, and
# are consumed in the (left-to-right) order the `<<TAG` operators appear on the
# line. A special complication is that the bodies of all here-docs must be
# consumed when the newline is encountered even if the parse context depth has
# changed. For instance, in `cat <<A && x=$(cat <<B &&\n`, bodies of here-docs
# "A" and "B" must be consumed even though "A" was introduced outside the
# recursive parse context in which "B" was introduced and in which the newline
# is encountered.
package Lexer;

sub new {
	my ($class, $parser, $s) = @_;
	bless {
		parser => $parser,
		buff => $s,
		heretags => []
	} => $class;
}

sub scan_heredoc_tag {
	my $self = shift @_;
	${$self->{buff}} =~ /\G(-?)/gc;
	my $indented = $1;
	my $tag = $self->scan_token();
	$tag =~ s/['"\\]//g;
	push(@{$self->{heretags}}, $indented ? "\t$tag" : "$tag");
	return "<<$indented$tag";
}

sub scan_op {
	my ($self, $c) = @_;
	my $b = $self->{buff};
	return $c unless $$b =~ /\G(.)/sgc;
	my $cc = $c . $1;
	return scan_heredoc_tag($self) if $cc eq '<<';
	return $cc if $cc =~ /^(?:&&|\|\||>>|;;|<&|>&|<>|>\|)$/;
	pos($$b)--;
	return $c;
}

sub scan_sqstring {
	my $self = shift @_;
	${$self->{buff}} =~ /\G([^']*'|.*\z)/sgc;
	return "'" . $1;
}

sub scan_dqstring {
	my $self = shift @_;
	my $b = $self->{buff};
	my $s = '"';
	while (1) {
		# slurp up non-special characters
		$s .= $1 if $$b =~ /\G([^"\$\\]+)/gc;
		# handle special characters
		last unless $$b =~ /\G(.)/sgc;
		my $c = $1;
		$s .= '"', last if $c eq '"';
		$s .= '$' . $self->scan_dollar(), next if $c eq '$';
		if ($c eq '\\') {
			$s .= '\\', last unless $$b =~ /\G(.)/sgc;
			$c = $1;
			next if $c eq "\n"; # line splice
			# backslash escapes only $, `, ", \ in dq-string
			$s .= '\\' unless $c =~ /^[\$`"\\]$/;
			$s .= $c;
			next;
		}
		die("internal error scanning dq-string '$c'\n");
	}
	return $s;
}

sub scan_balanced {
	my ($self, $c1, $c2) = @_;
	my $b = $self->{buff};
	my $depth = 1;
	my $s = $c1;
	while ($$b =~ /\G([^\Q$c1$c2\E]*(?:[\Q$c1$c2\E]|\z))/gc) {
		$s .= $1;
		$depth++, next if $s =~ /\Q$c1\E$/;
		$depth--;
		last if $depth == 0;
	}
	return $s;
}

sub scan_subst {
	my $self = shift @_;
	my @tokens = $self->{parser}->parse(qr/^\)$/);
	$self->{parser}->next_token(); # closing ")"
	return @tokens;
}

sub scan_dollar {
	my $self = shift @_;
	my $b = $self->{buff};
	return $self->scan_balanced('(', ')') if $$b =~ /\G\((?=\()/gc; # $((...))
	return '(' . join(' ', $self->scan_subst()) . ')' if $$b =~ /\G\(/gc; # $(...)
	return $self->scan_balanced('{', '}') if $$b =~ /\G\{/gc; # ${...}
	return $1 if $$b =~ /\G(\w+)/gc; # $var
	return $1 if $$b =~ /\G([@*#?$!0-9-])/gc; # $*, $1, $$, etc.
	return '';
}

sub swallow_heredocs {
	my $self = shift @_;
	my $b = $self->{buff};
	my $tags = $self->{heretags};
	while (my $tag = shift @$tags) {
		my $indent = $tag =~ s/^\t// ? '\\s*' : '';
		$$b =~ /(?:\G|\n)$indent\Q$tag\E(?:\n|\z)/gc;
	}
}

sub scan_token {
	my $self = shift @_;
	my $b = $self->{buff};
	my $token = '';
RESTART:
	$$b =~ /\G[ \t]+/gc; # skip whitespace (but not newline)
	return "\n" if $$b =~ /\G#[^\n]*(?:\n|\z)/gc; # comment
	while (1) {
		# slurp up non-special characters
		$token .= $1 if $$b =~ /\G([^\\;&|<>(){}'"\$\s]+)/gc;
		# handle special characters
		last unless $$b =~ /\G(.)/sgc;
		my $c = $1;
		last if $c =~ /^[ \t]$/; # whitespace ends token
		pos($$b)--, last if length($token) && $c =~ /^[;&|<>(){}\n]$/;
		$token .= $self->scan_sqstring(), next if $c eq "'";
		$token .= $self->scan_dqstring(), next if $c eq '"';
		$token .= $c . $self->scan_dollar(), next if $c eq '$';
		$self->swallow_heredocs(), $token = $c, last if $c eq "\n";
		$token = $self->scan_op($c), last if $c =~ /^[;&|<>]$/;
		$token = $c, last if $c =~ /^[(){}]$/;
		if ($c eq '\\') {
			$token .= '\\', last unless $$b =~ /\G(.)/sgc;
			$c = $1;
			next if $c eq "\n" && length($token); # line splice
			goto RESTART if $c eq "\n"; # line splice
			$token .= '\\' . $c;
			next;
		}
		die("internal error scanning character '$c'\n");
	}
	return length($token) ? $token : undef;
}

# ShellParser parses POSIX shell scripts (with minor extensions for Bash). It
# is a recursive descent parser very roughly modeled after section 2.10 "Shell
# Grammar" of POSIX chapter 2 "Shell Command Language".
package ShellParser;

sub new {
	my ($class, $s) = @_;
	my $self = bless {
		buff => [],
		stop => [],
		output => []
	} => $class;
	$self->{lexer} = Lexer->new($self, $s);
	return $self;
}

sub next_token {
	my $self = shift @_;
	return pop(@{$self->{buff}}) if @{$self->{buff}};
	return $self->{lexer}->scan_token();
}

sub untoken {
	my $self = shift @_;
	push(@{$self->{buff}}, @_);
}

sub peek {
	my $self = shift @_;
	my $token = $self->next_token();
	return undef unless defined($token);
	$self->untoken($token);
	return $token;
}

sub stop_at {
	my ($self, $token) = @_;
	return 1 unless defined($token);
	my $stop = ${$self->{stop}}[-1] if @{$self->{stop}};
	return defined($stop) && $token =~ $stop;
}

sub expect {
	my ($self, $expect) = @_;
	my $token = $self->next_token();
	return $token if defined($token) && $token eq $expect;
	push(@{$self->{output}}, "?!ERR?! expected '$expect' but found '" . (defined($token) ? $token : "<end-of-input>") . "'\n");
	$self->untoken($token) if defined($token);
	return ();
}

sub optional_newlines {
	my $self = shift @_;
	my @tokens;
	while (my $token = $self->peek()) {
		last unless $token eq "\n";
		push(@tokens, $self->next_token());
	}
	return @tokens;
}

sub parse_group {
	my $self = shift @_;
	return ($self->parse(qr/^}$/),
		$self->expect('}'));
}

sub parse_subshell {
	my $self = shift @_;
	return ($self->parse(qr/^\)$/),
		$self->expect(')'));
}

sub parse_case_pattern {
	my $self = shift @_;
	my @tokens;
	while (defined(my $token = $self->next_token())) {
		push(@tokens, $token);
		last if $token eq ')';
	}
	return @tokens;
}

sub parse_case {
	my $self = shift @_;
	my @tokens;
	push(@tokens,
	     $self->next_token(), # subject
	     $self->optional_newlines(),
	     $self->expect('in'),
	     $self->optional_newlines());
	while (1) {
		my $token = $self->peek();
		last unless defined($token) && $token ne 'esac';
		push(@tokens,
		     $self->parse_case_pattern(),
		     $self->optional_newlines(),
		     $self->parse(qr/^(?:;;|esac)$/)); # item body
		$token = $self->peek();
		last unless defined($token) && $token ne 'esac';
		push(@tokens,
		     $self->expect(';;'),
		     $self->optional_newlines());
	}
	push(@tokens, $self->expect('esac'));
	return @tokens;
}

sub parse_for {
	my $self = shift @_;
	my @tokens;
	push(@tokens,
	     $self->next_token(), # variable
	     $self->optional_newlines());
	my $token = $self->peek();
	if (defined($token) && $token eq 'in') {
		push(@tokens,
		     $self->expect('in'),
		     $self->optional_newlines());
	}
	push(@tokens,
	     $self->parse(qr/^do$/), # items
	     $self->expect('do'),
	     $self->optional_newlines(),
	     $self->parse_loop_body(),
	     $self->expect('done'));
	return @tokens;
}

sub parse_if {
	my $self = shift @_;
	my @tokens;
	while (1) {
		push(@tokens,
		     $self->parse(qr/^then$/), # if/elif condition
		     $self->expect('then'),
		     $self->optional_newlines(),
		     $self->parse(qr/^(?:elif|else|fi)$/)); # if/elif body
		my $token = $self->peek();
		last unless defined($token) && $token eq 'elif';
		push(@tokens, $self->expect('elif'));
	}
	my $token = $self->peek();
	if (defined($token) && $token eq 'else') {
		push(@tokens,
		     $self->expect('else'),
		     $self->optional_newlines(),
		     $self->parse(qr/^fi$/)); # else body
	}
	push(@tokens, $self->expect('fi'));
	return @tokens;
}

sub parse_loop_body {
	my $self = shift @_;
	return $self->parse(qr/^done$/);
}

sub parse_loop {
	my $self = shift @_;
	return ($self->parse(qr/^do$/), # condition
		$self->expect('do'),
		$self->optional_newlines(),
		$self->parse_loop_body(),
		$self->expect('done'));
}

sub parse_func {
	my $self = shift @_;
	return ($self->expect('('),
		$self->expect(')'),
		$self->optional_newlines(),
		$self->parse_cmd()); # body
}

sub parse_bash_array_assignment {
	my $self = shift @_;
	my @tokens = $self->expect('(');
	while (defined(my $token = $self->next_token())) {
		push(@tokens, $token);
		last if $token eq ')';
	}
	return @tokens;
}

my %compound = (
	'{' => \&parse_group,
	'(' => \&parse_subshell,
	'case' => \&parse_case,
	'for' => \&parse_for,
	'if' => \&parse_if,
	'until' => \&parse_loop,
	'while' => \&parse_loop);

sub parse_cmd {
	my $self = shift @_;
	my $cmd = $self->next_token();
	return () unless defined($cmd);
	return $cmd if $cmd eq "\n";

	my $token;
	my @tokens = $cmd;
	if ($cmd eq '!') {
		push(@tokens, $self->parse_cmd());
		return @tokens;
	} elsif (my $f = $compound{$cmd}) {
		push(@tokens, $self->$f());
	} elsif (defined($token = $self->peek()) && $token eq '(') {
		if ($cmd !~ /\w=$/) {
			push(@tokens, $self->parse_func());
			return @tokens;
		}
		$tokens[-1] .= join(' ', $self->parse_bash_array_assignment());
	}

	while (defined(my $token = $self->next_token())) {
		$self->untoken($token), last if $self->stop_at($token);
		push(@tokens, $token);
		last if $token =~ /^(?:[;&\n|]|&&|\|\|)$/;
	}
	push(@tokens, $self->next_token()) if $tokens[-1] ne "\n" && defined($token = $self->peek()) && $token eq "\n";
	return @tokens;
}

sub accumulate {
	my ($self, $tokens, $cmd) = @_;
	push(@$tokens, @$cmd);
}

sub parse {
	my ($self, $stop) = @_;
	push(@{$self->{stop}}, $stop);
	goto DONE if $self->stop_at($self->peek());
	my @tokens;
	while (my @cmd = $self->parse_cmd()) {
		$self->accumulate(\@tokens, \@cmd);
		last if $self->stop_at($self->peek());
	}
DONE:
	pop(@{$self->{stop}});
	return @tokens;
}

package ScriptParser;

sub new {
	my $class = shift @_;
	my $self = bless {} => $class;
	$self->{output} = [];
	$self->{ntests} = 0;
	return $self;
}

sub parse_cmd {
	return undef;
}

# main contains high-level functionality for processing command-line switches,
# feeding input test scripts to ScriptParser, and reporting results.
package main;

my $getnow = sub { return time(); };
my $interval = sub { return time() - shift; };
if (eval {require Time::HiRes; Time::HiRes->import(); 1;}) {
	$getnow = sub { return [Time::HiRes::gettimeofday()]; };
	$interval = sub { return Time::HiRes::tv_interval(shift); };
}

sub show_stats {
	my ($start_time, $stats) = @_;
	my $walltime = $interval->($start_time);
	my ($usertime) = times();
	my ($total_workers, $total_scripts, $total_tests, $total_errs) = (0, 0, 0, 0);
	for (@$stats) {
		my ($worker, $nscripts, $ntests, $nerrs) = @$_;
		print(STDERR "worker $worker: $nscripts scripts, $ntests tests, $nerrs errors\n");
		$total_workers++;
		$total_scripts += $nscripts;
		$total_tests += $ntests;
		$total_errs += $nerrs;
	}
	printf(STDERR "total: %d workers, %d scripts, %d tests, %d errors, %.2fs/%.2fs (wall/user)\n", $total_workers, $total_scripts, $total_tests, $total_errs, $walltime, $usertime);
}

sub check_script {
	my ($id, $next_script, $emit) = @_;
	my ($nscripts, $ntests, $nerrs) = (0, 0, 0);
	while (my $path = $next_script->()) {
		$nscripts++;
		my $fh;
		unless (open($fh, "<", $path)) {
			$emit->("?!ERR?! $path: $!\n");
			next;
		}
		my $s = do { local $/; <$fh> };
		close($fh);
		my $parser = ScriptParser->new(\$s);
		1 while $parser->parse_cmd();
		if (@{$parser->{output}}) {
			my $s = join('', @{$parser->{output}});
			$emit->("# chainlint: $path\n" . $s);
			$nerrs += () = $s =~ /\?![^?]+\?!/g;
		}
		$ntests += $parser->{ntests};
	}
	return [$id, $nscripts, $ntests, $nerrs];
}

sub exit_code {
	my $stats = shift @_;
	for (@$stats) {
		my ($worker, $nscripts, $ntests, $nerrs) = @$_;
		return 1 if $nerrs;
	}
	return 0;
}

Getopt::Long::Configure(qw{bundling});
GetOptions(
	"emit-all!" => \$emit_all,
	"stats|show-stats!" => \$show_stats) or die("option error\n");

my $start_time = $getnow->();
my @stats;

my @scripts;
push(@scripts, File::Glob::bsd_glob($_)) for (@ARGV);
unless (@scripts) {
	show_stats($start_time, \@stats) if $show_stats;
	exit;
}

push(@stats, check_script(1, sub { shift(@scripts); }, sub { print(@_); }));
show_stats($start_time, \@stats) if $show_stats;
exit(exit_code(\@stats));
